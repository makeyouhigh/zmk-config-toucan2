/* SPDX-License-Identifier: MIT */
#pragma once
#include "tps43_force.h"

/* Little-endian fixed wire layout; no pointers, struct padding, or USB work in
 * the producer. kind: 0 sensor frame, 1 error/reset, 2 watchdog cancellation.
 * extra: bit0 valid contact, bit1 software tap, bit2 three-tap click,
 * bit3 three-tap claimed. rc == 32767 means the report was not attempted. */
struct tps43_trace_record {
    uint32_t sample_ms;
    uint32_t work_us;
    uint32_t io_us;
    uint8_t raw[16];
    uint16_t before_flags;
    uint16_t after_flags;
    uint16_t baseline_before;
    uint16_t baseline;
    uint16_t peak;
    uint16_t threshold;
    uint16_t repeat_threshold;
    uint16_t trough;
    uint16_t pressed_age;
    uint16_t candidate_age;
    uint16_t repeat_left;
    int16_t output_x;
    int16_t output_y;
    int16_t event;
    int16_t button_rc;
    int16_t x_rc;
    int16_t y_rc;
    uint8_t kind;
    uint8_t extra;
    int16_t frame_rc;
    uint16_t reserved;
};
_Static_assert(sizeof(struct tps43_trace_record) == 68, "Trace wire layout changed");

#ifdef CONFIG_TOUCAN_FORCE_TRACE
void tps43_trace_record(const struct tps43_trace_record *record, bool touching);
static inline uint16_t tps43_trace_flags(const struct tps43_force_state *s) {
    return (s->active << 0) | (s->ready << 1) | (s->blocked << 2) |
           (s->down << 3) | (s->tap_consumed << 4) | (s->candidate << 5) |
           (s->prepress << 6) | (s->candidate_moving << 7) | (s->dragging << 8) |
           (s->drag_armed << 9) | (s->suppress_motion << 10) |
           (s->previous_resting << 11) | (s->hold_cancelled << 12);
}
static inline uint16_t tps43_trace_duration(int64_t value) {
    return value < 0 ? 0 : value > 65535 ? 65535 : (uint16_t)value;
}
static inline void tps43_trace_state(struct tps43_trace_record *r,
                                     const struct tps43_force_state *s) {
    r->after_flags = tps43_trace_flags(s);
    r->baseline = s->baseline;
    r->peak = s->press_peak;
    r->threshold = s->candidate_threshold;
    r->repeat_threshold = s->repeat_threshold;
    r->trough = s->release_trough;
    r->pressed_age = s->down ? tps43_trace_duration((int64_t)r->sample_ms - s->pressed_ms) : 0;
    r->candidate_age = s->candidate ?
        tps43_trace_duration((int64_t)r->sample_ms - s->candidate_ms) : 0;
    r->repeat_left = tps43_trace_duration(s->repeat_until_ms - r->sample_ms);
}
#endif

