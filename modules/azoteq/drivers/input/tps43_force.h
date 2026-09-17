/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <toucan/force_display.h>
#include <toucan/force_levels.h>

#define TPS43_FORCE_STALE_MS 250
#define TPS43_FORCE_LOCK_MS 120
#define TPS43_HOLD_SLOP 12U

enum tps43_force_event {
    TPS43_FORCE_RELEASE = -1, TPS43_FORCE_NONE = 0, TPS43_FORCE_PRESS = 1,
    TPS43_FORCE_CLICK = 2, /* complete down/up pair; never a held button */
};

/* Absolute click/lock floors stay fixed across contacts. A fixed-size rise and
 * fall identifies a pulse even when the valley stays above release_level.
 * Extrema describe the waveform, not a learned touchdown sensitivity. */
struct tps43_force_config {
    uint16_t lock_level, press_level, release_level;
    uint16_t moving_lock_level, moving_press_level;
    uint16_t debounce_ms, pulse_delta;
    uint16_t motion_threshold, motion_settle_ms;
    uint16_t drag_threshold, touch_hold_ms;
};
struct tps43_force_state {
    bool active, blocked, down, tap_consumed;
    bool candidate, prepress, candidate_moving, pulse_ready;
    bool dragging, suppress_motion, hold_cancelled;
    int64_t started_ms, last_sample_ms, motion_until_ms;
    int64_t candidate_ms, pressed_ms, lock_ms;
    uint16_t motion_x, motion_y, hold_x, hold_y;
    uint16_t lock_level, press_level, trough, peak;
};

