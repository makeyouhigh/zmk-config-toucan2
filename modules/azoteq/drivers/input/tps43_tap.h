/* SPDX-License-Identifier: MIT */
#pragma once

#include "tps43_force.h"

/* Software single-tap recognition never delays or consumes cursor motion.
 * The chip's single-tap recognizer suppresses relative XY while it decides. */
struct tps43_tap_state {
    bool active;
    bool blocked;
    int64_t started_ms;
    int64_t last_sample_ms;
    uint16_t x;
    uint16_t y;
};

static inline void tps43_tap_cancel(struct tps43_tap_state *state) {
    state->active = false;
    state->blocked = true;
}

static inline bool tps43_tap_step(struct tps43_tap_state *state, int64_t now_ms,
                                  uint8_t fingers, bool valid, bool force_consumed,
                                  uint16_t x, uint16_t y, uint16_t max_ms,
                                  uint16_t max_distance) {
    if (!valid || fingers > 1 || force_consumed ||
        (state->active && now_ms - state->last_sample_ms > TPS43_FORCE_STALE_MS)) {
        tps43_tap_cancel(state);
    }
    if (fingers == 0) {
        bool tap = state->active && !state->blocked &&
                   now_ms - state->started_ms <= max_ms;
        *state = (struct tps43_tap_state){0};
        return tap;
    }
    if (state->blocked) {
        return false;
    }
    if (!state->active) {
        state->active = true;
        state->started_ms = now_ms;
        state->x = x;
        state->y = y;
    }
    state->last_sample_ms = now_ms;
    if (now_ms - state->started_ms > max_ms ||
        tps43_force_moved(x, y, state->x, state->y, max_distance)) {
        tps43_tap_cancel(state);
    }
    return false;
}
