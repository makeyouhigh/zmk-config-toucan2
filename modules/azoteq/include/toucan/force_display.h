/* SPDX-License-Identifier: MIT */
#pragma once

/* INPUT_ABS_PRESSURE carries this three-state display value over input-split.
 * It is not a raw sensor value, a cursor axis, or a calibrated pressure unit. */
enum toucan_touch_display_state {
    TOUCAN_TOUCH_NONE = 0,
    TOUCAN_TOUCH_CONTACT = 1,
    TOUCAN_TOUCH_PRESSED = 2,
};
