/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <toucan/force_display.h>

/* Shared by the device driver and the host-side behavioural tests. */
#define TPS43_FORCE_STALE_MS 250
/* Keep v4's 24 * 3 squeeze travel limit independent of drag sensitivity.
 * Raising the drag threshold must not also admit moving false clicks. */
#define TPS43_FORCE_PRESS_TRAVEL_LIMIT 72U
#define TPS43_FORCE_REBOUND_MS 150U
#define TPS43_HOLD_SLOP 12U
#define TPS43_HOLD_MOVE_MS 120U

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
    uint16_t release_debounce_ms;
    uint16_t press_percent;
    uint16_t release_percent;
    uint16_t motion_threshold;
    uint16_t drag_threshold;
    uint16_t settle_ms;
    uint16_t moving_press_delta;
    uint16_t moving_press_percent;
    uint16_t motion_settle_ms;
    uint16_t drag_hold_ms;
    uint16_t repeat_ms;
    uint16_t touch_hold_ms;
    uint16_t click_margin_percent;
};

struct tps43_force_state {
    int64_t rebound_peak_ms;
    uint16_t rebound_trough;
    uint8_t rebound_samples;
    bool rebound_release;
    bool rebound_used;
    bool active;
    bool ready;
    bool blocked;
    bool down;
    bool tap_consumed;
    bool candidate;
    bool prepress;
    bool candidate_moving;
    bool dragging;
    bool drag_armed;
    bool suppress_motion;
    bool previous_resting;
    bool hold_cancelled;
    int64_t hold_motion_ms;
    int32_t previous_dx;
    int32_t previous_dy;
    int64_t started_ms;
    int64_t window_ms;
    int64_t last_sample_ms;
    int64_t candidate_ms;
    int64_t quiet_until_ms;
    int64_t motion_until_ms;
    int64_t pressed_ms;
    int64_t repeat_until_ms;
    uint64_t strength_sum;
    uint32_t samples;
    uint16_t baseline;
    uint16_t press_peak;
    uint16_t release_trough;
    uint16_t candidate_threshold;
    uint16_t repeat_threshold;
    int32_t baseline_q8;
    uint16_t strength_min;
    uint16_t strength_max;
    uint16_t candidate_x;
    uint16_t candidate_y;
    uint16_t drag_x;
    uint16_t drag_y;
    uint16_t previous_x;
    uint16_t previous_y;
    uint16_t motion_x;
    uint16_t motion_y;
    uint16_t repeat_x;
    uint16_t repeat_y;
    uint16_t hold_x;
    uint16_t hold_y;
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

static inline bool tps43_force_small_travel(int32_t step, int32_t previous,
                                            uint16_t threshold) {
    if ((step > 0 && previous > 0) || (step < 0 && previous < 0)) {
        int32_t a = step < 0 ? -step : step;
        int32_t b = previous < 0 ? -previous : previous;
        /* A tiny step after a large one is deceleration, not slow travel. */
        return a <= threshold && b <= threshold;
    }
    return false;
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
                                      uint16_t strength) {
    state->ready = false;
    state->candidate = false;
    state->prepress = false;
    state->window_ms = now_ms;
    state->strength_sum = strength;
    state->samples = 1;
    state->strength_min = strength;
    state->strength_max = strength;
    state->baseline = strength;
    state->baseline_q8 = (int32_t)strength * 256;
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
    if (state->active && now_ms - state->last_sample_ms > TPS43_FORCE_STALE_MS) {
        return tps43_force_cancel(state, true);
    }
    int64_t elapsed_ms = state->active ? now_ms - state->last_sample_ms : 0;
    state->last_sample_ms = now_ms;
    /* Once intentional travel has started, pressure no longer owns release.
     * A zero strength sample with valid single-finger contact is not a lift.
     * Invalid/multiple-finger frames and stale data retain cancellation. */
    if (state->down && state->dragging && fingers == 1) {
        state->candidate = false;
        state->suppress_motion = false;
        return TPS43_FORCE_NONE;
    }
    if (fingers != 1 || strength == 0) {
        return tps43_force_cancel(state, true);
    }
    if (state->blocked) {
        return TPS43_FORCE_NONE;
    }
    if (!state->active) {
        state->active = true;
        state->tap_consumed = false;
        state->started_ms = now_ms;
        state->previous_x = state->motion_x = x;
        state->previous_y = state->motion_y = y;
        state->hold_x = x;
        state->hold_y = y;
        tps43_force_window(state, now_ms, strength);
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
    bool prior_travel = (state->previous_resting || was_moving) &&
        (tps43_force_small_travel(dx, state->previous_dx, config->motion_threshold) ||
         tps43_force_small_travel(dy, state->previous_dy, config->motion_threshold));
    bool moving = tps43_force_moved(x, y, state->motion_x, state->motion_y,
                                    config->motion_threshold);
    /* Hold belongs only to the initial contact. Bound landing drift rather
     * than rejecting two one-unit steps. Once travel crosses the landing
     * drift band, require deliberate drag distance promptly; slow precision
     * travel cannot eventually latch a drag, and no movement is buffered. */
    bool touch_hold_ready = config->touch_hold_ms && !state->tap_consumed &&
        !state->hold_cancelled && now_ms - state->started_ms >= config->touch_hold_ms;
    if (config->touch_hold_ms && !state->hold_cancelled && !state->tap_consumed) {
        if (!touch_hold_ready) {
            if (tps43_force_moved(x, y, state->hold_x, state->hold_y, TPS43_HOLD_SLOP)) {
                state->hold_cancelled = true;
            }
        } else if (tps43_force_moved(x, y, state->hold_x, state->hold_y,
                                      TPS43_HOLD_SLOP)) {
            if (!state->hold_motion_ms) { state->hold_motion_ms = now_ms; }
            if (now_ms - state->hold_motion_ms > TPS43_HOLD_MOVE_MS) {
                state->hold_cancelled = true;
                touch_hold_ready = false;
            }
        }
    }
    state->previous_x = x;
    state->previous_y = y;
    state->previous_dx = dx;
    state->previous_dy = dy;
    if (moving) {
        state->motion_x = x;
        state->motion_y = y;
    }
    if (!state->ready) {
        if (moving) {
            state->motion_until_ms = now_ms + config->motion_settle_ms;
        }
        uint16_t low = strength < state->strength_min ? strength : state->strength_min;
        uint16_t high = strength > state->strength_max ? strength : state->strength_max;
        uint32_t tolerance = tps43_force_threshold(state->baseline, 16, 3);
        if ((uint32_t)high - low > tolerance) {
            tps43_force_window(state, now_ms, strength);
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
    uint32_t resting_threshold = tps43_force_threshold(state->baseline, config->press_delta,
                                                       config->press_percent);
    uint32_t release_threshold = tps43_force_threshold(state->baseline, config->release_delta,
                                                       config->release_percent);
    state->previous_resting = delta < (int32_t)(resting_threshold / 3U);
    bool hold_near = touch_hold_ready &&
        !tps43_force_moved(x, y, state->hold_x, state->hold_y, config->drag_threshold);
    if (delta >= (int32_t)resting_threshold) {
        /* Only an actual pre-click rise cancels hold, not normal resting drift. */
        state->hold_cancelled = true;
        touch_hold_ready = false;
    }
    bool travelling = was_moving || prior_travel || (moving && state->previous_resting);
    if (hold_near) {
        travelling = false;
    }
    bool repeating = !state->down && now_ms < state->repeat_until_ms;
    if (repeating) {
        /* Two tiny steps are not enough to distinguish a second squeeze from
         * finger-centroid jitter. Keep the released click's position inside
         * the existing motion deadband. Leaving it resumes movement now, with
         * no queued displacement or extra timed wait. Check a pressure rise
         * before cancelling on coordinates from the same report; a squeeze's
         * existing press-travel limit handles centroid deformation. */
        bool repeat_rise = delta >= (int32_t)state->repeat_threshold &&
            !tps43_force_moved(x, y, state->repeat_x, state->repeat_y,
                               TPS43_FORCE_PRESS_TRAVEL_LIMIT);
        if (!state->candidate && !state->prepress && !repeat_rise &&
            tps43_force_moved(x, y, state->repeat_x, state->repeat_y,
                              config->motion_threshold)) {
            state->repeat_until_ms = 0;
            repeating = false;
            state->quiet_until_ms = 0;
            state->suppress_motion = false;
        } else {
            travelling = false;
            state->suppress_motion = true;
        }
    }
    uint32_t moving_threshold = tps43_force_threshold(state->baseline,
        config->moving_press_delta, config->moving_press_percent);
    if (moving_threshold < resting_threshold) { moving_threshold = resting_threshold; }
    uint32_t lock_threshold = (state->candidate || state->prepress) && !state->down
        ? state->candidate_threshold : (travelling ? moving_threshold :
            (repeating ? state->repeat_threshold : resting_threshold));
    uint32_t press_threshold =
        (lock_threshold * (100U + config->click_margin_percent) + 99U) / 100U;
    if (!state->down) {
        /* v8's click threshold now locks motion; a further strength increase
         * clicks. Freeze the chosen baseline/mode while pressure builds, so
         * squeeze-centroid motion cannot move the target or raise its threshold.
         * Relaxing below half the lock threshold releases it without chatter. */
        if (state->prepress && delta <= (int32_t)(lock_threshold / 2U)) {
            state->prepress = false;
            state->candidate = false;
        }
        if (!state->prepress && delta >= (int32_t)lock_threshold) {
            state->prepress = true;
            state->candidate_moving = travelling;
            state->candidate_threshold = (uint16_t)lock_threshold;
            state->candidate_x = x;
            state->candidate_y = y;
        }
    }

    if (!state->down && (state->candidate || state->prepress) &&
        (!state->candidate_moving || !state->candidate) &&
        tps43_force_moved(x, y, state->candidate_x, state->candidate_y,
                          TPS43_FORCE_PRESS_TRAVEL_LIMIT)) {
        /* A large ongoing swipe is not a squeeze; allow normal centroid
         * deformation during confirmation instead of the old tiny anchor gate. */
        state->candidate = false;
        state->prepress = false;
        state->motion_until_ms = now_ms + config->motion_settle_ms;
        state->baseline = strength;
        state->baseline_q8 = (int32_t)strength * 256;
        state->repeat_until_ms = 0;
        state->quiet_until_ms = 0;
        state->suppress_motion = false;
        return TPS43_FORCE_NONE;
    }

    if (!state->down && !state->candidate && travelling) {
        /* Movement has its own higher force threshold. Keep the last moving
         * baseline during the settling tail, so a stop-and-squeeze is not
         * calibrated away. Do not delay or buffer ordinary pointer movement. */
        if (moving || prior_travel) {
            state->motion_until_ms = now_ms + config->motion_settle_ms;
        }
    }
    /* A fast second squeeze may make only a shallow valley. Recognize its
     * recovery, without lowering the normal release threshold for all clicks.
     * Require a full-strength peak, consecutive falling samples, and a new
     * rise beyond that peak. A held click/drag and weak repeat-click wobble
     * must not be split. Only one such split is allowed before a real release. */
    if (state->down) {
        if (strength < state->rebound_trough) { state->rebound_trough = strength; }
        if ((uint32_t)strength + config->release_delta / 2U <= state->press_peak) {
            if (state->rebound_samples < 2) { state->rebound_samples++; }
        } else if (state->rebound_samples < 2) { state->rebound_samples = 0; }
        if (strength > state->press_peak && !state->rebound_release) {
            bool quick_rebound = !state->rebound_used &&
                (uint32_t)state->press_peak >= (uint32_t)state->baseline +
                    (resting_threshold * (100U + config->click_margin_percent) + 99U) / 100U &&
                now_ms - state->pressed_ms < config->drag_hold_ms &&
                state->rebound_samples >= 2 &&
                (uint32_t)state->rebound_trough + config->release_delta <= state->press_peak &&
                now_ms - state->rebound_peak_ms <= TPS43_FORCE_REBOUND_MS;
            if (quick_rebound) {
                /* Require a new rise above the former peak, not just a dip.
                 * An isolated low sample cannot qualify. */
                if ((uint32_t)strength >= (uint32_t)state->press_peak + config->release_delta) {
                    state->rebound_release = true;
                    state->rebound_used = true;
                }
            } else {
                state->press_peak = strength;
                state->rebound_peak_ms = now_ms;
                state->rebound_trough = strength;
                state->rebound_samples = 0;
            }
        }
    }
    /* A click releases on a sustained fall from its peak by the release band.
     * Release confirmation is separate from the longer press confirmation. */
    bool relaxed = state->down &&
                   (state->rebound_release ||
                    (uint32_t)strength + release_threshold <= state->press_peak);
    bool next_down = state->down ? delta > (int32_t)release_threshold && !relaxed
                                 : delta >= (int32_t)press_threshold;

    /* Share the force button's ownership and release path. Holding alone must
     * not click or freeze the pointer. A squeeze/candidate has priority over
     * timed hold, so waiting to force-click does not become a drag. */
    bool hold_attempt = touch_hold_ready &&
        !state->down && !state->candidate && !state->prepress && !next_down;
    if (hold_attempt && tps43_force_moved(x, y, state->hold_x, state->hold_y,
                                         config->drag_threshold)) {
        state->down = true;
        state->dragging = true;
        state->tap_consumed = true;
        state->repeat_until_ms = 0;
        state->suppress_motion = false;
        return TPS43_FORCE_PRESS;
    }
    /* Until drag distance is reached, ordinary pointer reports continue.
     * Expiring the bounded travel decision never replays stored movement. */

    /* A quick squeeze/rebound cannot latch drag. Arm after the configured
     * hold, discard displacement accumulated during that hold, then require
     * new travel. Normal pointer movement and button-down never wait here. */
    if (state->down) {
        if (!state->drag_armed) {
            state->drag_x = x;
            state->drag_y = y;
            if (next_down && now_ms - state->pressed_ms >= config->drag_hold_ms) {
                state->drag_armed = true;
            }
        } else if (next_down && tps43_force_moved(x, y, state->drag_x, state->drag_y,
                                                 config->drag_threshold)) {
            state->dragging = true;
        }
        state->suppress_motion = !state->dragging;
    }
    if (next_down == state->down) {
        state->candidate = false;
        if (!state->down) {
            if (state->prepress) {
                state->suppress_motion = true;
                return TPS43_FORCE_NONE;
            }
            if (repeating) {
                /* Follow the actual trough immediately; a quick second rise
                 * must not chase a slowly adapting average of the first click. */
                if (strength < state->baseline) {
                    state->baseline = strength;
                    state->baseline_q8 = (int32_t)strength * 256;
                }
                return TPS43_FORCE_NONE;
            }
            /* Track slow resting drift; adapt downward faster after the user
             * relaxes a firm initial contact. Freeze during press candidates. */
            int32_t diff = (int32_t)strength * 256 - state->baseline_q8;
            int64_t tau_ms = travelling ? 64 : (diff < 0 ? 80 : 1000);
            int64_t dt = elapsed_ms < tau_ms ? elapsed_ms : tau_ms;
            if (travelling && !moving && !prior_travel) { dt = 0; }
            state->baseline_q8 += (int32_t)((int64_t)diff * dt / tau_ms);
            state->baseline = (uint16_t)(state->baseline_q8 / 256);
        }
        return TPS43_FORCE_NONE;
    }
    if (!state->candidate) {
        state->candidate = true;
        state->candidate_ms = now_ms;
        state->release_trough = strength;
        if (!state->prepress) {
            state->candidate_moving = travelling && !state->down;
            state->candidate_threshold = (uint16_t)lock_threshold;
            state->candidate_x = x;
            state->candidate_y = y;
        }
    }
    if (state->down && strength < state->release_trough) {
        state->release_trough = strength;
    }
    /* Do not emit cursor displacement during the force decision itself. */
    state->suppress_motion = !state->down || !state->dragging;
    uint16_t confirm_ms = state->down ? config->release_debounce_ms : config->debounce_ms;
    if (now_ms - state->candidate_ms < confirm_ms) {
        return TPS43_FORCE_NONE;
    }
    state->candidate = false;
    state->prepress = false;
    state->down = next_down;
    if (next_down) {
        state->press_peak = strength;
        state->rebound_peak_ms = now_ms;
        state->rebound_trough = strength;
        state->rebound_samples = 0;
        state->rebound_release = false;
        state->pressed_ms = now_ms;
        state->drag_armed = config->drag_hold_ms == 0;
        state->repeat_until_ms = 0;
        state->tap_consumed = true;
        state->dragging = false;
        state->drag_x = x;
        state->drag_y = y;
    } else {
        /* This is a stationary click release; active drags return above.
         * Arm a second squeeze from the newly relaxed strength immediately. */
        if (state->rebound_release) {
            /* This is an already confirmed re-squeeze, not a relaxed resting
             * level. Keep the original baseline for the following down, so
             * it cannot immediately release against a newly raised baseline. */
            state->release_trough = state->baseline;
        } else {
            state->rebound_used = false;
        }
        state->rebound_release = false;
        state->baseline = state->release_trough;
        state->baseline_q8 = (int32_t)state->baseline * 256;
        state->repeat_until_ms = now_ms + config->repeat_ms;
        state->repeat_threshold = (uint16_t)release_threshold;
        state->repeat_x = x;
        state->repeat_y = y;
        state->press_peak = 0;
        state->previous_dx = state->previous_dy = 0;
        state->previous_resting = false;
        state->quiet_until_ms = now_ms + config->debounce_ms;
        state->dragging = false;
        state->drag_armed = false;
        state->motion_until_ms = 0;
        state->motion_x = x;
        state->motion_y = y;
    }
    return next_down ? TPS43_FORCE_PRESS : TPS43_FORCE_RELEASE;
}
