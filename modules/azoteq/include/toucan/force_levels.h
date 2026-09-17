/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <dt-bindings/zmk/force.h>

/* User-adjustable operating limits, not physical sensor limits. */
#define TOUCAN_FORCE_LOCK_MIN 1500
#define TOUCAN_FORCE_LOCK_MAX 5000
#define TOUCAN_FORCE_PRESS_MIN 2000
#define TOUCAN_FORCE_PRESS_MAX 6000
#define TOUCAN_FORCE_RELEASE_MIN 1000
#define TOUCAN_FORCE_RELEASE_MAX 4000
#define TOUCAN_FORCE_MOVING_OFFSET 500

struct toucan_force_levels {
    uint16_t lock, press, release, moving_lock, moving_press;
};
static inline bool toucan_force_levels_valid(const struct toucan_force_levels *v) {
    return v->lock >= TOUCAN_FORCE_LOCK_MIN && v->lock <= TOUCAN_FORCE_LOCK_MAX &&
        v->press >= TOUCAN_FORCE_PRESS_MIN && v->press <= TOUCAN_FORCE_PRESS_MAX &&
        v->release >= TOUCAN_FORCE_RELEASE_MIN && v->release <= TOUCAN_FORCE_RELEASE_MAX &&
        v->moving_lock >= TOUCAN_FORCE_LOCK_MIN + TOUCAN_FORCE_MOVING_OFFSET &&
        v->moving_lock <= TOUCAN_FORCE_LOCK_MAX + TOUCAN_FORCE_MOVING_OFFSET &&
        v->moving_press >= TOUCAN_FORCE_PRESS_MIN + TOUCAN_FORCE_MOVING_OFFSET &&
        v->moving_press <= TOUCAN_FORCE_PRESS_MAX + TOUCAN_FORCE_MOVING_OFFSET &&
        v->release < v->lock && v->lock < v->press &&
        v->moving_lock >= v->lock && v->moving_press >= v->press &&
        v->moving_lock < v->moving_press;
}
/* A rejected operation leaves all levels intact; no wrapping or partial edit.
 * Moving levels keep their existing offset from stationary levels. */
static inline bool toucan_force_levels_adjust(struct toucan_force_levels *v,
                                               uint32_t command, uint32_t amount) {
    if (command > FORCE_RELEASE_DOWN || amount == 0 || amount > 2000) { return false; }
    int32_t delta = (command & 1) ? -(int32_t)amount : (int32_t)amount;
    int32_t lock=v->lock, press=v->press, release=v->release;
    int32_t moving_lock=v->moving_lock, moving_press=v->moving_press;
    switch(command/2) {
    case 0: lock+=delta; moving_lock+=delta; break;
    case 1: press+=delta; moving_press+=delta; break;
    case 2: release+=delta; break;
    }
    if (lock<1 || press<1 || release<1 || moving_lock<1 || moving_press<1 ||
        lock>UINT16_MAX || press>UINT16_MAX || release>UINT16_MAX ||
        moving_lock>UINT16_MAX || moving_press>UINT16_MAX) { return false; }
    struct toucan_force_levels next={.lock=lock,.press=press,.release=release,
        .moving_lock=moving_lock,.moving_press=moving_press};
    if (!toucan_force_levels_valid(&next)) { return false; }
    *v=next;
    return true;
}

void toucan_force_levels_get(struct toucan_force_levels *levels);
int toucan_force_levels_command(uint32_t command, uint32_t amount);
void toucan_force_levels_contact(bool touching);
void toucan_force_levels_publish(void);
