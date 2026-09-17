/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

/* Wire-compatible ZMK split input events. XY frames merge before entering
 * Bluetooth, never across a button/touch/diagnostic boundary. */
#define TOUCAN_SPLIT_QUEUE_SIZE 32
#define TOUCAN_SPLIT_MAX_AGE_MS 40
#define TS_KEY 1
#define TS_REL 2
#define TS_ABS 3
#define TS_X 0
#define TS_Y 1
#define TS_BTN0 0x100
#define TS_TOUCH 0x14a

struct toucan_split_event { uint8_t type; uint16_t code; int32_t value; bool sync; };
struct toucan_split_frame {
    struct toucan_split_event event;
    int32_t x, y;
    int64_t ms;
    bool xy, sealed;
};
struct toucan_split_queue {
    struct toucan_split_frame frames[TOUCAN_SPLIT_QUEUE_SIZE];
    uint8_t count;
    int32_t pending_x, pending_y;
    bool pending;
    int64_t pending_ms;
    uint32_t overflows;
};
static inline int32_t toucan_split_add(int32_t a, int32_t b) {
    int64_t sum = (int64_t)a + b;
    return sum > INT16_MAX ? INT16_MAX : sum < INT16_MIN ? INT16_MIN : (int32_t)sum;
}
static inline void toucan_split_remove(struct toucan_split_queue *q, unsigned int at) {
    for (unsigned int i=at+1; i<q->count; i++) { q->frames[i-1] = q->frames[i]; }
    q->count--;
}
static inline bool toucan_split_movement(struct toucan_split_frame *f) {
    return f->xy || f->event.type == TS_REL;
}
static inline void toucan_split_append(struct toucan_split_queue *q,
                                       struct toucan_split_frame f, int64_t now) {
    if (q->count) {
        struct toucan_split_frame *tail = &q->frames[q->count-1];
        if (!tail->sealed && tail->xy && f.xy) {
            if (now - tail->ms > TOUCAN_SPLIT_MAX_AGE_MS) { tail->x = tail->y = 0; }
            tail->x = toucan_split_add(tail->x, f.x);
            tail->y = toucan_split_add(tail->y, f.y);
            tail->ms = f.ms;
            return;
        }
        if (!tail->sealed && !tail->xy && !f.xy && tail->event.type == TS_REL &&
            f.event.type == TS_REL && tail->event.code == f.event.code &&
            tail->event.sync && f.event.sync) {
            if (now - tail->ms > TOUCAN_SPLIT_MAX_AGE_MS) { tail->event.value = 0; }
            tail->event.value = toucan_split_add(tail->event.value, f.event.value);
            tail->ms = f.ms;
            return;
        }
    }
    if (q->count == TOUCAN_SPLIT_QUEUE_SIZE) {
        /* Remove only a complete motion or non-HID diagnostic first. A sealed
         * Y remainder must still sync its X, and button edges keep their order. */
        bool removed = false;
        for (unsigned int i=0; i<q->count; i++) {
            if (!q->frames[i].sealed && (toucan_split_movement(&q->frames[i]) ||
                q->frames[i].event.type == TS_ABS)) {
                toucan_split_remove(q,i); removed=true; break;
            }
        }
        if (!removed) {
            /* Pathological control flood: explicitly clear all mouse buttons
             * and contact before accepting the newest state. */
            q->overflows++;
            q->count=0;
            for (unsigned int i=0; i<5; i++) {
                q->frames[q->count++] = (struct toucan_split_frame){
                    .event={TS_KEY,(uint16_t)(TS_BTN0+i),0,true}, .ms=now};
            }
            q->frames[q->count++] = (struct toucan_split_frame){
                .event={TS_KEY,TS_TOUCH,0,true}, .ms=now};
        }
    }
    q->frames[q->count++] = f;
}
static inline void toucan_split_flush(struct toucan_split_queue *q, int64_t now) {
    if (!q->pending) { return; }
    struct toucan_split_frame f = {.xy=true, .x=q->pending_x, .y=q->pending_y,
                                   .ms=q->pending_ms};
    q->pending=false; q->pending_x=q->pending_y=0;
    toucan_split_append(q,f,now);
}
static inline void toucan_split_push(struct toucan_split_queue *q,
                                     struct toucan_split_event e, int64_t now) {
    if (e.type == TS_REL && (e.code == TS_X || e.code == TS_Y)) {
        if (q->pending && now-q->pending_ms > TOUCAN_SPLIT_MAX_AGE_MS) {
            q->pending_x=q->pending_y=0;
        }
        q->pending=true; q->pending_ms=now;
        if (e.code == TS_X) { q->pending_x=toucan_split_add(q->pending_x,e.value); }
        else { q->pending_y=toucan_split_add(q->pending_y,e.value); }
        if (e.sync) { toucan_split_flush(q,now); }
        return;
    }
    toucan_split_flush(q,now);
    toucan_split_append(q,(struct toucan_split_frame){.event=e,.ms=now},now);
}
static inline bool toucan_split_pop(struct toucan_split_queue *q,
        struct toucan_split_event *e, int64_t *ms, int64_t now) {
    while (q->count) {
        struct toucan_split_frame *f=&q->frames[0];
        bool expired = now-f->ms > TOUCAN_SPLIT_MAX_AGE_MS;
        *ms=f->ms;
        if (f->xy) {
            if (expired) { f->x=f->y=0; }
            if (!f->sealed && f->x) {
                *e=(struct toucan_split_event){TS_REL,TS_X,f->x,f->y==0};
                if (f->y) { f->x=0; f->sealed=true; }
                else { toucan_split_remove(q,0); }
                return true;
            }
            if (f->y || f->sealed) {
                *e=(struct toucan_split_event){TS_REL,TS_Y,f->y,true};
                toucan_split_remove(q,0); return true;
            }
            toucan_split_remove(q,0); continue;
        }
        *e=f->event;
        if (expired && e->type==TS_REL) { e->value=0; }
        toucan_split_remove(q,0); return true;
    }
    return false;
}
