/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Bluetooth connection interval units are 1.25 ms. */
#define TOUCAN_MOUSE_CONN_INTERVAL 12
#define TOUCAN_MOUSE_PARAM_RETRY_MS 15000

static inline bool toucan_mouse_link_needs_update(uint16_t interval, uint16_t latency) {
    return interval > TOUCAN_MOUSE_CONN_INTERVAL || latency != 0;
}
