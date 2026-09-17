"""Exercise the production central LCD callbacks with a fake local transport."""
from pathlib import Path
import subprocess
import sys
import tempfile

root=Path(__file__).resolve().parents[1]
environment=r'''
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define ARG_UNUSED(x) (void)(x)
#define K_MSEC(x) (x)
struct k_spinlock { int unused; };
typedef int k_spinlock_key_t;
static inline int k_spin_lock(struct k_spinlock *s) { (void)s; return 0; }
static inline void k_spin_unlock(struct k_spinlock *s,int k) { (void)s; (void)k; }
struct k_work { int unused; };
struct k_work_delayable { struct k_work work; };
#define K_WORK_DELAYABLE_DEFINE(name,callback) static struct k_work_delayable name
static int schedules;
static inline int k_work_schedule(struct k_work_delayable *w,int delay) { (void)w;(void)delay; schedules++;return 0; }
static inline int k_work_reschedule(struct k_work_delayable *w,int delay) { return k_work_schedule(w,delay); }
static inline int64_t k_uptime_get(void) { return 1234; }
typedef int atomic_t;
static inline int atomic_get(atomic_t *v) { return *v; }
static inline void atomic_set(atomic_t *v,int next) { *v=next; }
static inline int atomic_inc(atomic_t *v) { return (*v)++; }
#define DT_NODELABEL(n) 0
#define DEVICE_DT_GET(n) NULL
#define DEVICE_DT_NAME(n) "fcfg"
#define INPUT_EV_ABS 3
struct input_event { uint8_t type; uint16_t code; int32_t value; bool sync; };
#define INPUT_CALLBACK_DEFINE(device,callback)
struct zmk_behavior_binding { const char *behavior_dev; uint32_t param1,param2; };
struct zmk_behavior_binding_event { int layer; int64_t timestamp; };
static int requests;
static struct zmk_behavior_binding requested;
static inline int zmk_behavior_invoke_binding(const struct zmk_behavior_binding *b,
    struct zmk_behavior_binding_event e,bool pressed) {
    (void)e; (void)pressed; requested=*b;requests++;return 0;
}
#define ZMK_SPLIT_TRANSPORT_CONNECTIONS_STATUS_DISCONNECTED 0
struct zmk_split_transport_status { bool available,enabled; int connections; };
struct zmk_split_transport_central_api { struct zmk_split_transport_status (*get_status)(void); };
struct zmk_split_transport_central { const struct zmk_split_transport_central_api *api; };
static struct zmk_split_transport_status fake_link={true,true,0};
static unsigned status_reads;
static struct zmk_split_transport_status fake_get_status(void) { status_reads++;return fake_link; }
static const struct zmk_split_transport_central_api fake_api={fake_get_status};
static struct zmk_split_transport_central fake_transport={&fake_api};
#define STRUCT_SECTION_FOREACH(type,name) for (struct type *name=&fake_transport; name; name=NULL)
'''
test=r'''
#include <assert.h>
#include <string.h>
#include "modules/azoteq/src/toucan_force_status.c"
static void send_levels(const struct toucan_force_levels *v,unsigned generation) {
    for (unsigned i=0;i<3;i++) {
        struct input_event event={INPUT_EV_ABS,TOUCAN_FORCE_STATUS_CODE+i,
            toucan_force_status_pack(v,generation,i),false};
        receive_status(&event);
    }
}
int main(void) {
    struct toucan_force_levels v={2750,3500,2200,3250,4000},out={0}; uint32_t rev;
    assert(!toucan_force_status_get(&out,&rev));
    /* A connected central receives the snapshot WITHOUT a peripheral-role event. */
    fake_link.connections=1;
    send_levels(&v,1);
    assert(toucan_force_status_get(&out,&rev) && memcmp(&out,&v,sizeof(v))==0);
    toucan_force_status_request();
    assert(!toucan_force_status_get(&out,&rev));
    request_work(NULL);
    assert(requests==1 && requested.param1==FORCE_READ && requested.param2==0);
    assert(strcmp(requested.behavior_dev,"fcfg")==0);
    send_levels(&v,2); request_work(NULL); assert(requests==1);
    toucan_force_status_pending(); v.press=3400;v.moving_press=3900;
    assert(!toucan_force_status_get(&out,&rev));
    send_levels(&v,3); assert(toucan_force_status_get(&out,&rev) && out.press==3400);
    fake_link.connections=0;
    assert(!toucan_force_status_get(&out,&rev));
    send_levels(&v,4); assert(!toucan_force_status_get(&out,&rev));
    int before=requests; request_work(NULL); assert(requests==before);
    fake_link.connections=1;
    int pending=schedules;
    assert(!toucan_force_status_get(&out,&rev) && schedules==pending+1);
    request_work(NULL); send_levels(&v,5); assert(toucan_force_status_get(&out,&rev));
    /* Touch-state traffic must not perform transport queries per sensor frame. */
    unsigned reads=status_reads;
    struct input_event touch={INPUT_EV_ABS,0x18,1,false};
    for (int i=0;i<1000;i++) receive_status(&touch);
    assert(status_reads==reads);
    toucan_force_status_request(); before=schedules;
    request_work(NULL); request_work(NULL); request_work(NULL);
    assert(schedules==before+2); /* exactly three total requests, two retries */
    fake_link.enabled=false;
    assert(!toucan_force_status_get(&out,&rev));
    return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    temp=Path(d)
    (temp/'test_environment.h').write_text(environment)
    for name in ['zephyr/kernel.h','zephyr/device.h','zephyr/input/input.h',
                 'zephyr/sys/atomic.h','zephyr/sys/iterable_sections.h',
                 'zmk/behavior.h','zmk/split/transport/central.h']:
        p=temp/name;p.parent.mkdir(parents=True,exist_ok=True)
        p.write_text('#include "test_environment.h"\n')
    (temp/'test.c').write_text(test)
    executable=temp/('test.exe' if sys.platform=='win32' else 'test')
    command=(sys.argv[1:] or ['cc'])+['-std=c11','-Wall','-Wextra','-Werror',
        '-I'+str(temp),'-I'+str(root),'-I'+str(root/'modules/azoteq/include'),
        str(temp/'test.c'),'-o',str(executable)]
    if sys.platform!='win32': command+=['-fsanitize=address,undefined']
    subprocess.run(command,check=True)
    subprocess.run([str(executable)],check=True)
print('Production LCD receive/query/reconnect callbacks passed without peripheral events')
