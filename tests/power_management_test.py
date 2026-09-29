"""Compile production power transitions and the generated activity callback.

Mock only Zephyr/I2C boundaries; inject pending wake work and bus/GPIO failures.
This verifies firmware ordering, not physical current or battery capacity.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
driver = (root / 'modules/azoteq/drivers/input/tps43.c').read_text(encoding='utf-8')


def function(name):
    match = re.search(r'(?:static )?(?:int|void) ' + name + r'\([^;]*?\) \{', driver)
    assert match, name
    end = driver.index('\n}', match.start()) + 2
    return driver[match.start():end]


assert 'PM_DEVICE_DT_INST_DEFINE(inst, tps43_pm_action)' in driver
assert 'PM_DEVICE_DT_INST_GET(inst)' in driver
cmake = (root / 'modules/azoteq/activity_filter.cmake').read_text(encoding='utf-8')
callback = cmake.split('set(toucan_activity_new [=[', 1)[1].split(']=])', 1)[0]
source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#define INPUT_EV_ABS 3
#define INPUT_EV_REL 2
#define INPUT_EV_KEY 1
#include <toucan/input_activity.h>
#define LOG_INF(...) ((void)0)
#define LOG_WRN(...) ((void)0)
#define LOG_ERR(...) ((void)0)
#define K_MSEC(ms) (ms)
#define TPS43_REG_SYSTEM_CONTROL_1 0x432
#define TPS43_SUSPEND 1
#define GPIO_INT_DISABLE 0
#define GPIO_INT_EDGE_TO_ACTIVE 1
#define CONTAINER_OF(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
struct k_work { int unused; };
struct k_work_sync { int unused; };
struct device;
struct tps43_drv_data {
    const struct device *dev;
    bool suspended;
    int lock, requested_sleep, work_q;
    struct k_work power_work;
};
struct gpio_dt_spec { const void *port; };
struct tps43_config { bool enable_power_management, force_click; struct gpio_dt_spec rdy_gpio; };
struct device { struct tps43_drv_data *data; const struct tps43_config *config; };
enum pm_device_action { PM_DEVICE_ACTION_SUSPEND, PM_DEVICE_ACTION_RESUME, PM_DEVICE_ACTION_OTHER };
static int read_rc, write_rc, irq_rc, lock_rc, irq, reads, writes, ends, queued, cancelled;
static int finger_cancels, running_wake, calls;
static bool chip_asleep;
static struct tps43_drv_data *pending_data;
static int k_sem_take(int *lock, int timeout) {
    assert(timeout == 100); if (lock_rc) return lock_rc; assert(*lock == 0); *lock = 1; return 0;
}
static void k_sem_give(int *lock) { assert(*lock == 1); *lock = 0; }
static void atomic_set(int *p, int value) { *p = value; }
static int atomic_get(int *p) { return *p; }
static void k_work_cancel_sync(struct k_work *work, struct k_work_sync *sync) {
    (void)work; (void)sync; cancelled++; queued = 0;
    /* A resume already executing must finish before cancellation returns. */
    if (running_wake) { chip_asleep = false; pending_data->suspended = false; running_wake = 0; }
}
static int k_work_submit_to_queue(int *queue, struct k_work *work) {
    (void)queue; (void)work; queued++; return 1;
}
static void tps43_force_cancel_and_report(const struct device *dev) { assert(dev->data->lock); finger_cancels++; }
static int gpio_pin_interrupt_configure_dt(const struct gpio_dt_spec *pin, int mode) {
    assert(pin->port); if (irq_rc) return irq_rc; irq = mode; return 0;
}
static void tps43_force_communication(const struct device *dev) { assert(dev->data->lock); }
static void k_sleep(int delay) { assert(delay == 1); }
static void k_busy_wait(int delay) { assert(delay == 200); }
static int tps43_i2c_read_reg8(const struct device *dev, int reg, uint8_t *value) {
    assert(dev->data->lock && reg == 0x432); reads++; *value = chip_asleep ? 1 : 0; return read_rc;
}
static int tps43_i2c_write_reg8(const struct device *dev, int reg, uint8_t value) {
    assert(dev->data->lock && reg == 0x432); writes++;
    if (write_rc == 0) chip_asleep = (value & 1) != 0;
    return write_rc;
}
static void tps43_end_communication_window(const struct device *dev) { assert(dev->data->lock); ends++; }
''' + '\n'.join(function(n) for n in (
    'tps43_set_suspend_internal', 'tps43_set_suspend', 'tps43_set_power_sync',
    'tps43_pm_action', 'tps43_power_work', 'tps43_set_sleep')) + r'''
struct input_event { uint8_t type; uint16_t code; };
static struct k_work note_activity_work;
static int k_work_submit(struct k_work *work) { assert(work == &note_activity_work); calls++; return 0; }
''' + callback + r'''
int main(void) {
    struct tps43_drv_data data = {0};
    struct tps43_config config = {true, true, {(void *)1}};
    struct device dev = {&data, &config};
    data.dev = &dev; pending_data = &data;
    assert(tps43_set_sleep(NULL, true) == -EINVAL);
    /* Regression: old code returned before the suspend write ever ran. */
    assert(tps43_set_sleep(&dev, true) == 0);
    assert(chip_asleep && data.suspended && !queued && irq == 0 && ends == 1 && data.lock == 0);
    int count = writes;
    assert(tps43_pm_action(&dev, PM_DEVICE_ACTION_SUSPEND) == 0 && writes == count);
    /* A delayed or already running wake cannot survive system suspension. */
    assert(tps43_set_sleep(&dev, false) == 0 && queued == 1 && chip_asleep);
    running_wake = 1;
    assert(tps43_pm_action(&dev, PM_DEVICE_ACTION_SUSPEND) == 0);
    assert(chip_asleep && data.suspended && !queued && !running_wake);
    assert(tps43_pm_action(&dev, PM_DEVICE_ACTION_RESUME) == 0);
    assert(!chip_asleep && !data.suspended && irq == 1 && data.lock == 0);
    assert(tps43_pm_action(&dev, PM_DEVICE_ACTION_OTHER) == -ENOTSUP);
    /* Read failure cannot write a fabricated control value or claim sleep. */
    read_rc = -EIO; count = writes;
    assert(tps43_pm_action(&dev, PM_DEVICE_ACTION_SUSPEND) == -EIO);
    assert(!chip_asleep && !data.suspended && writes == count && irq == 1 && data.lock == 0);
    read_rc = 0; write_rc = -EIO;
    assert(tps43_pm_action(&dev, PM_DEVICE_ACTION_SUSPEND) == -EIO);
    assert(!chip_asleep && !data.suspended && irq == 1);
    write_rc = -ETIMEDOUT; irq_rc = -EINVAL;
    assert(tps43_pm_action(&dev, PM_DEVICE_ACTION_SUSPEND) == -EINVAL);
    irq_rc = 0;
    assert(tps43_pm_action(&dev, PM_DEVICE_ACTION_SUSPEND) == -ETIMEDOUT);
    write_rc = 0; lock_rc = -EBUSY;
    assert(tps43_pm_action(&dev, PM_DEVICE_ACTION_SUSPEND) == -EBUSY);
    lock_rc = 0;
    assert(tps43_pm_action(&dev, PM_DEVICE_ACTION_SUSPEND) == 0 && chip_asleep);
    /* Preserve failed resume error even when interrupt restoration succeeds. */
    read_rc = -EIO;
    assert(tps43_pm_action(&dev, PM_DEVICE_ACTION_RESUME) == -EIO && data.suspended);
    read_rc = 0;
    assert(tps43_set_sleep(&dev, false) == 0 && chip_asleep && queued);
    tps43_power_work(&data.power_work); queued = 0;
    assert(!chip_asleep && !data.suspended && finger_cancels > 0 && cancelled > 0);
    config.enable_power_management = false; count = writes;
    assert(tps43_set_sleep(&dev, true) == 0 && writes == count);
    /* Only our five private ABS codes are excluded, on both halves.
     * sync=false by itself was insufficient in ZMK's unfiltered callback. */
    for (int type = 0; type < 5; type++) for (int code = 0; code < 768; code++) {
        struct input_event ev = {(uint8_t)type, (uint16_t)code};
        bool display = type == INPUT_EV_ABS && (code == 0x18 || (code >= 0x30 && code <= 0x33));
        count = calls; activity_input_listener(&ev);
        assert(calls == count + !display);
    }
    count = calls;
    for (int n = 0; n < 14400; n++) {
        struct input_event ev = {INPUT_EV_ABS, 0x33}; activity_input_listener(&ev);
    }
    assert(calls == count); /* one hour of display traffic cannot postpone sleep */
    return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    temp = Path(d)
    include = temp / 'zephyr/dt-bindings/input'
    include.mkdir(parents=True)
    (include / 'input-event-codes.h').write_text('#pragma once\n', encoding='utf-8')
    p = temp / 'power.c'
    p.write_text(source, encoding='utf-8')
    executable = temp / ('power.exe' if sys.platform == 'win32' else 'power')
    cmd = (sys.argv[1:] or ['cc']) + ['-std=c11', '-Wall', '-Wextra', '-Werror',
        '-I' + str(temp), '-I' + str(root / 'modules/azoteq/include'), str(p), '-o', str(executable)]
    if sys.platform != 'win32':
        cmd += ['-fsanitize=undefined,address']
    subprocess.run(cmd, check=True)
    subprocess.run([str(executable)], check=True)
print('Production power transitions, pending resume, bus faults and activity callback passed')
