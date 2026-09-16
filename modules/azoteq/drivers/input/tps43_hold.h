/* SPDX-License-Identifier: MIT */
#pragma once
#include "tps43_force.h"

struct tps43_hold_state {
    bool active;
    bool blocked;
    bool down;
    bool dragging;
    int64_t started_ms;
    int64_t last_sample_ms;
    uint16_t x;
    uint16_t y;
};

static inline enum tps43_force_event tps43_hold_cancel(struct tps43_hold_state *s,
                                                        bool block) {
    bool down = s->down;
    *s = (struct tps43_hold_state){.blocked=block};
    return down ? TPS43_FORCE_RELEASE : TPS43_FORCE_NONE;
}

static inline enum tps43_force_event
tps43_hold_step(struct tps43_hold_state *s, int64_t now, uint8_t fingers, bool valid,
                bool force_busy, uint16_t x, uint16_t y, uint32_t hold_ms,
                uint16_t rest_distance, uint16_t drag_distance) {
    if (!valid || fingers != 1 || force_busy ||
        (s->active && now - s->last_sample_ms > TPS43_FORCE_STALE_MS)) {
        return tps43_hold_cancel(s, fingers != 0);
    }
    if (s->blocked) {
        return TPS43_FORCE_NONE;
    }
    if (!s->active) {
        s->active = true;
        s->started_ms = now;
        s->x = x;
        s->y = y;
    }
    s->last_sample_ms = now;
    if (s->down) {
        if (tps43_force_moved(x, y, s->x, s->y, drag_distance)) {
            s->dragging = true;
        }
        return TPS43_FORCE_NONE;
    }
    if (tps43_force_moved(x, y, s->x, s->y, rest_distance)) {
        return tps43_hold_cancel(s, true);
    }
    if (now - s->started_ms >= hold_ms) {
        s->down = true;
        s->x = x;
        s->y = y;
        return TPS43_FORCE_PRESS;
    }
    return TPS43_FORCE_NONE;
}
