/* SPDX-License-Identifier: MIT */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/sys/atomic.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <toucan/force_status.h>

static struct k_spinlock status_lock;
static struct toucan_force_status_rx receiver;
static struct toucan_force_levels received_levels;
static bool available;
static uint32_t revision;
static atomic_t request_attempts;
static void request_work(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(status_request_work,request_work);

static void receive_status(struct input_event *event) {
    if (event->type != INPUT_EV_ABS) return;
    k_spinlock_key_t key=k_spin_lock(&status_lock);
    if (toucan_force_status_receive(&receiver,event->code,event->value,&received_levels)) {
        available=true;
        revision++;
    }
    k_spin_unlock(&status_lock,key);
}
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(trackpad_split)),receive_status);

bool toucan_force_status_get(struct toucan_force_levels *out, uint32_t *out_revision) {
    k_spinlock_key_t key=k_spin_lock(&status_lock);
    *out=received_levels;
    *out_revision=revision;
    bool valid=available;
    k_spin_unlock(&status_lock,key);
    return valid;
}

static void request_work(struct k_work *work) {
    ARG_UNUSED(work);
    struct toucan_force_levels current;
    uint32_t current_revision;
    if (toucan_force_status_get(&current,&current_revision)) return;
    const struct zmk_behavior_binding binding={
        .behavior_dev=DEVICE_DT_NAME(DT_NODELABEL(force_cfg)),.param1=FORCE_READ,.param2=0};
    const struct zmk_behavior_binding_event event={.layer=TOUCAN_FORCE_SYS_LAYER,.timestamp=k_uptime_get()};
    zmk_behavior_invoke_binding(&binding,event,true);
    if (atomic_inc(&request_attempts) < 2) k_work_schedule(&status_request_work,K_MSEC(250));
}

void toucan_force_status_pending(void) {
    k_spinlock_key_t key=k_spin_lock(&status_lock);
    available=false;
    receiver=(struct toucan_force_status_rx){0};
    revision++;
    k_spin_unlock(&status_lock,key);
}
void toucan_force_status_request(void) {
    toucan_force_status_pending();
    atomic_set(&request_attempts,0);
    k_work_reschedule(&status_request_work,K_MSEC(30));
}

static int connection_changed(const zmk_event_t *eh) {
    const struct zmk_split_peripheral_status_changed *event=as_zmk_split_peripheral_status_changed(eh);
    if (!event) return ZMK_EV_EVENT_BUBBLE;
    if (event->connected) {
        toucan_force_status_request();
    } else {
        k_work_cancel_delayable(&status_request_work);
        toucan_force_status_pending();
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(toucan_force_status,connection_changed);
ZMK_SUBSCRIPTION(toucan_force_status,zmk_split_peripheral_status_changed);
