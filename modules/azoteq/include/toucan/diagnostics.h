/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
#include <stdbool.h>

/* Private, non-synchronizing ABS events, never mouse axes. Begin/end sequence
 * markers make a dropped diagnostic event invalidate its whole snapshot. */
#define TOUCAN_DIAG_CODE_BASE 0x30
#define TOUCAN_DIAG_SOURCE_WORDS 11
#define TOUCAN_DIAG_CODE_END (TOUCAN_DIAG_CODE_BASE + TOUCAN_DIAG_SOURCE_WORDS + 1)
#define TOUCAN_DIAG_WORDS 40
struct toucan_diag_source {
    uint32_t values[TOUCAN_DIAG_SOURCE_WORDS], sequence, mask, drops;
    bool collecting;
};
static inline bool toucan_diag_source_step(struct toucan_diag_source *s,
                                            uint16_t code, uint32_t value) {
    if (code == TOUCAN_DIAG_CODE_BASE) {
        s->sequence = value;
        s->mask = 0;
        s->collecting = true;
    } else if (code > TOUCAN_DIAG_CODE_BASE && code < TOUCAN_DIAG_CODE_END &&
               s->collecting) {
        unsigned int i = code - TOUCAN_DIAG_CODE_BASE - 1;
        s->values[i] = value;
        s->mask |= 1U << i;
    } else if (code == TOUCAN_DIAG_CODE_END) {
        bool complete = s->collecting && value == s->sequence &&
                        s->mask == (1U << TOUCAN_DIAG_SOURCE_WORDS) - 1U;
        s->collecting = false;
        if (!complete) { s->drops++; }
        return complete;
    }
    return false;
}
enum toucan_diag_index {
    TD_MAGIC, TD_VERSION, TD_NOW, TD_SOURCE_SEQUENCE, TD_SOURCE_BASE,
    TD_INPUT_EVENTS = TD_SOURCE_BASE + TOUCAN_DIAG_SOURCE_WORDS,
    TD_INPUT_DISTANCE, TD_HID_REPORTS, TD_HID_DISTANCE, TD_SUBMITTED,
    TD_SUBMITTED_DISTANCE, TD_COMPLETED, TD_RETRIES, TD_ERRORS,
    TD_MAX_NOTIFY_MS, TD_MAX_QUEUE_AGE_MS, TD_MAX_QUEUE, TD_QUEUE,
    TD_IN_FLIGHT, TD_HOST_INTERVAL, TD_HOST_LATENCY, TD_LAST_ERROR,
    TD_TOUCH_STATE, TD_BUTTON_STATE, TD_SOURCE_RECEIVED_MS,
    TD_INPUT_MS, TD_SUBMITTED_MS, TD_COMPLETED_MS, TD_MAX_CALLBACK_MS,
    TD_SOURCE_DROPS,
};

#if defined(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) && defined(CONFIG_ZMK_BLE)
struct input_event;
void toucan_diag_input(const struct input_event *event);
void toucan_diag_hid(uint32_t distance, uint8_t pending);
void toucan_diag_tx_start(uint32_t age, uint8_t pending, uint8_t in_flight);
void toucan_diag_tx_result(uint32_t duration, int error, uint32_t distance);
void toucan_diag_complete(uint32_t age, uint8_t in_flight);
#endif
