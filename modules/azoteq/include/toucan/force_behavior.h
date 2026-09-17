/* SPDX-License-Identifier: MIT */
#pragma once
#include <zephyr/device.h>
#include <zephyr/sys/util.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_BLE)
#include <zmk/split/bluetooth/service.h>
/* The BLE payload includes the terminating NUL. Check the actual device name,
 * not the devicetree label used by the keymap or the human display name. */
#define TOUCAN_FORCE_ASSERT_SPLIT_NAME(node_id) \
    BUILD_ASSERT(sizeof(DEVICE_DT_NAME(node_id)) <= ZMK_SPLIT_RUN_BEHAVIOR_DEV_LEN, \
                 "Force behavior device name exceeds split BLE payload")
#else
#define TOUCAN_FORCE_ASSERT_SPLIT_NAME(node_id)
#endif
