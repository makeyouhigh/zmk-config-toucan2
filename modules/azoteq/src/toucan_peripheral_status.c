/* SPDX-License-Identifier: MIT */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/sys/atomic.h>
#include <zmk/battery.h>
#include <zmk/usb.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <toucan/peripheral_status.h>

static atomic_t attempts;
static atomic_t battery_sample_received;
static void publish_peripheral_status(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(peripheral_status_work, publish_peripheral_status);

static void publish_peripheral_status(struct k_work *work) {
    ARG_UNUSED(work);
    if (!zmk_split_bt_peripheral_is_connected()) return;
    uint8_t level = zmk_battery_state_of_charge();
    bool known = level <= 100 && (level > 0 || atomic_get(&battery_sample_received));
    int32_t packet = toucan_peripheral_status_pack(level, known, zmk_usb_is_powered());
    input_report_abs(DEVICE_DT_GET(DT_NODELABEL(tps43_trackpad)),
                     TOUCAN_INPUT_PERIPHERAL_STATUS_CODE, packet, false, K_NO_WAIT);
    /* Input notifications may not yet be subscribed when the link event fires.
     * Bound retries to three packets per change, including local queue failure. */
    if (atomic_inc(&attempts) < 2) {
        k_work_schedule(&peripheral_status_work, K_SECONDS(1));
    }
}

static int peripheral_status_event(const zmk_event_t *eh) {
    if (as_zmk_battery_state_changed(eh)) atomic_set(&battery_sample_received, 1);
    atomic_set(&attempts, 0);
    k_work_reschedule(&peripheral_status_work, K_MSEC(250));
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(toucan_peripheral_status, peripheral_status_event);
ZMK_SUBSCRIPTION(toucan_peripheral_status, zmk_battery_state_changed);
ZMK_SUBSCRIPTION(toucan_peripheral_status, zmk_usb_conn_state_changed);
ZMK_SUBSCRIPTION(toucan_peripheral_status, zmk_split_peripheral_status_changed);
