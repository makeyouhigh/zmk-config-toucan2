/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>

#define TOUCAN_INPUT_FORCE_VALUES_CODE 0x1a
#define TOUCAN_INPUT_FORCE_PEAK_CODE 0x1b
#define TOUCAN_FORCE_VALUES_INTERVAL_MS 200

static inline int32_t toucan_force_pair(uint16_t high, uint16_t low) {
    uint32_t bits = ((uint32_t)high << 16) | low;
    return bits <= INT32_MAX ? (int32_t)bits : -1 - (int32_t)(UINT32_MAX - bits);
}
static inline uint16_t toucan_force_pair_high(int32_t value) {
    return (uint16_t)((uint32_t)value >> 16);
}
static inline uint16_t toucan_force_pair_low(int32_t value) {
    return (uint16_t)value;
}
