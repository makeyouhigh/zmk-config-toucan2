/* SPDX-License-Identifier: MIT */
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <toucan/packed_xy.h>

int __real_zmk_input_split_report_peripheral_event(uint8_t reg, uint8_t type, uint16_t code,
                                                    int32_t value, bool sync);

/* Decode before the stock input queue/listeners so all layer processors, axis
 * scalers, USB/BLE routing and button recovery keep their original behavior. */
int __wrap_zmk_input_split_report_peripheral_event(uint8_t reg, uint8_t type, uint16_t code,
                                                    int32_t value, bool sync) {
    if (type != INPUT_EV_ABS || code != TOUCAN_INPUT_PACKED_XY_CODE) {
        return __real_zmk_input_split_report_peripheral_event(reg, type, code, value, sync);
    }
    int16_t x = toucan_unpack_x(value);
    int16_t y = toucan_unpack_y(value);
    if (x != 0) {
        int ret = __real_zmk_input_split_report_peripheral_event(reg, INPUT_EV_REL,
                                                                 INPUT_REL_X, x, y == 0);
        if (ret < 0) {
            return ret;
        }
    }
    if (y != 0) {
        return __real_zmk_input_split_report_peripheral_event(reg, INPUT_EV_REL,
                                                              INPUT_REL_Y, y, true);
    }
    return 0;
}
