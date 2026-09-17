/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/sys/byteorder.h>
#include <toucan/diagnostics.h>
#include <toucan/force_display.h>

BUILD_ASSERT(TD_SOURCE_DROPS + 1 == TOUCAN_DIAG_WORDS);
static struct k_spinlock lock;
static uint32_t values[TOUCAN_DIAG_WORDS];
static struct toucan_diag_source source;
static uint8_t read_cache[CONFIG_BT_MAX_CONN][TOUCAN_DIAG_WORDS * 4];

void toucan_diag_input(const struct input_event *e) {
    uint32_t now = k_uptime_get_32();
    k_spinlock_key_t key = k_spin_lock(&lock);
    if (e->type == INPUT_EV_REL &&
        (e->code == INPUT_REL_X || e->code == INPUT_REL_Y) && e->value) {
        values[TD_INPUT_EVENTS]++;
        values[TD_INPUT_DISTANCE] += e->value < 0 ? -(int64_t)e->value : e->value;
        values[TD_INPUT_MS] = now;
    } else if (e->type == INPUT_EV_KEY && e->code == INPUT_BTN_0) {
        values[TD_BUTTON_STATE] = e->value != 0;
    } else if (e->type == INPUT_EV_ABS && e->code == TOUCAN_INPUT_TOUCH_STATE_CODE) {
        values[TD_TOUCH_STATE] = e->value;
    } else if (e->type == INPUT_EV_ABS && e->code >= TOUCAN_DIAG_CODE_BASE &&
               e->code <= TOUCAN_DIAG_CODE_END) {
        if (toucan_diag_source_step(&source, e->code, (uint32_t)e->value)) {
            memcpy(&values[TD_SOURCE_BASE], source.values, sizeof(source.values));
            values[TD_SOURCE_SEQUENCE] = source.sequence;
            values[TD_SOURCE_RECEIVED_MS] = now;
        }
        values[TD_SOURCE_DROPS] = source.drops;
    }
    k_spin_unlock(&lock, key);
}
void toucan_diag_hid(uint32_t distance, uint8_t pending) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    values[TD_HID_REPORTS]++;
    values[TD_HID_DISTANCE] += distance;
    values[TD_QUEUE] = pending;
    values[TD_MAX_QUEUE] = MAX(values[TD_MAX_QUEUE], pending);
    k_spin_unlock(&lock, key);
}
void toucan_diag_tx_start(uint32_t age, uint8_t pending, uint8_t in_flight) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    values[TD_MAX_QUEUE_AGE_MS] = MAX(values[TD_MAX_QUEUE_AGE_MS], age);
    values[TD_QUEUE] = pending;
    values[TD_IN_FLIGHT] = in_flight;
    k_spin_unlock(&lock, key);
}
void toucan_diag_tx_result(uint32_t duration, int error, uint32_t distance) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    values[TD_MAX_NOTIFY_MS] = MAX(values[TD_MAX_NOTIFY_MS], duration);
    values[TD_LAST_ERROR] = (uint32_t)error;
    if (!error) {
        values[TD_SUBMITTED]++;
        values[TD_SUBMITTED_DISTANCE] += distance;
        values[TD_SUBMITTED_MS] = k_uptime_get_32();
    } else if (error == -ENOMEM || error == -EAGAIN || error == -EPERM) {
        values[TD_RETRIES]++;
    } else {
        values[TD_ERRORS]++;
    }
    k_spin_unlock(&lock, key);
}
void toucan_diag_complete(uint32_t age, uint8_t in_flight) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    values[TD_COMPLETED]++;
    values[TD_COMPLETED_MS] = k_uptime_get_32();
    values[TD_MAX_CALLBACK_MS] = MAX(values[TD_MAX_CALLBACK_MS], age);
    values[TD_IN_FLIGHT] = in_flight;
    k_spin_unlock(&lock, key);
}

/* Explicit encrypted reads only: no notifications, timers, subscriptions,
 * flash writes, or host-setting changes. A long read sees one frozen snapshot. */
static ssize_t read_diagnostics(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                void *buf, uint16_t len, uint16_t offset) {
    uint8_t index = bt_conn_index(conn);
    if (offset == 0) {
        struct bt_conn_info info;
        bool connected = bt_conn_get_info(conn, &info) == 0 && info.type == BT_CONN_TYPE_LE;
        k_spinlock_key_t key = k_spin_lock(&lock);
        values[TD_MAGIC] = 0x31444354; /* TCD1 */
        values[TD_VERSION] = 1;
        values[TD_NOW] = k_uptime_get_32();
        values[TD_HOST_INTERVAL] = connected ? info.le.interval : 0;
        values[TD_HOST_LATENCY] = connected ? info.le.latency : 0;
        for (unsigned int i = 0; i < TOUCAN_DIAG_WORDS; i++) {
            sys_put_le32(values[i], &read_cache[index][i * 4]);
        }
        k_spin_unlock(&lock, key);
    }
    return bt_gatt_attr_read(conn, attr, buf, len, offset,
                            read_cache[index], sizeof(read_cache[index]));
}
/* Static services are sorted by name in Zephyr. Append after existing services
 * so the established HID attribute handles do not move. */
BT_GATT_SERVICE_DEFINE(zz_toucan_diagnostics,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(
        0x0ed291b0, 0x9e1a, 0x4f35, 0xa02d, 0x75e470bf0601))),
    BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(
        0x0ed291b1, 0x9e1a, 0x4f35, 0xa02d, 0x75e470bf0601)),
        BT_GATT_CHRC_READ, BT_GATT_PERM_READ_ENCRYPT, read_diagnostics, NULL, NULL)
);
