/* SPDX-License-Identifier: MIT */
#pragma once
#include "tps43_force.h"

/* v14 protocol 2: fixed levels are recorded directly. No learned baseline.
 * Little endian, 60 bytes. kind 0=frame, 1=I2C/reset, 2=watchdog.
 * extra bits: valid, software tap, three-tap click, three-tap claimed.
 * rc 32767 means no report attempted. No USB I/O in the sensor producer. */
struct tps43_trace_record {
    uint32_t sample_ms, work_us, io_us;
    uint8_t raw[16];
    uint16_t before_flags, after_flags;
    uint16_t lock_level, press_level, release_level;
    uint16_t pressed_age, candidate_age;
    int16_t output_x, output_y, event, button_rc, x_rc, y_rc, frame_rc;
    uint8_t kind, extra;
    uint16_t reserved;
};
_Static_assert(sizeof(struct tps43_trace_record) == 60, "Trace wire layout changed");

#ifdef CONFIG_TOUCAN_FORCE_TRACE
void tps43_trace_record(const struct tps43_trace_record *record, bool touching);
static inline uint16_t tps43_trace_flags(const struct tps43_force_state *s) {
    return (s->active << 0) | (s->blocked << 1) | (s->down << 2) |
        (s->tap_consumed << 3) | (s->candidate << 4) | (s->prepress << 5) |
        (s->candidate_moving << 6) | (s->dragging << 7) | (s->drag_armed << 8) |
        (s->suppress_motion << 9) | (s->hold_cancelled << 10);
}
static inline uint16_t tps43_trace_duration(int64_t value) {
    return value < 0 ? 0 : value > 65535 ? 65535 : (uint16_t)value;
}
static inline void tps43_trace_state(struct tps43_trace_record *r,
                                     const struct tps43_force_state *s,
                                     const struct tps43_force_config *c) {
    r->after_flags = tps43_trace_flags(s);
    r->lock_level = s->active ? s->lock_level : c->lock_level;
    r->press_level = s->active ? s->press_level : c->press_level;
    r->release_level = c->release_level;
    r->pressed_age = s->down ? tps43_trace_duration((int64_t)r->sample_ms-s->pressed_ms) : 0;
    r->candidate_age = s->candidate ? tps43_trace_duration((int64_t)r->sample_ms-s->candidate_ms) : 0;
}
#endif
