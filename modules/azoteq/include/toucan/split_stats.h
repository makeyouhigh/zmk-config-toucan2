/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
struct toucan_split_stats {
    uint32_t max_queue_ms, max_notify_ms, max_complete_ms, queued;
};
struct toucan_split_stats toucan_split_stats_get(void);
