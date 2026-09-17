/* SPDX-License-Identifier: MIT */
#pragma once
#include "tps43_force.h"
struct tps43_three_tap_state {
    bool active, blocked, claimed, position_valid;
    uint8_t fingers;
    uint16_t x, y;
    int64_t started_ms, last_sample_ms;
};
struct tps43_three_tap_result { bool click, claimed; };
/* The force lift frame preserves tap_consumed to suppress a same-frame tap.
 * That marker does not belong to the next contact. */
static inline bool tps43_three_tap_consumed(const struct tps43_force_state *force) {
    return force->active && (force->down || force->tap_consumed);
}
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
    /* Finger slots retain identity. The first slot may disappear before the
     * remaining fingers lift; 0xffff then means no coordinate, not a swipe.
     * Still enforce contact validity, finger count and the total tap timeout. */
    bool position_valid = x != UINT16_MAX && y != UINT16_MAX;
    if (position_valid && (fingers != s->fingers || !s->position_valid)) {
        s->x = x; s->y = y;
    } else if (position_valid && tps43_force_moved(x, y, s->x, s->y, max_distance)) {
        s->blocked = true;
    }
    if (now - s->started_ms > max_ms) { s->blocked = true; }
    s->fingers = fingers;
    s->position_valid = position_valid;
    s->last_sample_ms = now;
    return (struct tps43_three_tap_result){.claimed = s->claimed};
}
static inline bool tps43_two_finger_motion(uint8_t fingers, bool three_claimed) {
    return fingers == 2 && !three_claimed;
}
