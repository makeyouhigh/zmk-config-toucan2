/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <toucan/force_display.h>

/* Shared by the device driver and the host-side behavioural tests. */
#define TPS43_FORCE_STALE_MS 250
#define TPS43_FORCE_STOP_MS 16

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
    uint16_t press_percent;
    uint16_t release_percent;
    uint16_t motion_threshold;
    uint16_t drag_threshold;
    uint16_t settle_ms;
};

struct tps43_force_state {
    bool active;
    bool ready;
    bool blocked;
    bool down;
    bool tap_consumed;
    bool candidate;
    bool dragging;
    bool suppress_motion;
    bool previous_resting;
    int32_t previous_dx;
    int32_t previous_dy;
    int64_t started_ms;
    int64_t window_ms;
    int64_t last_sample_ms;
    int64_t candidate_ms;
    int64_t quiet_until_ms;
    int64_t motion_until_ms;
    int64_t motion_window_ms;
    uint64_t strength_sum;
    uint32_t samples;
    uint16_t baseline;
    int32_t baseline_q8;
    uint16_t strength_min;
    uint16_t strength_max;
    uint16_t anchor_x;
    uint16_t anchor_y;
    uint16_t candidate_x;
    uint16_t candidate_y;
    uint16_t drag_x;
    uint16_t drag_y;
    uint16_t previous_x;
    uint16_t previous_y;
    uint16_t motion_x;
    uint16_t motion_y;
};

static inline enum tps43_force_event
tps43_force_cancel(struct tps43_force_state *state, bool block_until_lift) {
    bool was_down = state->down;
    bool consumed = state->tap_consumed || was_down || block_until_lift;
    /* Keep this through the lift frame, where the chip reports SINGLE_TAP. */
    *state = (struct tps43_force_state){.blocked = block_until_lift,
                                      .tap_consumed = consumed,
                                      .suppress_motion = was_down || block_until_lift};
    return was_down ? TPS43_FORCE_RELEASE : TPS43_FORCE_NONE;
}

static inline uint32_t tps43_force_threshold(uint16_t baseline, uint16_t minimum,
                                            uint16_t percent) {
    uint32_t relative = ((uint32_t)baseline * percent + 99U) / 100U;
    return relative > minimum ? relative : minimum;
}

static inline bool tps43_force_moved(uint16_t x, uint16_t y, uint16_t anchor_x,
                                     uint16_t anchor_y, uint32_t threshold) {
    int32_t dx = (int32_t)x - anchor_x;
    int32_t dy = (int32_t)y - anchor_y;
    return dx > (int32_t)threshold || dx < -(int32_t)threshold ||
           dy > (int32_t)threshold || dy < -(int32_t)threshold;
}

static inline enum toucan_touch_display_state
tps43_force_display_state(const struct tps43_force_state *state, uint8_t fingers, bool valid) {
    if (!valid || fingers == 0) {
        return TOUCAN_TOUCH_NONE;
    }
    return state->down ? TOUCAN_TOUCH_PRESSED : TOUCAN_TOUCH_CONTACT;
}

/* Re-establish the resting level after movement or unstable initial contact. */
static inline void tps43_force_window(struct tps43_force_state *state, int64_t now_ms,
                                      uint16_t strength, uint16_t x, uint16_t y) {
    state->ready = false;
    state->candidate = false;
    state->window_ms = now_ms;
    state->strength_sum = strength;
    state->samples = 1;
    state->strength_min = strength;
    state->strength_max = strength;
    state->baseline = strength;
    state->baseline_q8 = (int32_t)strength * 256;
    state->anchor_x = x;
    state->anchor_y = y;
}

