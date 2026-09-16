/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Shared by the device driver and the host-side behavioural tests. */
#define TPS43_FORCE_STALE_MS 250

enum tps43_force_event {
    TPS43_FORCE_RELEASE = -1,
    TPS43_FORCE_NONE = 0,
    TPS43_FORCE_PRESS = 1,
};

struct tps43_force_config {
    uint16_t press_delta;
    uint16_t release_delta;
    uint16_t baseline_ms;
    uint16_t debounce_ms;
};

struct tps43_force_state {
    bool active;
    bool ready;
    bool blocked;
    bool down;
    bool tap_consumed;
    bool candidate;
    int64_t started_ms;
    int64_t last_sample_ms;
    int64_t candidate_ms;
    uint64_t strength_sum;
    uint32_t samples;
    uint16_t baseline;
};

static inline enum tps43_force_event
tps43_force_cancel(struct tps43_force_state *state, bool block_until_lift) {
    bool was_down = state->down;
    bool consumed = state->tap_consumed || was_down || block_until_lift;
    /* Keep this through the lift frame, where the chip reports SINGLE_TAP. */
    *state = (struct tps43_force_state){.blocked = block_until_lift,
                                      .tap_consumed = consumed};
    return was_down ? TPS43_FORCE_RELEASE : TPS43_FORCE_NONE;
}

static inline enum tps43_force_event
tps43_force_step(struct tps43_force_state *state,
                 const struct tps43_force_config *config, int64_t now_ms,
                 uint8_t fingers, uint16_t strength, bool valid) {
    /* Never calibrate on a palm, a multi-finger gesture or an invalid frame. */
    if (!valid) {
        return tps43_force_cancel(state, true);
    }
    if (fingers == 0) {
        return tps43_force_cancel(state, false);
    }
    if (fingers != 1 || strength == 0) {
        return tps43_force_cancel(state, true);
    }
    if (state->blocked) {
        return TPS43_FORCE_NONE;
    }
    if (state->active && now_ms - state->last_sample_ms > TPS43_FORCE_STALE_MS) {
        return tps43_force_cancel(state, true);
    }
    state->last_sample_ms = now_ms;
    if (!state->active) {
        state->active = true;
        state->tap_consumed = false;
        state->started_ms = now_ms;
    }
    if (!state->ready) {
        state->strength_sum += strength;
        state->samples++;
        state->baseline = (uint16_t)(state->strength_sum / state->samples);
        if (now_ms - state->started_ms >= config->baseline_ms && state->samples >= 2) {
            state->ready = true;
        }
        return TPS43_FORCE_NONE;
    }

    /* Freeze the baseline for the contact: a held click must not fade out. */
    int32_t delta = (int32_t)strength - (int32_t)state->baseline;
    bool next_down = state->down ? delta > config->release_delta
                                 : delta >= config->press_delta;
    if (next_down == state->down) {
        state->candidate = false;
        return TPS43_FORCE_NONE;
    }
    if (!state->candidate) {
        state->candidate = true;
        state->candidate_ms = now_ms;
    }
    if (now_ms - state->candidate_ms < config->debounce_ms) {
        return TPS43_FORCE_NONE;
    }
    state->candidate = false;
    state->down = next_down;
    if (next_down) {
        state->tap_consumed = true;
    }
    return next_down ? TPS43_FORCE_PRESS : TPS43_FORCE_RELEASE;
}