static inline void tps43_force_set_levels(struct tps43_force_config *c,
                                          const struct toucan_force_levels *v) {
    c->lock_level=v->lock; c->press_level=v->press; c->release_level=v->release;
    c->moving_lock_level=v->moving_lock; c->moving_press_level=v->moving_press;
}
static inline bool tps43_force_config_valid(const struct tps43_force_config *c) {
    return c->release_level > 0 && c->release_level < c->lock_level &&
        c->lock_level < c->press_level && c->moving_lock_level >= c->lock_level &&
        c->moving_press_level >= c->press_level &&
        c->moving_lock_level < c->moving_press_level && c->debounce_ms <= 1000 &&
        c->pulse_delta >= 2 && c->motion_threshold > 0 &&
        c->drag_threshold > c->motion_threshold && c->motion_settle_ms <= 1000 &&
        c->touch_hold_ms <= 1000;
}
static inline bool tps43_force_moved(uint16_t x, uint16_t y, uint16_t anchor_x,
                                    uint16_t anchor_y, uint32_t threshold) {
    int32_t dx = (int32_t)x - anchor_x, dy = (int32_t)y - anchor_y;
    return dx > (int32_t)threshold || dx < -(int32_t)threshold ||
           dy > (int32_t)threshold || dy < -(int32_t)threshold;
}
static inline enum tps43_force_event
tps43_force_cancel(struct tps43_force_state *s, bool block_until_lift) {
    bool down = s->down;
    bool consumed = s->tap_consumed || down || block_until_lift;
    *s = (struct tps43_force_state){.blocked=block_until_lift,
        .tap_consumed=consumed, .suppress_motion=down || block_until_lift};
    return down ? TPS43_FORCE_RELEASE : TPS43_FORCE_NONE;
}
static inline enum toucan_touch_display_state
tps43_force_display_state(const struct tps43_force_state *s, uint8_t fingers, bool valid) {
    if (!valid || !fingers) { return TOUCAN_TOUCH_NONE; }
    return s->down || (s->pulse_ready && s->suppress_motion)
        ? TOUCAN_TOUCH_PRESSED : TOUCAN_TOUCH_CONTACT;
}
static inline enum tps43_force_event
tps43_force_step(struct tps43_force_state *s, const struct tps43_force_config *c,
                 int64_t now, uint8_t fingers, uint16_t strength, bool valid,
                 uint16_t x, uint16_t y) {
    s->suppress_motion = false;
    if (!valid) { return tps43_force_cancel(s, true); }
    if (s->active && now - s->last_sample_ms > TPS43_FORCE_STALE_MS) {
        return tps43_force_cancel(s, fingers != 0);
    }
    if (!fingers) {
        bool click = s->pulse_ready;
        enum tps43_force_event event = tps43_force_cancel(s, false);
        return click ? TPS43_FORCE_CLICK : event;
    }
    s->last_sample_ms = now;
    /* Only the stationary-touch hold path can own a held button. */
    if (s->down && s->dragging && fingers == 1) { return TPS43_FORCE_NONE; }
    if (fingers != 1 || !strength) { return tps43_force_cancel(s, true); }
    if (s->blocked) { return TPS43_FORCE_NONE; }
    if (!s->active) {
        s->active = true; s->tap_consumed = false; s->started_ms = now;
        s->motion_x = s->hold_x = x; s->motion_y = s->hold_y = y;
        s->lock_level = c->lock_level; s->press_level = c->press_level;
        s->trough = s->peak = strength;
    }
    /* Initial stationary contact for 250 ms, then deliberate movement. Travel
     * during the wait cancels hold until lift. No extra force timer or speed
     * deadline after the hold, and no pressure-dependent drag release. */
    if (c->touch_hold_ms && !s->hold_cancelled) {
        if (now - s->started_ms < c->touch_hold_ms) {
            if (tps43_force_moved(x, y, s->hold_x, s->hold_y, TPS43_HOLD_SLOP)) {
                s->hold_cancelled = true;
            }
        } else if (tps43_force_moved(x, y, s->hold_x, s->hold_y, c->drag_threshold)) {
            s->pulse_ready = s->candidate = s->prepress = false;
            s->down = s->dragging = s->tap_consumed = true;
            s->pressed_ms = now; s->suppress_motion = false;
            return TPS43_FORCE_PRESS;
        }
    }
    if (strength < s->trough) { s->trough = strength; }
    uint32_t rise = (uint32_t)strength - s->trough;
    if (!s->prepress && !s->pulse_ready) {
        bool travelled = tps43_force_moved(x, y, s->motion_x, s->motion_y,
                                          c->motion_threshold);
        bool moving = now < s->motion_until_ms;
        /* A same-frame squeeze can move the centroid. */
        if (travelled && (moving || strength < c->lock_level)) {
            moving = true; s->motion_until_ms = now + c->motion_settle_ms;
        }
        if (travelled) { s->motion_x = x; s->motion_y = y; }
        s->candidate_moving = moving;
        s->lock_level = moving ? c->moving_lock_level : c->lock_level;
        s->press_level = moving ? c->moving_press_level : c->press_level;
        if (strength >= s->lock_level && rise >= c->pulse_delta / 2U) {
            s->prepress = true; s->lock_ms = now;
        }
    }
    if (!s->pulse_ready) {
        if (strength >= s->press_level && rise >= c->pulse_delta) {
            if (!s->candidate) {
                s->candidate = true; s->candidate_ms = now; s->peak = strength;
            }
            if (strength > s->peak) { s->peak = strength; }
            if (now - s->candidate_ms >= c->debounce_ms) {
                s->pulse_ready = s->tap_consumed = true;
            }
        } else { s->candidate = false; }
    }
    if (s->pulse_ready) {
        if (strength > s->peak) { s->peak = strength; }
        if (strength <= c->release_level || (uint32_t)s->peak - strength >= c->pulse_delta) {
            s->pulse_ready = s->candidate = s->prepress = false;
            s->trough = s->peak = strength;
            s->hold_cancelled = true; /* a completed click cannot turn into hold drag */
            s->suppress_motion = true; /* discard this click's centroid shift */
            return TPS43_FORCE_CLICK;
        }
    } else if (!s->candidate &&
               (strength < s->lock_level || rise < c->pulse_delta / 2U)) {
        s->prepress = false;
    }
    /* Bound the lock, including an arbitrarily high plateau. Never queue or
     * replay suppressed XY, and never refresh this deadline on every frame. */
    s->suppress_motion = s->prepress && now - s->lock_ms < TPS43_FORCE_LOCK_MS;

    return TPS43_FORCE_NONE;
}
