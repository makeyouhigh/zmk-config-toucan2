/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Private display packet, never a mouse axis. v19 includes the current
 * Touch Strength in the existing state heartbeat; no extra input reports. */
#define TOUCAN_INPUT_TOUCH_STATE_CODE 0x18
#define TOUCAN_FORCE_DISPLAY_INTERVAL_MS 250
#define TOUCAN_FORCE_DISPLAY_STALE_MS 750
#define TOUCAN_FORCE_DISPLAY_PACKED (1U << 18)
#define TOUCAN_FORCE_DISPLAY_KNOWN (1U << 19)

enum toucan_touch_display_state {
    TOUCAN_TOUCH_NONE = 0,
    TOUCAN_TOUCH_CONTACT = 1,
    TOUCAN_TOUCH_PRESSED = 2,
};

struct toucan_force_display_sample {
    uint16_t strength;
    uint8_t state;
    bool known;
};

static inline int32_t toucan_force_display_pack(uint8_t state, uint16_t strength, bool known) {
    if (state == TOUCAN_TOUCH_NONE || !known) strength = 0;
    return TOUCAN_FORCE_DISPLAY_PACKED | (known ? TOUCAN_FORCE_DISPLAY_KNOWN : 0) |
        ((uint32_t)strength << 2) | state;
}

static inline bool toucan_force_display_decode(int32_t value,
                                               struct toucan_force_display_sample *out) {
    /* A new left half can still handle a v17/v18 right half's state-only packet. */
    if (value >= 0 && value <= TOUCAN_TOUCH_PRESSED) {
        *out = (struct toucan_force_display_sample){.state = value};
        return true;
    }
    uint32_t v = (uint32_t)value;
    if (value < 0 || (v & ~0xfffffU) || !(v & TOUCAN_FORCE_DISPLAY_PACKED) ||
        (v & 3U) > TOUCAN_TOUCH_PRESSED) return false;
    *out = (struct toucan_force_display_sample){.strength = (v >> 2) & 0xffffU,
        .state = v & 3U, .known = (v & TOUCAN_FORCE_DISPLAY_KNOWN) != 0};
    if (!out->known || out->state == TOUCAN_TOUCH_NONE) out->strength = 0;
    return true;
}

/* Four character cells, with no truncation or misleading wrap above 9999. */
static inline void toucan_force_display_text(char out[5], uint16_t strength, bool known) {
    if (!known || strength > 9999) {
        const char *text = known ? "OVER" : "----";
        for (unsigned i = 0; i < 4; i++) out[i] = text[i];
    } else {
        for (int i = 3; i >= 0; i--) { out[i] = '0' + strength % 10; strength /= 10; }
    }
    out[4] = '\0';
}
