/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <zmk/ble.h>
#include <zmk/hid.h>
#include <toucan/mouse_queue.h>
#include <toucan/ble_mouse_policy.h>
#include <toucan/mouse_window.h>
#include <toucan/diagnostics.h>

LOG_MODULE_REGISTER(toucan_ble_mouse, CONFIG_ZMK_LOG_LEVEL);
BUILD_ASSERT(CONFIG_BT_PERIPHERAL_PREF_MAX_INT == TOUCAN_MOUSE_CONN_INTERVAL);
BUILD_ASSERT(CONFIG_BT_PERIPHERAL_PREF_LATENCY == 0);
BUILD_ASSERT(CONFIG_ZMK_DISPLAY_DEDICATED_THREAD_PRIORITY > CONFIG_ZMK_BLE_THREAD_PRIORITY);

/* Pinned to ZMK v0.3: this is the same mouse report attribute used by hog.c.
 * Reuse the existing encrypted HID service and report descriptor. */
extern const struct bt_gatt_service_static hog_svc;

struct peer_queue {
    struct bt_conn *conn;
    struct toucan_mouse_queue reports;
    int64_t parameter_retry_ms;
};
static struct peer_queue peers[CONFIG_BT_MAX_CONN];
static struct k_spinlock queue_lock;
static struct toucan_mouse_window transmit_window;
static uint32_t flight_started[TOUCAN_MOUSE_IN_FLIGHT];
K_SEM_DEFINE(mouse_pending, 0, 1);

static uint8_t flights_used(void) {
    uint8_t count = 0;
    for (unsigned int i = 0; i < TOUCAN_MOUSE_IN_FLIGHT; i++) {
        count += transmit_window.slots[i].token != 0;
    }
    return count;
}
static uint32_t motion_distance(const struct toucan_mouse_packet *p) {
    return (uint32_t)abs((int)p->x) + (uint32_t)abs((int)p->y);
}

/* Called by the normal input listener. Unlike stock hog.c, no 100 ms queue
 * wait and no recursive overflow retry can stall the input thread here. */
int __wrap_zmk_hog_send_mouse_report(struct zmk_hid_mouse_report_body *report) {
    struct bt_conn *conn = zmk_ble_active_profile_conn();
    if (!conn) {
        return -ENOTCONN;
    }
    uint8_t index = bt_conn_index(conn);
    struct toucan_mouse_packet p = {
        .buttons = report->buttons, .x = report->d_x, .y = report->d_y,
        .wheel = report->d_scroll_y, .pan = report->d_scroll_x,
        .motion_ms = k_uptime_get(),
    };
    k_spinlock_key_t key = k_spin_lock(&queue_lock);
    struct peer_queue *peer = &peers[index];
    if (!peer->conn) {
        peer->conn = bt_conn_ref(conn);
    }
    toucan_mouse_push(&peer->reports, p, p.motion_ms);
    toucan_diag_hid(motion_distance(&p), peer->reports.count);
    k_spin_unlock(&queue_lock, key);
    bt_conn_unref(conn);
    k_sem_give(&mouse_pending);
    return 0;
}

static void mouse_sent(struct bt_conn *conn, void *user_data) {
    ARG_UNUSED(conn);
    k_spinlock_key_t key = k_spin_lock(&queue_lock);
    bool completed = toucan_mouse_window_complete(&transmit_window, (uint32_t)(uintptr_t)user_data);
    if (completed) {
        unsigned int index = (uint32_t)(uintptr_t)user_data & 1U;
        toucan_diag_complete(k_uptime_get_32() - flight_started[index], flights_used());
    }
    k_spin_unlock(&queue_lock, key);
    if (completed) {
        k_sem_give(&mouse_pending);
    }
}

static bool connected(struct bt_conn *conn) {
    struct bt_conn_info info;
    return bt_conn_get_info(conn, &info) == 0 && info.state == BT_CONN_STATE_CONNECTED;
}

/* Preferred parameters alone do not prove what the host negotiated. Inspect
 * the live host connection, then request a supported 15 ms / zero-latency
 * update when needed. Run outside the input thread and limit retries. */
static void mouse_request_parameters(struct bt_conn *conn) {
    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info) || info.state != BT_CONN_STATE_CONNECTED ||
        info.role != BT_CONN_ROLE_PERIPHERAL ||
        !toucan_mouse_link_needs_update(info.le.interval, info.le.latency)) {
        return;
    }
    int64_t now = k_uptime_get();
    k_spinlock_key_t key = k_spin_lock(&queue_lock);
    struct peer_queue *peer = &peers[bt_conn_index(conn)];
    bool request = peer->conn == conn && now >= peer->parameter_retry_ms;
    if (request) {
        peer->parameter_retry_ms = now + TOUCAN_MOUSE_PARAM_RETRY_MS;
    }
    k_spin_unlock(&queue_lock, key);
    if (!request) { return; }
    const struct bt_le_conn_param params = {
        .interval_min = TOUCAN_MOUSE_CONN_INTERVAL,
        .interval_max = TOUCAN_MOUSE_CONN_INTERVAL,
        .latency = 0,
        .timeout = CONFIG_BT_PERIPHERAL_PREF_TIMEOUT,
    };
    int err = bt_conn_le_param_update(conn, &params);
    LOG_INF("Host mouse link interval=%u latency=%u, 15ms/L0 request=%d",
            info.le.interval, info.le.latency, err);
}