static inline enum tps43_force_event
tps43_force_step(struct tps43_force_state *state,
                 const struct tps43_force_config *config, int64_t now_ms,
                 uint8_t fingers, uint16_t strength, bool valid, uint16_t x, uint16_t y) {
    state->suppress_motion = now_ms < state->quiet_until_ms;
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
    int64_t elapsed_ms = state->active ? now_ms - state->last_sample_ms : 0;
    state->last_sample_ms = now_ms;
    if (!state->active) {
        state->active = true;
        state->tap_consumed = false;
        state->started_ms = now_ms;
        state->motion_window_ms = now_ms;
        state->previous_x = state->motion_x = x;
        state->previous_y = state->motion_y = y;
        tps43_force_window(state, now_ms, strength, x, y);
        return TPS43_FORCE_NONE;
    }

    /* Detect recent travel, not distance from the original touch-down point.
     * A squeeze may move its centroid: once a stationary squeeze is a candidate,
     * position changes must not discard it and absorb its force into baseline. */
    bool was_moving = now_ms < state->motion_until_ms;
    int32_t dx = (int32_t)x - state->previous_x;
    int32_t dy = (int32_t)y - state->previous_y;
    /* Accumulate slow travel without treating bounded back-and-forth jitter as
     * a new movement on every report. Two small steps in the same direction
     * also preserve movement-before-pressure ordering below the dead band. */
    bool prior_travel = state->previous_resting &&
        (((dx > 0 && state->previous_dx > 0) || (dx < 0 && state->previous_dx < 0)) &&
             (dx + state->previous_dx > config->motion_threshold ||
              dx + state->previous_dx < -(int32_t)config->motion_threshold));
    prior_travel |= state->previous_resting &&
        (((dy > 0 && state->previous_dy > 0) || (dy < 0 && state->previous_dy < 0)) &&
             (dy + state->previous_dy > config->motion_threshold ||
              dy + state->previous_dy < -(int32_t)config->motion_threshold));
    bool moving = tps43_force_moved(x, y, state->motion_x, state->motion_y,
                                    config->motion_threshold);
    state->previous_x = x;
    state->previous_y = y;
    state->previous_dx = dx;
    state->previous_dy = dy;
    if (moving) {
        state->motion_window_ms = now_ms;
        state->motion_x = x;
        state->motion_y = y;
    }
    if (!state->ready) {
        if (moving) {
            state->motion_until_ms = now_ms + TPS43_FORCE_STOP_MS;
        }
        uint16_t low = strength < state->strength_min ? strength : state->strength_min;
        uint16_t high = strength > state->strength_max ? strength : state->strength_max;
        uint32_t tolerance = tps43_force_threshold(state->baseline, 16, 3);
        if ((uint32_t)high - low > tolerance) {
            tps43_force_window(state, now_ms, strength, x, y);
            return TPS43_FORCE_NONE;
        }
        state->strength_min = low;
        state->strength_max = high;
        state->strength_sum += strength;
        state->samples++;
        state->baseline = (uint16_t)(state->strength_sum / state->samples);
        state->baseline_q8 = (int32_t)state->baseline * 256;
        if (now_ms - state->started_ms >= config->settle_ms &&
            now_ms - state->window_ms >= config->baseline_ms && state->samples >= 4) {
            state->ready = true;
        }
        return TPS43_FORCE_NONE;
    }

    int32_t delta = (int32_t)strength - (int32_t)state->baseline;
    uint32_t press_threshold = tps43_force_threshold(state->baseline, config->press_delta,
                                                     config->press_percent);
    uint32_t release_threshold = tps43_force_threshold(state->baseline, config->release_delta,
                                                       config->release_percent);
    state->previous_resting = delta < (int32_t)(press_threshold / 3U);

    if (!state->down && state->candidate &&
        tps43_force_moved(x, y, state->candidate_x, state->candidate_y,
                          (uint32_t)config->drag_threshold * 3U)) {
        /* A large ongoing swipe is not a squeeze; allow normal centroid
         * deformation during confirmation instead of the old tiny anchor gate. */
        state->candidate = false;
        state->motion_until_ms = now_ms + TPS43_FORCE_STOP_MS;
        state->baseline = strength;
        state->baseline_q8 = (int32_t)strength * 256;
        return TPS43_FORCE_NONE;
    }

    if (!state->down && !state->candidate &&
        (was_moving || prior_travel || (moving && state->previous_resting))) {
        /* Travel that began before pressure rose cannot become a drag. The
         * local resting level follows travel; a short pause arms the next squeeze. */
        if (moving || prior_travel) {
            state->baseline = strength;
            state->baseline_q8 = (int32_t)strength * 256;
            state->motion_until_ms = now_ms + TPS43_FORCE_STOP_MS;
        }
        /* Once coordinates settle, keep the last moving baseline. Absorbing
         * pressure during this guard made an immediate stop-and-squeeze vanish. */
        return TPS43_FORCE_NONE;
    }
    bool next_down = state->down ? delta > (int32_t)release_threshold
                                 : delta >= (int32_t)press_threshold;

    /* Click deformation is not a drag. Start dragging only after deliberate
     * displacement, and never reapply this dead zone once a drag has started. */
    if (state->down) {
        if (next_down && !state->dragging && tps43_force_moved(x, y, state->drag_x, state->drag_y,
                                                 config->drag_threshold)) {
            state->dragging = true;
        }
        state->suppress_motion = !state->dragging;
    }
    if (next_down == state->down) {
        state->candidate = false;
        if (!state->down) {
            /* Track slow resting drift; adapt downward faster after the user
             * relaxes a firm initial contact. Freeze during press candidates. */
            int32_t diff = (int32_t)strength * 256 - state->baseline_q8;
            int64_t tau_ms = diff < 0 ? 80 : 1000;
            int64_t dt = elapsed_ms < tau_ms ? elapsed_ms : tau_ms;
            state->baseline_q8 += (int32_t)((int64_t)diff * dt / tau_ms);
            state->baseline = (uint16_t)(state->baseline_q8 / 256);
        }
        return TPS43_FORCE_NONE;
    }
    if (!state->candidate) {
        state->candidate = true;
        state->candidate_ms = now_ms;
        state->candidate_x = x;
        state->candidate_y = y;
    }
    /* Do not emit cursor displacement during the force decision itself. */
    state->suppress_motion = !state->down || !state->dragging;
    if (now_ms - state->candidate_ms < config->debounce_ms) {
        return TPS43_FORCE_NONE;
    }
    state->candidate = false;
    state->down = next_down;
    if (next_down) {
        state->tap_consumed = true;
        state->dragging = false;
        state->drag_x = x;
        state->drag_y = y;
    } else {
        /* Keep the resting baseline and ready state for a second squeeze.
         * A drag stays fluid while release is debounced; only stationary click
         * deformation needs the small release guard. */
        bool was_dragging = state->dragging;
        state->quiet_until_ms = now_ms + (was_dragging ? 0 : config->debounce_ms);
        state->dragging = false;
        state->motion_until_ms = now_ms + (was_dragging ? TPS43_FORCE_STOP_MS : 0);
        state->motion_window_ms = now_ms;
        state->motion_x = x;
        state->motion_y = y;
    }
    return next_down ? TPS43_FORCE_PRESS : TPS43_FORCE_RELEASE;
}
