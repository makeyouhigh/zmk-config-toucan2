/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT zmk_behavior_force_adjust
#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <toucan/force_levels.h>
#include <toucan/force_behavior.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
struct force_adjust_config { uint32_t command, amount; };

static int pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);
#if IS_ENABLED(CONFIG_INPUT_TPS43)
    const struct device *dev=zmk_behavior_get_binding(binding->behavior_dev);
    if (!dev) { return -ENODEV; }
    const struct force_adjust_config *config=dev->config;
    int err=toucan_force_levels_command(config->command,config->amount);
    if (err) { return err; }
#else
    ARG_UNUSED(binding);
#endif
    return ZMK_BEHAVIOR_OPAQUE;
}

static int released(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding); ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api api={
    .binding_pressed=pressed,.binding_released=released,.locality=BEHAVIOR_LOCALITY_GLOBAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata=zmk_behavior_get_empty_param_metadata,
#endif
};

#define FORCE_ADJUST_INST(n) \
    TOUCAN_FORCE_ASSERT_SPLIT_NAME(DT_DRV_INST(n)); \
    BUILD_ASSERT(DT_INST_PROP(n,amount)>0 && DT_INST_PROP(n,amount)<=2000, "Invalid force step"); \
    static const struct force_adjust_config config_##n={ \
        .command=DT_INST_PROP(n,command),.amount=DT_INST_PROP(n,amount)}; \
    BEHAVIOR_DT_INST_DEFINE(n,NULL,NULL,NULL,&config_##n,POST_KERNEL, \
                           CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,&api);
DT_INST_FOREACH_STATUS_OKAY(FORCE_ADJUST_INST)
#endif
