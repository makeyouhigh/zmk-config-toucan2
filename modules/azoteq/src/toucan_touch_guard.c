/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT toucan_touch_guard
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zmk/keymap.h>
#include <toucan/touch_lease.h>
#include <toucan/diagnostics.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define TRACKPAD DEVICE_DT_GET(DT_INST_PHANDLE(0, device))
#define TOUCH_LAYER DT_INST_PROP(0, layer)
#define TOUCH_TIMEOUT_MS DT_INST_PROP(0, timeout_ms)

static struct toucan_touch_lease lease;
static struct k_spinlock lease_lock;
/* Only the work handler changes layer ownership. */
static bool layer_owned;
static void touch_guard_work_cb(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(touch_guard_work, touch_guard_work_cb);

static void touch_guard_work_cb(struct k_work *work) {
    int64_t now = k_uptime_get();
    k_spinlock_key_t key = k_spin_lock(&lease_lock);
    toucan_touch_lease_expire(&lease, now, TOUCH_TIMEOUT_MS);
    bool touching = lease.state != TOUCAN_TOUCH_NONE;
    bool release = lease.release_pending && lease.left_down;
    bool monitor = touching || lease.left_down;
    int64_t remaining = MAX(1, TOUCH_TIMEOUT_MS - (now - lease.updated_ms));
    k_spin_unlock(&lease_lock, key);

    if (touching && !layer_owned && !zmk_keymap_layer_active(TOUCH_LAYER)) {
        zmk_keymap_layer_activate(TOUCH_LAYER);
        layer_owned = zmk_keymap_layer_active(TOUCH_LAYER);
    } else if (!touching && layer_owned) {
        zmk_keymap_layer_deactivate(TOUCH_LAYER);
        layer_owned = false;
    }

    if (release) {
        /* Re-enter the input queue so mouse state still changes on its normal
         * input thread. Retry if full; never block the system workqueue. */
        input_report_key(TRACKPAD, INPUT_BTN_0, 0, true, K_NO_WAIT);
    }
    if (monitor) {
        /* schedule preserves any immediate update already queued by a new input. */
        k_work_schedule(&touch_guard_work, K_MSEC(release ? 20 : remaining));
    }
}

static void touch_guard_input(struct input_event *event) {
#if defined(CONFIG_ZMK_BLE) && defined(CONFIG_ZMK_POINTING)
    toucan_diag_input(event);
#endif
    int64_t now = k_uptime_get();
    bool handled = true;
    k_spinlock_key_t key = k_spin_lock(&lease_lock);
    if (event->type == INPUT_EV_ABS && event->code == TOUCAN_INPUT_TOUCH_STATE_CODE) {
        toucan_touch_lease_update(&lease, now,
            CLAMP(event->value, TOUCAN_TOUCH_NONE, TOUCAN_TOUCH_PRESSED));
    } else if (event->type == INPUT_EV_KEY && event->code == INPUT_BTN_TOUCH) {
        toucan_touch_lease_update(&lease, now,
            event->value ? MAX(lease.state, TOUCAN_TOUCH_CONTACT) : TOUCAN_TOUCH_NONE);
    } else if (event->type == INPUT_EV_KEY && event->code == INPUT_BTN_0) {
        toucan_touch_lease_button(&lease, now, event->value != 0);
    } else {
        handled = false;
    }
    k_spin_unlock(&lease_lock, key);
    if (handled) {
        k_work_reschedule(&touch_guard_work, K_NO_WAIT);
    }
}

/* Independent of layer-specific pointer processors: release still works when
 * NAV/NUM scrolling overrides are active or a split notification was lost. */
INPUT_CALLBACK_DEFINE(TRACKPAD, touch_guard_input);

#endif
