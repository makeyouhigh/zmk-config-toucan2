/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT zmk_behavior_force_config
#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <toucan/force_levels.h>
#include <toucan/force_behavior.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
TOUCAN_FORCE_ASSERT_SPLIT_NAME(DT_DRV_INST(0));
static int pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);
#if IS_ENABLED(CONFIG_INPUT_TPS43)
    int err=toucan_force_levels_command(binding->param1,binding->param2);
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
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
#define COMMAND(label,number) {.display_name=label,.type=BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,.value=number}
static const struct behavior_parameter_value_metadata commands[]={
    COMMAND("이동 억제 높이기",FORCE_LOCK_UP), COMMAND("이동 억제 낮추기",FORCE_LOCK_DOWN),
    COMMAND("클릭 높이기",FORCE_CLICK_UP), COMMAND("클릭 낮추기",FORCE_CLICK_DOWN),
    COMMAND("해제 높이기",FORCE_RELEASE_UP), COMMAND("해제 낮추기",FORCE_RELEASE_DOWN),
};
static const struct behavior_parameter_value_metadata amounts[]={
    {.display_name="조절량 (센서 단위)",.type=BEHAVIOR_PARAMETER_VALUE_TYPE_RANGE,.range={.min=1,.max=2000}},
};
static const struct behavior_parameter_value_metadata resets[]={COMMAND("초기 설정 복원",FORCE_RESET)};
static const struct behavior_parameter_value_metadata zero[]={COMMAND("0",0)};
static const struct behavior_parameter_metadata_set sets[]={
    {.param1_values=commands,.param1_values_len=ARRAY_SIZE(commands),
     .param2_values=amounts,.param2_values_len=ARRAY_SIZE(amounts)},
    {.param1_values=resets,.param1_values_len=ARRAY_SIZE(resets),
     .param2_values=zero,.param2_values_len=ARRAY_SIZE(zero)},
};
static const struct behavior_parameter_metadata metadata={.sets=sets,.sets_len=ARRAY_SIZE(sets)};
#endif
static const struct behavior_driver_api api={
    .binding_pressed=pressed,.binding_released=released,.locality=BEHAVIOR_LOCALITY_GLOBAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata=&metadata,
#endif
};
BEHAVIOR_DT_INST_DEFINE(0,NULL,NULL,NULL,NULL,POST_KERNEL,CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,&api);
#endif
