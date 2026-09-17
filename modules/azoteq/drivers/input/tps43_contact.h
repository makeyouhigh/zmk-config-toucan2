/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* IQS5xx keeps finger identities in fixed slots. Slot zero can be empty
 * while another finger is still touching, especially during a staggered lift. */
static inline bool tps43_contact_valid(uint8_t status, uint8_t fingers) {
    return !(status & 0x06U) && fingers <= 5;
}
static inline bool tps43_first_contact_valid(uint8_t status, uint8_t fingers,
                                             uint8_t first_area) {
    return tps43_contact_valid(status, fingers) && (fingers == 0 || first_area != 0);
}
