/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <toucan/force_display.h>

#define TPS43_FORCE_STALE_MS 250
#define TPS43_HOLD_SLOP 12U
#define TPS43_HOLD_MOVE_MS 120U

enum tps43_force_event {
    TPS43_FORCE_RELEASE = -1, TPS43_FORCE_NONE = 0, TPS43_FORCE_PRESS = 1,
};

/* Raw sensor levels, fixed for every contact. No baseline, percentage of
 * touchdown strength, peak tracking or special double-click threshold. */
struct tps43_force_config {
    uint16_t lock_level, press_level, release_level;
    uint16_t moving_lock_level, moving_press_level;
    uint16_t debounce_ms;
    uint16_t motion_threshold, motion_settle_ms;
    uint16_t drag_threshold, drag_hold_ms, touch_hold_ms;
};

struct tps43_force_state {
    bool active, blocked, down, tap_consumed;
    bool candidate, prepress, candidate_moving;
    bool dragging, drag_armed, suppress_motion, hold_cancelled;
    int64_t started_ms, last_sample_ms, motion_until_ms;
    int64_t candidate_ms, pressed_ms, hold_motion_ms;
    uint16_t motion_x, motion_y, hold_x, hold_y, drag_x, drag_y;
    uint16_t lock_level, press_level;
};

static inline bool tps43_force_config_valid(const struct tps43_force_config *c) {
    return c->release_level > 0 && c->release_level < c->lock_level &&
        c->lock_level < c->press_level && c->moving_lock_level >= c->lock_level &&
        c->moving_press_level >= c->press_level &&
        c->moving_lock_level < c->moving_press_level && c->debounce_ms <= 1000 &&
        c->motion_threshold > 0 && c->drag_threshold >= c->motion_threshold &&
        c->motion_settle_ms <= 1000 && c->drag_hold_ms <= 1000 && c->touch_hold_ms <= 1000;
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
    return s->down ? TOUCAN_TOUCH_PRESSED : TOUCAN_TOUCH_CONTACT;
}

static inline enum tps43_force_event
tps43_force_step(struct tps43_force_state *s, const struct tps43_force_config *c,
                 int64_t now, uint8_t fingers, uint16_t strength, bool valid,
                 uint16_t x, uint16_t y) {
    s->suppress_motion = false;
    if (!valid) { return tps43_force_cancel(s, true); }
    if (!fingers) { return tps43_force_cancel(s, false); }
    if (s->active && now - s->last_sample_ms > TPS43_FORCE_STALE_MS) {
        return tps43_force_cancel(s, true);
    }
    s->last_sample_ms = now;
    /* Drag owns the button until lift, independent of strength changes. */
    if (s->down && s->dragging && fingers == 1) { return TPS43_FORCE_NONE; }
    if (fingers != 1 || !strength) { return tps43_force_cancel(s, true); }
    if (s->blocked) { return TPS43_FORCE_NONE; }
    if (!s->active) {
        s->active = true;
        s->tap_consumed = false;
        s->started_ms = now;
        s->motion_x = s->hold_x = x;
        s->motion_y = s->hold_y = y;
        s->lock_level = c->lock_level;
        s->press_level = c->press_level;
        /* A firm initial contact uses the same fixed level. */
    }
    if (s->down) {
        /* Hysteresis separates noise from release. A short real release must
         * re-arm the next click; there is no additional release timer. */
        if (strength <= c->release_level) {
            s->down = s->candidate = s->prepress = s->drag_armed = false;
            s->suppress_motion = true;
            s->motion_x = x; s->motion_y = y;
            s->motion_until_ms = 0;
            return TPS43_FORCE_RELEASE;
        }
        s->suppress_motion = true;
        if (!s->drag_armed) {
            s->drag_x = x; s->drag_y = y;
            s->drag_armed = now - s->pressed_ms >= c->drag_hold_ms;
        } else if (tps43_force_moved(x, y, s->drag_x, s->drag_y, c->drag_threshold)) {
            s->dragging = true;
            s->suppress_motion = false;
        }
        return TPS43_FORCE_NONE;
    }
    /* Select two fixed profiles. Once pressure locks the pointer, squeeze
     * deformation cannot switch profiles or raise the click level. */
    if (!s->prepress) {
        bool travelled = tps43_force_moved(x, y, s->motion_x, s->motion_y,
                                          c->motion_threshold);
        bool moving = now < s->motion_until_ms;
        /* Same-frame pressure can shift the centroid. Remember earlier
         * low-pressure travel instead of promoting a stationary squeeze. */
        if (travelled && (moving || strength < c->lock_level)) {
            moving = true;
            s->motion_until_ms = now + c->motion_settle_ms;
        }
        if (travelled) { s->motion_x = x; s->motion_y = y; }
        s->candidate_moving = moving;
        s->lock_level = moving ? c->moving_lock_level : c->lock_level;
        s->press_level = moving ? c->moving_press_level : c->press_level;
        if (strength >= s->lock_level) {
            s->prepress = true;
            s->hold_cancelled = true;
        }
    }
    if (s->prepress) {
        if (strength < s->lock_level) {
            s->prepress = s->candidate = false;
            s->motion_x = x; s->motion_y = y;
        } else {
            s->suppress_motion = true;
            if (strength < s->press_level) {
                s->candidate = false;
                return TPS43_FORCE_NONE;
            }
            if (!s->candidate) { s->candidate = true; s->candidate_ms = now; }
            if (now - s->candidate_ms < c->debounce_ms) { return TPS43_FORCE_NONE; }
            s->candidate = s->prepress = false;
            s->down = s->tap_consumed = true;
            s->pressed_ms = now;
            s->drag_armed = c->drag_hold_ms == 0;
            s->drag_x = x; s->drag_y = y;
            return TPS43_FORCE_PRESS;
        }
    }
    /* Initial rest can arm drag. Ordinary movement is never delayed. Cancel
     * travel before the hold, or slow precision travel after the hold. */
    if (c->touch_hold_ms && !s->tap_consumed && !s->hold_cancelled) {
        bool outside = tps43_force_moved(x, y, s->hold_x, s->hold_y, TPS43_HOLD_SLOP);
        if (now - s->started_ms < c->touch_hold_ms) {
            if (outside) { s->hold_cancelled = true; }
        } else if (outside) {
            if (!s->hold_motion_ms) { s->hold_motion_ms = now; }
            if (now - s->hold_motion_ms > TPS43_HOLD_MOVE_MS) {
                s->hold_cancelled = true;
            } else if (tps43_force_moved(x, y, s->hold_x, s->hold_y, c->drag_threshold)) {
                s->down = s->dragging = s->tap_consumed = true;
                s->pressed_ms = now;
                return TPS43_FORCE_PRESS;
            }
        }
    }
    return TPS43_FORCE_NONE;
}
