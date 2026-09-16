/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

/* Queue button transitions, not one entry per sensor sample. The caller
 * serializes access; enqueue never waits for the Bluetooth consumer. */
#define TOUCAN_MOUSE_QUEUE_CAPACITY 16
#define TOUCAN_MOUSE_MOTION_MAX_AGE_MS 40

struct toucan_mouse_packet {
    uint16_t buttons;
    int16_t x, y, wheel, pan;
    int64_t motion_ms;
};
struct toucan_mouse_queue {
    struct toucan_mouse_packet packets[TOUCAN_MOUSE_QUEUE_CAPACITY];
    uint8_t head, count;
};

static inline int16_t toucan_mouse_add(int16_t a, int16_t b) {
    int32_t value = (int32_t)a + b;
    return value > INT16_MAX ? INT16_MAX : value < INT16_MIN ? INT16_MIN : (int16_t)value;
}
static inline void toucan_mouse_expire(struct toucan_mouse_packet *p, int64_t now) {
    if (now - p->motion_ms > TOUCAN_MOUSE_MOTION_MAX_AGE_MS) {
        p->x = p->y = p->wheel = p->pan = 0;
        p->motion_ms = now;
    }
}
static inline void toucan_mouse_merge(struct toucan_mouse_packet *dst,
                                       struct toucan_mouse_packet src, int64_t now) {
    toucan_mouse_expire(dst, now);
    toucan_mouse_expire(&src, now);
    dst->x = toucan_mouse_add(dst->x, src.x);
    dst->y = toucan_mouse_add(dst->y, src.y);
    dst->wheel = toucan_mouse_add(dst->wheel, src.wheel);
    dst->pan = toucan_mouse_add(dst->pan, src.pan);
    /* A continuous stream remains fresh. Using the oldest sample's time
     * discarded recent displacement every 40 ms on a slow BLE consumer.
     * Empty reports must not keep old movement alive after the finger stops. */
    if ((src.x || src.y || src.wheel || src.pan) && src.motion_ms > dst->motion_ms) {
        dst->motion_ms = src.motion_ms;
    }
}
static inline void toucan_mouse_push(struct toucan_mouse_queue *q,
                                      struct toucan_mouse_packet p, int64_t now) {
    if (q->count) {
        unsigned int last = (q->head + q->count - 1U) % TOUCAN_MOUSE_QUEUE_CAPACITY;
        if (q->packets[last].buttons == p.buttons) {
            toucan_mouse_merge(&q->packets[last], p, now);
            return;
        }
    }
    if (q->count == TOUCAN_MOUSE_QUEUE_CAPACITY) {
        /* A pathological edge flood must still release any host-held button.
         * Normal taps/double-clicks retain every edge, regardless of motion. */
        *q = (struct toucan_mouse_queue){0};
        q->packets[0] = (struct toucan_mouse_packet){.motion_ms = now};
        q->count = 1;
        if (!p.buttons) {
            q->packets[0] = p;
            return;
        }
    }
    q->packets[(q->head + q->count) % TOUCAN_MOUSE_QUEUE_CAPACITY] = p;
    q->count++;
}
static inline bool toucan_mouse_pop(struct toucan_mouse_queue *q,
                                     struct toucan_mouse_packet *p, int64_t now) {
    if (!q->count) {
        return false;
    }
    *p = q->packets[q->head];
    q->head = (q->head + 1U) % TOUCAN_MOUSE_QUEUE_CAPACITY;
    q->count--;
    toucan_mouse_expire(p, now);
    return true;
}
/* Restore a failed notification before newer button edges, coalescing motion.
 * No successfully submitted notification is retried. */
static inline void toucan_mouse_retry(struct toucan_mouse_queue *q,
                                       struct toucan_mouse_packet p, int64_t now) {
    toucan_mouse_expire(&p, now);
    if (q->count && q->packets[q->head].buttons == p.buttons) {
        toucan_mouse_merge(&p, q->packets[q->head], now);
        q->packets[q->head] = p;
        return;
    }
    if (q->count == TOUCAN_MOUSE_QUEUE_CAPACITY) {
        /* Nothing was submitted for p, so the bounded queue's final release
         * and current button state take precedence over this oldest edge. */
        return;
    }
    q->head = (q->head + TOUCAN_MOUSE_QUEUE_CAPACITY - 1U) % TOUCAN_MOUSE_QUEUE_CAPACITY;
    q->packets[q->head] = p;
    q->count++;
}
