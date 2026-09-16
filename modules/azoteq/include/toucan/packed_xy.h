/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>

/* Private split transport code. Both halves must have the decoder/encoder. */
#define TOUCAN_INPUT_PACKED_XY_CODE 0x19

static inline int32_t toucan_pack_xy(int16_t x, int16_t y) {
    uint32_t bits = ((uint32_t)(uint16_t)x << 16) | (uint16_t)y;
    return bits <= INT32_MAX ? (int32_t)bits : (int32_t)((int64_t)bits - 4294967296LL);
}

static inline int16_t toucan_unpack_axis(uint16_t bits) {
    return bits <= INT16_MAX ? (int16_t)bits : (int16_t)((int32_t)bits - 65536);
}

static inline int16_t toucan_unpack_x(int32_t packed) {
    return toucan_unpack_axis((uint32_t)packed >> 16);
}

static inline int16_t toucan_unpack_y(int32_t packed) {
    return toucan_unpack_axis((uint16_t)packed);
}
