"""Compile the production right battery/USB sender; verify bounded, nonblocking reports."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'modules/azoteq/src/toucan_peripheral_status.c').read_text(encoding='utf-8')
source = '\n'.join(s for s in source.splitlines() if not s.startswith('#include'))
test = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <toucan/peripheral_status.h>
typedef int atomic_t;
static void atomic_set(atomic_t *p, int v) { *p = v; }
static int atomic_get(atomic_t *p) { return *p; }
static int atomic_inc(atomic_t *p) { return (*p)++; }
struct k_work { int unused; };
static int schedules, sends, level, queue_rc;
static bool link, powered;
static int32_t packet;
#define ARG_UNUSED(v) (void)(v)
#define K_MSEC(ms) (ms)
#define K_SECONDS(s) ((s) * 1000)
#define K_NO_WAIT 0
#define K_WORK_DELAYABLE_DEFINE(name, cb) static int name
static void k_work_schedule(int *work, int ms) { (void)work; assert(ms == 1000); schedules++; }
static void k_work_reschedule(int *work, int ms) { (void)work; assert(ms == 250); schedules++; }
#define DT_NODELABEL(node) 0
#define DEVICE_DT_GET(node) NULL
static bool zmk_split_bt_peripheral_is_connected(void) { return link; }
static uint8_t zmk_battery_state_of_charge(void) { return level; }
static bool zmk_usb_is_powered(void) { return powered; }
static int input_report_abs(void *dev, int code, int32_t value, bool sync, int timeout) {
    (void)dev; assert(code == TOUCAN_INPUT_PERIPHERAL_STATUS_CODE && !sync && timeout == 0);
    sends++; packet = value; return queue_rc;
}
typedef struct { int kind; } zmk_event_t;
static bool as_zmk_battery_state_changed(const zmk_event_t *event) { return event->kind == 1; }
#define ZMK_EV_EVENT_BUBBLE 0
#define ZMK_LISTENER(...)
#define ZMK_SUBSCRIPTION(...)
''' + source + r'''
int main(void) {
    struct toucan_peripheral_status status;
    for (int n = 0; n <= 100; n++) for (int usb = 0; usb <= 1; usb++) {
        assert(toucan_peripheral_status_decode(toucan_peripheral_status_pack(n, true, usb), &status));
        assert(status.battery == n && status.battery_known && status.usb_powered == usb);
    }
    assert(!toucan_peripheral_status_decode(0, &status));
    assert(!toucan_peripheral_status_decode(-1, &status));
    assert(!toucan_peripheral_status_decode(0x10165, &status));
    assert(!toucan_peripheral_status_decode(0x10001, &status));
    assert(toucan_peripheral_status_decode(toucan_peripheral_status_pack(255, true, true), &status));
    assert(!status.battery_known && status.usb_powered);
    zmk_event_t connected = {0}, battery = {1};
    peripheral_status_event(&connected); publish_peripheral_status(NULL);
    assert(sends == 0 && schedules == 1);
    link = true; schedules = 0;
    peripheral_status_event(&connected);
    publish_peripheral_status(NULL); publish_peripheral_status(NULL); publish_peripheral_status(NULL);
    assert(sends == 3 && schedules == 3); /* One initial schedule, only two retries. */
    assert(toucan_peripheral_status_decode(packet, &status) && !status.battery_known);
    powered = true; level = 74;
    peripheral_status_event(&connected); publish_peripheral_status(NULL);
    assert(toucan_peripheral_status_decode(packet, &status));
    assert(status.battery_known && status.battery == 74 && status.usb_powered);
    powered = false; level = 0;
    peripheral_status_event(&battery); publish_peripheral_status(NULL);
    assert(toucan_peripheral_status_decode(packet, &status));
    assert(status.battery_known && status.battery == 0 && !status.usb_powered);
    queue_rc = -12; schedules = sends = 0;
    peripheral_status_event(&connected);
    publish_peripheral_status(NULL); publish_peripheral_status(NULL); publish_peripheral_status(NULL);
    assert(schedules == 3 && sends == 3); /* No unbounded retry on a saturated queue. */
    link = false; schedules = sends = 0;
    publish_peripheral_status(NULL); assert(schedules == 0 && sends == 0);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    temp = Path(d)
    p = temp / 'test.c'
    p.write_text(test, encoding='utf-8')
    executable = temp / ('test.exe' if sys.platform == 'win32' else 'test')
    cmd = (sys.argv[1:] or ['cc']) + ['-std=c11', '-Wall', '-Wextra', '-Werror',
        '-I' + str(root / 'modules/azoteq/include'), str(p), '-o', str(executable)]
    if sys.platform != 'win32': cmd += ['-fsanitize=undefined,address']
    subprocess.run(cmd, check=True)
    subprocess.run([str(executable)], check=True)
print('Right status sender: battery/USB packet, no mouse sync, bounded reconnect/change retries and nonblocking failure passed')
