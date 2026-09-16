/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <toucan/force_display.h>

struct toucan_touch_lease {
    uint8_t state;
    bool left_down;
    bool release_pending;
    int64_t updated_ms;
};

static inline void toucan_touch_lease_update(struct toucan_touch_lease *lease,
                                             int64_t now, uint8_t state) {
    if (lease->state == TOUCAN_TOUCH_PRESSED && state != TOUCAN_TOUCH_PRESSED &&
        lease->left_down) {
        lease->release_pending = true;
    }
    lease->state = state;
    lease->updated_ms = now;
}

static inline void toucan_touch_lease_button(struct toucan_touch_lease *lease,
                                             int64_t now, bool down) {
    lease->left_down = down;
    if (!down) {
        lease->release_pending = false;
    }
    lease->updated_ms = now;
}

static inline bool toucan_touch_lease_expire(struct toucan_touch_lease *lease,
                                             int64_t now, uint16_t timeout_ms) {
    if (now - lease->updated_ms < timeout_ms) {
        return false;
    }
    lease->state = TOUCAN_TOUCH_NONE;
    lease->release_pending |= lease->left_down;
    return true;
}
