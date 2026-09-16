/* SPDX-License-Identifier: MIT */
#pragma once

/* Private absolute-event code shared by the sensor and LCD. This Zephyr
 * version does not name pressure code 0x18 in its input event headers.
 * It is not a raw sensor value, a cursor axis, or a calibrated pressure unit. */
#define TOUCAN_INPUT_TOUCH_STATE_CODE 0x18

enum toucan_touch_display_state {
    TOUCAN_TOUCH_NONE = 0,
    TOUCAN_TOUCH_CONTACT = 1,
    TOUCAN_TOUCH_PRESSED = 2,
};
