/* SPDX-License-Identifier: MIT */
#pragma once
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <toucan/force_display.h>
#include <toucan/force_status.h>
#include <toucan/peripheral_status.h>

/* Display telemetry must not wake the keyboard or restart its sleep timer.
 * Contact edges, mouse motion/buttons and all other inputs remain activity. */
static inline bool toucan_input_is_user_activity(uint8_t type, uint16_t code) {
    return type != INPUT_EV_ABS ||
        (code != TOUCAN_INPUT_TOUCH_STATE_CODE &&
         code != TOUCAN_INPUT_PERIPHERAL_STATUS_CODE &&
         !(code >= TOUCAN_FORCE_STATUS_CODE &&
           code < TOUCAN_FORCE_STATUS_CODE + TOUCAN_FORCE_STATUS_PARTS));
}
