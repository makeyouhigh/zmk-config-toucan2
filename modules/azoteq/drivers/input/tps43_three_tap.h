/* SPDX-License-Identifier: MIT */
#pragma once
#include "tps43_force.h"
struct tps43_three_tap_state {
    bool active, blocked, claimed;
    uint8_t fingers;
    uint16_t x, y;
    int64_t started_ms, last_sample_ms;
};
struct tps43_three_tap_result { bool click, claimed; };
static inline void tps43_three_tap_cancel(struct tps43_three_tap_state *s) {
    s->blocked = true;
}
/* Claim the contact once three fingers appear. Count changes can shift the
 * centroid without finger travel, so reanchor on uneven landing/lifting. */
static inline struct tps43_three_tap_result
tps43_three_tap_step(struct tps43_three_tap_state *s, int64_t now, uint8_t fingers,
                     bool valid, bool consumed, uint16_t x, uint16_t y,
                     uint16_t max_ms, uint16_t max_distance) {
    if (!valid || consumed || fingers > 3 ||
        (s->active && now - s->last_sample_ms > TPS43_FORCE_STALE_MS)) {
        s->blocked = true;
    }
    if (fingers == 0) {
        struct tps43_three_tap_result result = {
            .claimed = s->claimed,
            .click = s->active && s->claimed && !s->blocked &&
                     now - s->started_ms <= max_ms,
        };
        *s = (struct tps43_three_tap_state){0};
        return result;
    }
    if (!s->active) {
        s->active = true;
        s->started_ms = now;
        s->x = x; s->y = y;
    }
    if (fingers >= 3) { s->claimed = true; }
    if (fingers != s->fingers) {
        s->x = x; s->y = y;
    } else if (tps43_force_moved(x, y, s->x, s->y, max_distance)) {
        s->blocked = true;
    }
    if (now - s->started_ms > max_ms) { s->blocked = true; }
    s->fingers = fingers;
    s->last_sample_ms = now;
    return (struct tps43_three_tap_result){.claimed = s->claimed};
}
static inline bool tps43_two_finger_motion(uint8_t fingers, bool three_claimed) {
    return fingers == 2 && !three_claimed;
}
