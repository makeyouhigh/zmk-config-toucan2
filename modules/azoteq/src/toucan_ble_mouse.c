/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/atomic.h>
#include <zmk/ble.h>
#include <zmk/hid.h>
#include <toucan/mouse_queue.h>

/* Pinned to ZMK v0.3: this is the same mouse report attribute used by hog.c.
 * Reuse the existing encrypted HID service and report descriptor. */
extern const struct bt_gatt_service_static hog_svc;

struct peer_queue {
    struct bt_conn *conn;
    struct toucan_mouse_queue reports;
};
static struct peer_queue peers[CONFIG_BT_MAX_CONN];
static struct k_spinlock queue_lock;
static atomic_t awaiting_tag;
K_SEM_DEFINE(mouse_pending, 0, 1);
K_SEM_DEFINE(mouse_completed, 0, 1);

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
    k_spin_unlock(&queue_lock, key);
    bt_conn_unref(conn);
    k_sem_give(&mouse_pending);
    return 0;
}

static void mouse_sent(struct bt_conn *conn, void *user_data) {
    ARG_UNUSED(conn);
    if (atomic_cas(&awaiting_tag, (atomic_val_t)(uintptr_t)user_data, 0)) {
        k_sem_give(&mouse_completed);
    }
}

static bool connected(struct bt_conn *conn) {
    struct bt_conn_info info;
    return bt_conn_get_info(conn, &info) == 0 && info.state == BT_CONN_STATE_CONNECTED;
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
    k_spin_unlock(&queue_lock, key);
    if (release) {
        bt_conn_unref(release);
    }
    k_sem_give(&mouse_completed);
}

BT_CONN_CB_DEFINE(toucan_mouse_conn_callbacks) = {.disconnected = mouse_disconnected};

static void mouse_sender(void *a, void *b, void *c) {
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    uint32_t tag = 0;
    unsigned int next_peer = 0;
    for (;;) {
        struct toucan_mouse_packet p;
        struct bt_conn *conn = NULL;
        k_spinlock_key_t key = k_spin_lock(&queue_lock);
        for (unsigned int n = 0; n < ARRAY_SIZE(peers); n++) {
            unsigned int index = (next_peer + n) % ARRAY_SIZE(peers);
            if (peers[index].conn &&
                toucan_mouse_pop(&peers[index].reports, &p, k_uptime_get())) {
                conn = bt_conn_ref(peers[index].conn);
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
        struct zmk_hid_mouse_report_body report = {
            .buttons = p.buttons, .d_x = p.x, .d_y = p.y,
            .d_scroll_y = p.wheel, .d_scroll_x = p.pan,
        };
        tag = (tag + 1U) & INT32_MAX;
        if (!tag) { tag = 1; }
        atomic_set(&awaiting_tag, tag);
        struct bt_gatt_notify_params params = {
            .attr = &hog_svc.attrs[13], .data = &report, .len = sizeof(report),
            .func = mouse_sent, .user_data = (void *)(uintptr_t)tag,
        };
        /* Zephyr v3.5 may wait for a Bluetooth buffer here, even on its system
         * queue. Isolate that wait on this thread, away from input, key HID,
         * sensor, layer recovery and display processing. */
        int err = bt_gatt_notify_cb(conn, &params);
        if (!err) {
            /* At most one mouse notification is in the Bluetooth stack.
             * New movement merges while it is in flight; edges stay ordered. */
            while ((uint32_t)atomic_get(&awaiting_tag) == tag && connected(conn)) {
                k_sem_take(&mouse_completed, K_MSEC(25));
            }
        } else if (err == -ENOMEM || err == -EAGAIN || err == -EPERM) {
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
