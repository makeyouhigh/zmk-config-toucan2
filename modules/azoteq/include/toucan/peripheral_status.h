/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* One display-only packet on battery/USB/link changes. Never a mouse axis. */
#define TOUCAN_INPUT_PERIPHERAL_STATUS_CODE 0x33
#define TOUCAN_PERIPHERAL_STATUS_TAG (1U << 16)
struct toucan_peripheral_status {
    uint8_t battery;
    bool battery_known;
    bool usb_powered;
};
static inline int32_t toucan_peripheral_status_pack(uint8_t battery, bool known, bool powered) {
    known = known && battery <= 100;
    return TOUCAN_PERIPHERAL_STATUS_TAG | (known ? battery | (1U << 8) : 0) |
           (powered ? (1U << 9) : 0);
}
static inline bool toucan_peripheral_status_decode(int32_t value,
                                                   struct toucan_peripheral_status *out) {
    uint32_t v = (uint32_t)value;
    if (!(v & TOUCAN_PERIPHERAL_STATUS_TAG) || (v & ~0x1037fU) ||
        (v & 127U) > 100 || (!(v & (1U << 8)) && (v & 127U))) return false;
    *out = (struct toucan_peripheral_status){
        .battery = v & 127U, .battery_known = (v & (1U << 8)) != 0,
        .usb_powered = (v & (1U << 9)) != 0,
    };
    return true;
}
