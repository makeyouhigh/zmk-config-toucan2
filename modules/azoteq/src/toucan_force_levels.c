/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <toucan/force_levels.h>

LOG_MODULE_REGISTER(toucan_force_levels, CONFIG_ZMK_LOG_LEVEL);
#define PAD DT_NODELABEL(tps43_trackpad)
#define DEFAULT_LEVELS { .lock=DT_PROP(PAD,force_lock_level), .press=DT_PROP(PAD,force_press_level), \
    .release=DT_PROP(PAD,force_release_level), .moving_lock=DT_PROP(PAD,force_moving_lock_level), \
    .moving_press=DT_PROP(PAD,force_moving_press_level) }
static const struct toucan_force_levels defaults=DEFAULT_LEVELS;
static struct toucan_force_levels levels=DEFAULT_LEVELS;
static struct k_spinlock levels_lock;
static bool contact;
struct saved_levels { uint16_t version; struct toucan_force_levels levels; };
_Static_assert(sizeof(struct saved_levels)==12,"Settings layout changed");

void toucan_force_levels_get(struct toucan_force_levels *out) {
    k_spinlock_key_t key=k_spin_lock(&levels_lock); *out=levels; k_spin_unlock(&levels_lock,key);
}
void toucan_force_levels_contact(bool touching) {
    k_spinlock_key_t key=k_spin_lock(&levels_lock); contact=touching; k_spin_unlock(&levels_lock,key);
}
#if IS_ENABLED(CONFIG_SETTINGS)
static void save_work(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(save_pending,save_work);
static void save_work(struct k_work *work) {
    ARG_UNUSED(work);
    struct saved_levels saved={.version=1};
    k_spinlock_key_t key=k_spin_lock(&levels_lock);
    bool busy=contact; saved.levels=levels;
    k_spin_unlock(&levels_lock,key);
    if (busy) { k_work_reschedule(&save_pending,K_SECONDS(1)); return; }
    int err=settings_save_one("toucan_force/levels",&saved,sizeof(saved));
    if (err) { LOG_ERR("Force levels save failed: %d",err); }
}
static int settings_set(const char *name, size_t len, settings_read_cb read_cb, void *arg) {
    if (strcmp(name,"levels")) { return -ENOENT; }
    if (len!=sizeof(struct saved_levels)) { return -EINVAL; }
    struct saved_levels saved;
    int bytes=read_cb(arg,&saved,sizeof(saved));
    if (bytes<0) { return bytes; }
    if ((size_t)bytes!=sizeof(saved) || saved.version!=1 || !toucan_force_levels_valid(&saved.levels)) {
        return -EINVAL;
    }
    k_spinlock_key_t key=k_spin_lock(&levels_lock); levels=saved.levels; k_spin_unlock(&levels_lock,key);
    return 0;
}
SETTINGS_STATIC_HANDLER_DEFINE(toucan_force,"toucan_force",NULL,settings_set,NULL,NULL);
#endif

int toucan_force_levels_command(uint32_t command,uint32_t amount) {
    k_spinlock_key_t key=k_spin_lock(&levels_lock);
    bool changed;
    if(command==FORCE_RESET && amount==0) { levels=defaults; changed=true; }
    else { changed=toucan_force_levels_adjust(&levels,command,amount); }
    k_spin_unlock(&levels_lock,key);
    if (!changed) { LOG_WRN("Rejected force level change %u/%u",command,amount); return -EINVAL; }
#if IS_ENABLED(CONFIG_SETTINGS)
    /* Only explicit changes schedule a write. No flash writes on sensor frames. */
    k_work_reschedule(&save_pending,K_SECONDS(1));
#endif
    return 0;
}
