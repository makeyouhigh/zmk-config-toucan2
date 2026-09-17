/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zmk/split/transport/types.h>
#include <zmk/split/bluetooth/service.h>
#include <zmk/split/bluetooth/uuid.h>
#include <toucan/split_queue.h>
#include <toucan/mouse_window.h>
#include <toucan/split_stats.h>

/* Pinned ZMK v0.3 input characteristic, preserving the existing wire format
 * and left firmware. Only reg zero (Toucan trackpad) takes this path. */
extern const struct bt_gatt_service_static split_svc;
int __real_zmk_split_peripheral_report_event(const struct zmk_split_transport_peripheral_event *ev);
BUILD_ASSERT(TS_X == INPUT_REL_X && TS_Y == INPUT_REL_Y && TS_REL == INPUT_EV_REL);
BUILD_ASSERT(TS_BTN0 == INPUT_BTN_0 && TS_TOUCH == INPUT_BTN_TOUCH);
static struct toucan_split_queue queue;
static struct toucan_mouse_window window;
static struct k_spinlock lock;
static struct bt_conn *peer;
static struct toucan_split_stats stats;
static uint32_t flight_ms[TOUCAN_MOUSE_IN_FLIGHT];
K_SEM_DEFINE(pending,0,1);

struct toucan_split_stats toucan_split_stats_get(void) {
    k_spinlock_key_t key=k_spin_lock(&lock);
    struct toucan_split_stats snapshot=stats;
    snapshot.queued=queue.count;
    k_spin_unlock(&lock,key);
    return snapshot;
}

int __wrap_zmk_split_peripheral_report_event(const struct zmk_split_transport_peripheral_event *ev) {
    if (ev->type != ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_INPUT_EVENT ||
        ev->data.input_event.reg != 0) {
        return __real_zmk_split_peripheral_report_event(ev);
    }
    k_spinlock_key_t key=k_spin_lock(&lock);
    if (!peer) { k_spin_unlock(&lock,key); return -ENOTCONN; }
    struct toucan_split_event e = {ev->data.input_event.type,ev->data.input_event.code,
                                   ev->data.input_event.value,ev->data.input_event.sync != 0};
    toucan_split_push(&queue,e,k_uptime_get());
    k_spin_unlock(&lock,key);
    k_sem_give(&pending);
    return 0;
}
static void connected(struct bt_conn *conn, uint8_t err) {
    if (err) { return; }
    struct bt_conn_info info;
    if (bt_conn_get_info(conn,&info) || info.role != BT_CONN_ROLE_PERIPHERAL) { return; }
    k_spinlock_key_t key=k_spin_lock(&lock);
    if (!peer) { peer=bt_conn_ref(conn); }
    k_spin_unlock(&lock,key);
    k_sem_give(&pending);
}
static void disconnected(struct bt_conn *conn, uint8_t reason) {
    ARG_UNUSED(reason);
    struct bt_conn *release=NULL;
    k_spinlock_key_t key=k_spin_lock(&lock);
    if (peer==conn) { release=peer; peer=NULL; queue=(struct toucan_split_queue){0}; }
    toucan_mouse_window_disconnect(&window,(uintptr_t)conn);
    k_spin_unlock(&lock,key);
    if (release) { bt_conn_unref(release); }
    k_sem_give(&pending);
}
BT_CONN_CB_DEFINE(toucan_split_callbacks)={.connected=connected,.disconnected=disconnected};
static void sent(struct bt_conn *conn, void *token) {
    ARG_UNUSED(conn);
    k_spinlock_key_t key=k_spin_lock(&lock);
    bool completed=toucan_mouse_window_complete(&window,(uint32_t)(uintptr_t)token);
    if (completed) {
        stats.max_complete_ms=MAX(stats.max_complete_ms,
            k_uptime_get_32()-flight_ms[(uint32_t)(uintptr_t)token & 1U]);
    }
    k_spin_unlock(&lock,key);
    if (completed) { k_sem_give(&pending); }
}
static const struct bt_gatt_attr *input_attribute(void) {
    for (size_t i=0; i+2<split_svc.attr_count; i++) {
        if (bt_uuid_cmp(split_svc.attrs[i].uuid,
            BT_UUID_DECLARE_128(ZMK_SPLIT_BT_INPUT_EVENT_UUID))==0 &&
            (uintptr_t)split_svc.attrs[i+2].user_data==0) { return &split_svc.attrs[i]; }
    }
    return NULL;
}
static void sender(void *a,void *b,void *c) {
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    const struct bt_gatt_attr *attr=input_attribute();
    if (!attr) { return; }
    for (;;) {
        struct toucan_split_event event;
        int64_t event_ms=0;
        uint32_t token=0;
        struct bt_conn *conn=NULL;
        k_spinlock_key_t key=k_spin_lock(&lock);
        if (peer && toucan_mouse_window_available(&window) &&
            toucan_split_pop(&queue,&event,&event_ms,k_uptime_get())) {
            conn=bt_conn_ref(peer);
            token=toucan_mouse_window_acquire(&window,(uintptr_t)conn);
            flight_ms[token & 1U]=k_uptime_get_32();
            stats.max_queue_ms=MAX(stats.max_queue_ms,(uint32_t)(k_uptime_get()-event_ms));
        }
        k_spin_unlock(&lock,key);
        if (!conn) { k_sem_take(&pending,K_FOREVER); continue; }
        /* Retry this one event in place to preserve button order. The input
         * thread remains free and newer movement coalesces outside BLE. */
        for (;;) {
            struct bt_conn_info info;
            if (bt_conn_get_info(conn,&info) || info.state!=BT_CONN_STATE_CONNECTED) { break; }
            if (event.type==TS_REL && k_uptime_get()-event_ms>TOUCAN_SPLIT_MAX_AGE_MS) {
                event.value=0;
            }
            struct zmk_split_input_event_payload payload={.type=event.type,.code=event.code,
                .value=(uint32_t)event.value,.sync=event.sync};
            struct bt_gatt_notify_params params={.attr=attr,.data=&payload,.len=sizeof(payload),
                .func=sent,.user_data=(void *)(uintptr_t)token};
            uint32_t started=k_uptime_get_32();
            int err=bt_gatt_notify_cb(conn,&params);
            key=k_spin_lock(&lock);
            stats.max_notify_ms=MAX(stats.max_notify_ms,k_uptime_get_32()-started);
            k_spin_unlock(&lock,key);
            if (!err) { token=0; break; }
            if (err==-EPERM) { bt_conn_set_security(conn,BT_SECURITY_L2); }
            if (err!=-ENOMEM && err!=-EAGAIN && err!=-EPERM && err!=-EINVAL) { break; }
            /* EINVAL can mean the split subscription is not ready yet. */
            k_sleep(K_MSEC(5));
        }
        if (token) {
            key=k_spin_lock(&lock);
            toucan_mouse_window_complete(&window,token);
            k_spin_unlock(&lock,key);
        }
        bt_conn_unref(conn);
    }
}
K_THREAD_DEFINE(toucan_split_sender,2048,sender,NULL,NULL,NULL,
                CONFIG_ZMK_BLE_THREAD_PRIORITY,0,0);
