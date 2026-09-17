"""Exercise production telemetry throttling and its touch-layer consumer."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]

def function(path, signature):
    source = (root / path).read_text(encoding='utf-8')
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

source = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <toucan/force_display.h>
#include <toucan/touch_lease.h>
#define K_NO_WAIT 0
#define INPUT_EV_ABS 3
#define INPUT_EV_KEY 1
#define INPUT_BTN_TOUCH 330
#define INPUT_BTN_0 272
#define MAX(a,b) ((a)>(b)?(a):(b))
struct tps43_drv_data { int64_t force_display_report_ms; uint8_t force_display_state; };
struct device { struct tps43_drv_data *data; };
struct input_event { int type,code; int32_t value; };
static int64_t now;
static int sends,scheduled,fail;
static int32_t payload;
static int64_t k_uptime_get(void) { return now; }
static int input_report_abs(const struct device *d,int code,int32_t v,bool sync,int timeout) {
    (void)d; assert(code==TOUCAN_INPUT_TOUCH_STATE_CODE && !sync && timeout==K_NO_WAIT);
    sends++;payload=v;return fail ? -1 : 0;
}
typedef int k_spinlock_key_t;
static int lease_lock,touch_guard_work;
static struct toucan_touch_lease lease;
static int k_spin_lock(int *lock) { (void)lock;return 0; }
static void k_spin_unlock(int *lock,int key) { (void)lock;(void)key; }
static int k_work_reschedule(int *work,int delay) { (void)work;assert(delay==K_NO_WAIT);scheduled++;return 0; }
'''
source += function('modules/azoteq/drivers/input/tps43.c',
                   'static void tps43_force_display_report(')
source += '\n' + function('modules/azoteq/src/toucan_touch_guard.c',
                          'static void touch_guard_input(')
source += r'''
int main(void) {
    struct toucan_force_display_sample sample;
    for (unsigned v=0;v<=65535;v++) {
        for (unsigned state=1;state<=2;state++) {
            assert(toucan_force_display_decode(toucan_force_display_pack(state,v,true),&sample));
            assert(sample.state==state && sample.strength==v && sample.known);
        }
    }
    assert(toucan_force_display_decode(1,&sample) && sample.state==1 && !sample.known);
    assert(!toucan_force_display_decode(-1,&sample));
    assert(!toucan_force_display_decode(3,&sample));
    assert(!toucan_force_display_decode(0x1fffff,&sample));
    assert(toucan_force_display_decode(toucan_force_display_pack(0,5000,true),&sample));
    assert(sample.strength==0 && sample.known);
    char text[5];
    toucan_force_display_text(text,0,true);assert(!strcmp(text,"0000"));
    toucan_force_display_text(text,42,true);assert(!strcmp(text,"0042"));
    toucan_force_display_text(text,9999,true);assert(!strcmp(text,"9999"));
    toucan_force_display_text(text,10000,true);assert(!strcmp(text,"OVER"));
    toucan_force_display_text(text,3500,false);assert(!strcmp(text,"----"));
    struct tps43_drv_data data={0}; struct device dev={&data};
    for (int i=0;i<1000;i++) { now=i*8;tps43_force_display_report(&dev,1,2000+i,true); }
    assert(sends>=31 && sends<=33); /* 125 Hz values do not cause 125 Hz reports */
    int before=sends;now++;
    tps43_force_display_report(&dev,2,4500,true); assert(sends==before+1);
    assert(toucan_force_display_decode(payload,&sample) && sample.strength==4500 && sample.state==2);
    tps43_force_display_report(&dev,1,3300,true); assert(sends==before+2);
    struct input_event event={INPUT_EV_ABS,TOUCAN_INPUT_TOUCH_STATE_CODE,payload};
    touch_guard_input(&event);
    assert(lease.state==TOUCAN_TOUCH_CONTACT); /* Never clamp packed strength to PRESSED. */
    event.value=toucan_force_display_pack(2,5000,true); touch_guard_input(&event);
    assert(lease.state==TOUCAN_TOUCH_PRESSED);
    lease.left_down=true;
    event.value=toucan_force_display_pack(1,3200,true);touch_guard_input(&event);
    assert(lease.state==1 && lease.release_pending);
    event.value=toucan_force_display_pack(0,0,true);touch_guard_input(&event);
    assert(lease.state==0);
    before=scheduled;event.value=-1;touch_guard_input(&event);assert(scheduled==before);
    fail=1;now++;tps43_force_display_report(&dev,2,4700,true);
    assert(data.force_display_state==1); /* Failed display send never commits state. */
    return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    directory=Path(d)
    test=directory/'display.c';test.write_text(source,encoding='utf-8')
    executable=directory/('display.exe' if sys.platform=='win32' else 'display')
    command=(sys.argv[1:] or ['cc'])+['-std=c11','-Wall','-Wextra','-Werror',
        '-I'+str(root/'modules/azoteq/include'),str(test),'-o',str(executable)]
    if sys.platform!='win32': command+=['-fsanitize=address,undefined']
    subprocess.run(command,check=True)
    subprocess.run([str(executable)],check=True)
print('Live strength packet, production throttle, touch-layer decode and four-cell display passed')