static void mouse_disconnected(struct bt_conn *conn, uint8_t reason) {
    ARG_UNUSED(reason);
    struct bt_conn *release = NULL;
    k_spinlock_key_t key = k_spin_lock(&queue_lock);
    struct peer_queue *peer = &peers[bt_conn_index(conn)];
    if (peer->conn == conn) {
        release = peer->conn;
        *peer = (struct peer_queue){0};
    }
    toucan_mouse_window_disconnect(&transmit_window, (uintptr_t)conn);
    k_spin_unlock(&queue_lock, key);
    if (release) {
        bt_conn_unref(release);
    }
    k_sem_give(&mouse_pending);
}

BT_CONN_CB_DEFINE(toucan_mouse_conn_callbacks) = {.disconnected = mouse_disconnected};

static void mouse_sender(void *a, void *b, void *c) {
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    unsigned int next_peer = 0;
    for (;;) {
        struct toucan_mouse_packet p;
        struct bt_conn *conn = NULL;
        uint32_t tag = 0;
        k_spinlock_key_t key = k_spin_lock(&queue_lock);
        for (unsigned int n = 0; n < ARRAY_SIZE(peers) &&
             toucan_mouse_window_available(&transmit_window); n++) {
            unsigned int index = (next_peer + n) % ARRAY_SIZE(peers);
            if (peers[index].conn &&
                toucan_mouse_pop(&peers[index].reports, &p, k_uptime_get())) {
                conn = bt_conn_ref(peers[index].conn);
                tag = toucan_mouse_window_acquire(&transmit_window, (uintptr_t)conn);
                flight_started[tag & 1U] = k_uptime_get_32();
                toucan_diag_tx_start((uint32_t)(k_uptime_get() - p.motion_ms),
                                     peers[index].reports.count, flights_used());
                next_peer = (index + 1U) % ARRAY_SIZE(peers);
                break;
            }
        }
        k_spin_unlock(&queue_lock, key);
        if (!conn) {
            k_sem_take(&mouse_pending, K_FOREVER);
            continue;
        }
        if (!connected(conn)) {
            mouse_disconnected(conn, 0);
            bt_conn_unref(conn);
            continue;
        }
        mouse_request_parameters(conn);
        toucan_mouse_expire(&p, k_uptime_get());
        struct zmk_hid_mouse_report_body report = {
            .buttons = p.buttons, .d_x = p.x, .d_y = p.y,
            .d_scroll_y = p.wheel, .d_scroll_x = p.pan,
        };
        struct bt_gatt_notify_params params = {
            .attr = &hog_svc.attrs[13], .data = &report, .len = sizeof(report),
            .func = mouse_sent, .user_data = (void *)(uintptr_t)tag,
        };
        /* Zephyr v3.5 may wait for a Bluetooth buffer here, even on its system
         * queue. Isolate that wait on this thread, away from input, key HID,
         * sensor, layer recovery and display processing. */
        uint32_t notify_started = k_uptime_get_32();
        int err = bt_gatt_notify_cb(conn, &params);
        toucan_diag_tx_result(k_uptime_get_32() - notify_started, err, motion_distance(&p));
        /* Keep a second notification ready while the preceding completion
         * callback is pending. Stop-and-wait can leave the next event empty;
         * host arrivals measured 30 ms on a 15 ms link. Never exceed two in flight. */
        if (err) {
            key = k_spin_lock(&queue_lock);
            toucan_mouse_window_complete(&transmit_window, tag);
            k_spin_unlock(&queue_lock, key);
        }
        if (err == -ENOMEM || err == -EAGAIN || err == -EPERM) {
            if (err == -EPERM) {
                bt_conn_set_security(conn, BT_SECURITY_L2);
            }
            key = k_spin_lock(&queue_lock);
            struct peer_queue *peer = &peers[bt_conn_index(conn)];
            if (peer->conn == conn) {
                toucan_mouse_retry(&peer->reports, p, k_uptime_get());
            }
            k_spin_unlock(&queue_lock, key);
            k_sleep(K_MSEC(err == -EPERM ? 20 : 5));
        }
        if (!connected(conn)) {
            mouse_disconnected(conn, 0);
        }
        bt_conn_unref(conn);
    }
}

K_THREAD_DEFINE(toucan_mouse_sender, 2048, mouse_sender, NULL, NULL, NULL,
                CONFIG_ZMK_BLE_THREAD_PRIORITY, 0, 0);
