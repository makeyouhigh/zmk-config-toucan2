/* SPDX-License-Identifier: MIT */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/sys/atomic.h>
#include <zmk/behavior.h>
#include <zephyr/sys/iterable_sections.h>
#include <zmk/split/transport/central.h>
#include <toucan/force_status.h>

static struct k_spinlock status_lock;
static struct toucan_force_status_rx receiver;
static struct toucan_force_levels received_levels;
static bool available;
static bool connected;
static uint32_t revision;
static atomic_t request_attempts;
static void request_work(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(status_request_work,request_work);

/* This runs on the central half. Peripheral-role connection events are not
 * raised here. Read the central transport's local status without radio I/O. */
static bool split_link_available(void) {
    STRUCT_SECTION_FOREACH(zmk_split_transport_central,transport) {
        if (!transport->api || !transport->api->get_status) continue;
        struct zmk_split_transport_status status=transport->api->get_status();
        if (status.available && status.enabled &&
            status.connections!=ZMK_SPLIT_TRANSPORT_CONNECTIONS_STATUS_DISCONNECTED) return true;
    }
    return false;
}

/* Caller holds status_lock. A new link invalidates the previous snapshot. */
static bool update_connection(bool link) {
    if (connected==link) return false;
    connected=link;
    available=false;
    receiver=(struct toucan_force_status_rx){0};
    revision++;
    return link;
}

static void receive_status(struct input_event *event) {
    if (event->type != INPUT_EV_ABS || event->code<TOUCAN_FORCE_STATUS_CODE ||
        event->code>=TOUCAN_FORCE_STATUS_CODE+TOUCAN_FORCE_STATUS_PARTS) return;
    bool link=split_link_available();
    k_spinlock_key_t key=k_spin_lock(&status_lock);
    update_connection(link);
    if (connected && toucan_force_status_receive(&receiver,event->code,event->value,&received_levels)) {
        available=true;
        revision++;
    }
    k_spin_unlock(&status_lock,key);
}
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(trackpad_split)),receive_status);

bool toucan_force_status_get(struct toucan_force_levels *out, uint32_t *out_revision) {
    bool link=split_link_available();
    k_spinlock_key_t key=k_spin_lock(&status_lock);
    bool new_link=update_connection(link);
    *out=received_levels;
    *out_revision=revision;
    bool valid=available;
    k_spin_unlock(&status_lock,key);
    if (new_link) {
        atomic_set(&request_attempts,0);
        k_work_reschedule(&status_request_work,K_MSEC(30));
    }
    return valid;
}

static void request_work(struct k_work *work) {
    ARG_UNUSED(work);
    bool link=split_link_available();
    k_spinlock_key_t key=k_spin_lock(&status_lock);
    update_connection(link);
    bool ready=available;
    k_spin_unlock(&status_lock,key);
    if (!link || (atomic_get(&request_attempts)>0 && ready)) return;
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
