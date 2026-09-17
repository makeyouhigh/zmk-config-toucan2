/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Two slots cover a completion callback arriving one connection event late.
 * Keep this small: sensor samples merge outside the Bluetooth stack instead
 * of creating a long replay queue. Caller serializes access. */
#define TOUCAN_MOUSE_IN_FLIGHT 2U
struct toucan_mouse_flight {
    uintptr_t peer;
    uint32_t token;
};
struct toucan_mouse_window {
    struct toucan_mouse_flight slots[TOUCAN_MOUSE_IN_FLIGHT];
    uint32_t generation;
};
static inline bool toucan_mouse_window_available(const struct toucan_mouse_window *w) {
    for (unsigned int i = 0; i < TOUCAN_MOUSE_IN_FLIGHT; i++) {
        if (!w->slots[i].token) { return true; }
    }
    return false;
}
static inline uint32_t toucan_mouse_window_acquire(struct toucan_mouse_window *w,
                                                  uintptr_t peer) {
    for (unsigned int i = 0; i < TOUCAN_MOUSE_IN_FLIGHT; i++) {
        if (w->slots[i].token) { continue; }
        w->generation = (w->generation + 1U) & 0x3fffffffU;
        if (!w->generation) { w->generation = 1; }
        uint32_t token = (w->generation << 1) | i;
        w->slots[i] = (struct toucan_mouse_flight){.peer = peer, .token = token};
        return token;
    }
    return 0;
}
static inline bool toucan_mouse_window_complete(struct toucan_mouse_window *w, uint32_t token) {
    if (!token) { return false; }
    unsigned int i = token & 1U;
    if (w->slots[i].token != token) { return false; }
    w->slots[i] = (struct toucan_mouse_flight){0};
    return true;
}
static inline void toucan_mouse_window_disconnect(struct toucan_mouse_window *w, uintptr_t peer) {
    for (unsigned int i = 0; i < TOUCAN_MOUSE_IN_FLIGHT; i++) {
        if (w->slots[i].peer == peer) {
            w->slots[i] = (struct toucan_mouse_flight){0};
        }
    }
    /* Keep generation: callbacks from a disconnected link may arrive late. */
}
