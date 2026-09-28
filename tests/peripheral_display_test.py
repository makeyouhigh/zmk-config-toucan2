"""Exercise the shipped right battery callbacks and renderer with local caches."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
screen = (root / 'boards/shields/nice_view_gem/widgets/screen.c').read_text(encoding='utf-8')
arc = (root / 'boards/shields/nice_view_gem/widgets/battery_arc_peripheral.c').read_text(encoding='utf-8')
left_arc = (root / 'boards/shields/nice_view_gem/widgets/battery_arc.c').read_text(encoding='utf-8')
charge = (root / 'boards/shields/nice_view_gem/widgets/battery_charge.h').read_text(encoding='utf-8')


def function(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


test = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <toucan/peripheral_status.h>
typedef int atomic_t;
static atomic_t peripheral_zero_received;
static atomic_t peripheral_status_payload;
struct input_event { int type, code; int32_t value; };
#define INPUT_EV_ABS 3
static void atomic_set(atomic_t *p, int v) { *p = v; }
static int atomic_get(atomic_t *p) { return *p; }
struct zmk_split_transport_status { bool available, enabled; };
struct zmk_split_transport_central_api {
    struct zmk_split_transport_status (*get_status)(void);
    int (*get_available_source_ids)(uint8_t *);
};
struct zmk_split_transport_central { const struct zmk_split_transport_central_api *api; };
static struct zmk_split_transport_status link = {true, true};
static int sources_count, cached_level, cache_rc;
static uint8_t source_id;
static struct zmk_split_transport_status get_status(void) { return link; }
static int get_sources(uint8_t *sources) { sources[0] = source_id; return sources_count; }
static const struct zmk_split_transport_central_api api = {get_status, get_sources};
static const struct zmk_split_transport_central transports[] = {{NULL}, {&api}};
#define STRUCT_SECTION_FOREACH(type, item) \
    for (const struct type *item = transports; item < transports + 2; item++)
#define ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT 2
struct zmk_peripheral_battery_state_changed { uint8_t source, state_of_charge; };
typedef struct zmk_peripheral_battery_state_changed zmk_event_t;
static const struct zmk_peripheral_battery_state_changed *as_zmk_peripheral_battery_state_changed(const zmk_event_t *ev) { return ev; }
static int zmk_split_central_get_peripheral_battery_level(uint8_t source, uint8_t *out) {
    assert(source == 0); *out = cached_level; return cache_rc;
}
struct battery_peripheral_status_state { uint8_t level; bool known, connected, usb_present; };
struct status_state { uint8_t battery_p, battery; bool battery_p_known, peripheral_connected, charging, charging_p; uint8_t layer_index; };
struct zmk_widget_screen { void *obj; int cbuf[1]; struct status_state state; };
#define FORCE_TP_LAYER 8
#define TOUCAN_FORCE_SYS_LAYER 7
static int redraws;
static void draw_top(void *obj, int *buf, const struct status_state *state) {
    (void)obj; (void)buf; (void)state; redraws++;
}

/* Capture the actual LVGL draw commands without a screen or radio. */
typedef int lv_obj_t;
typedef struct { int x, y; } lv_point_t;
typedef struct { int bg_color, bg_opa, border_width, border_color, border_opa, radius; } lv_draw_rect_dsc_t;
typedef struct { int unused; } lv_draw_label_dsc_t;
static int rectangles, filled_dots, labels, quinquefive_12, quinquefive_8;
static char rendered[2][8];
#define LV_OPA_COVER 255
#define LVGL_FOREGROUND 1
#define LV_TEXT_ALIGN_CENTER 0
static int lv_color_white(void) { return 1; }
static int lv_color_black(void) { return 0; }
static void lv_draw_rect_dsc_init(lv_draw_rect_dsc_t *d) { *d = (lv_draw_rect_dsc_t){0}; }
static void lv_canvas_draw_rect(lv_obj_t *c, int x, int y, int w, int h, const lv_draw_rect_dsc_t *d) {
    (void)c; (void)x; (void)y; (void)w; (void)h;
    rectangles++; filled_dots += d->bg_color == 1;
}
static void init_label_dsc(lv_draw_label_dsc_t *d, int color, const int *font, int align) {
    (void)d; (void)color; (void)font; (void)align;
}
static void lv_canvas_draw_text(lv_obj_t *c, int x, int y, int width, const lv_draw_label_dsc_t *d, const char *text) {
    (void)c; (void)x; (void)y; (void)width; (void)d;
    assert(labels < 2); snprintf(rendered[labels++], 8, "%s", text);
}
'''
test += function(screen, 'static bool right_peripheral_connected(') + '\n'
test += function(screen, 'static void peripheral_status_input(') + '\n'
test += function(screen, 'static struct battery_peripheral_status_state\nbattery_peripheral_status_get_state(') + '\n'
test += function(screen, 'static void set_battery_peripheral_status(') + '\n'
test += '\n'.join(line for line in charge.splitlines() if not line.startswith('#')) + '\n'
test += '\n'.join(line for line in arc.splitlines() if not line.startswith('#include')) + '\n'
test += '\n'.join(line for line in left_arc.splitlines() if not line.startswith('#include')) + '\n'
test += r'''
static void expect_draw(struct status_state state, const char *text, int dots) {
    rectangles = filled_dots = labels = 0;
    draw_battery_peripheral_status(NULL, &state);
    assert(rectangles == 10 && filled_dots == dots && labels == 2);
    assert(strcmp(rendered[0], "R") == 0 && strcmp(rendered[1], text) == 0);
}
int main(void) {
    struct battery_peripheral_status_state s = battery_peripheral_status_get_state(NULL);
    assert(!s.connected && !s.known);
    sources_count = 1;
    s = battery_peripheral_status_get_state(NULL);
    assert(s.connected && !s.known); /* Empty initial cache is not 0%. */
    cached_level = 75;
    s = battery_peripheral_status_get_state(NULL);
    assert(s.connected && s.known && s.level == 75); /* Recover a missed UI event. */
    zmk_event_t other = {1, 20};
    s = battery_peripheral_status_get_state(&other);
    assert(s.known && s.level == 75); /* Another source cannot overwrite R. */
    cached_level = 0;
    zmk_event_t zero = {0, 0};
    s = battery_peripheral_status_get_state(&zero);
    assert(s.connected && s.known && s.level == 0);
    assert(battery_peripheral_status_get_state(NULL).known);
    sources_count = 0;
    assert(!battery_peripheral_status_get_state(NULL).known);
    sources_count = 1;
    assert(!battery_peripheral_status_get_state(NULL).known);
    cached_level = 255;
    assert(!battery_peripheral_status_get_state(NULL).known);
    cached_level = 82; cache_rc = -1;
    assert(!battery_peripheral_status_get_state(NULL).known);
    cache_rc = 0;
    source_id = 1;
    assert(!battery_peripheral_status_get_state(NULL).connected);
    source_id = 0; sources_count = -1;
    assert(!battery_peripheral_status_get_state(NULL).connected);
    sources_count = 1; link.enabled = false;
    assert(!battery_peripheral_status_get_state(NULL).connected);
    link.enabled = true; link.available = false;
    assert(!battery_peripheral_status_get_state(NULL).connected);
    link.available = true;

    struct zmk_widget_screen widget = {0};
    s = battery_peripheral_status_get_state(NULL);
    set_battery_peripheral_status(&widget, s);
    assert(redraws == 1 && widget.state.battery_p == 82);
    for (int i = 0; i < 1000; i++) set_battery_peripheral_status(&widget, s);
    assert(redraws == 1); /* Polling an unchanged cache must not redraw. */
    widget.state.layer_index = FORCE_TP_LAYER; s.level = 81;
    set_battery_peripheral_status(&widget, s);
    assert(redraws == 1 && widget.state.battery_p == 81);
    widget.state.layer_index = TOUCAN_FORCE_SYS_LAYER; s.level = 80;
    set_battery_peripheral_status(&widget, s);
    assert(redraws == 1 && widget.state.battery_p == 80);
    expect_draw((struct status_state){0}, "OFF", 0);
    expect_draw((struct status_state){.peripheral_connected = true}, "--", 0);
    expect_draw((struct status_state){.peripheral_connected = true, .battery_p_known = true}, "0", 0);
    expect_draw((struct status_state){.peripheral_connected = true, .battery_p_known = true, .battery_p = 75}, "75", 8);
    expect_draw((struct status_state){.peripheral_connected = true, .battery_p_known = true, .battery_p = 100}, "100", 10);
    /* Each side's own USB state replaces only its numeric label. */
    rectangles = filled_dots = labels = 0;
    draw_battery_status(NULL, &(struct status_state){.battery = 80, .charging = false});
    assert(labels == 2 && strcmp(rendered[0], "L") == 0 && strcmp(rendered[1], "80") == 0);
    rectangles = filled_dots = labels = 0;
    draw_battery_status(NULL, &(struct status_state){.battery = 80, .charging = true});
    assert(labels == 1 && rectangles > 10 && strcmp(rendered[0], "L") == 0);
    rectangles = filled_dots = labels = 0;
    draw_battery_peripheral_status(NULL, &(struct status_state){.peripheral_connected = true, .charging_p = true});
    assert(labels == 1 && rectangles > 10 && strcmp(rendered[0], "R") == 0);
    expect_draw((struct status_state){.peripheral_connected = true, .charging = true}, "--", 0);
    expect_draw((struct status_state){.charging_p = true}, "OFF", 0);
    struct input_event event = {INPUT_EV_ABS, TOUCAN_INPUT_PERIPHERAL_STATUS_CODE,
        toucan_peripheral_status_pack(63, true, true)};
    peripheral_status_input(&event);
    s = battery_peripheral_status_get_state(NULL);
    assert(s.connected && s.known && s.level == 63 && s.usb_present);
    event.value = toucan_peripheral_status_pack(63, true, false);
    peripheral_status_input(&event);
    s = battery_peripheral_status_get_state(NULL);
    assert(s.level == 63 && !s.usb_present);
    event.value = -1; peripheral_status_input(&event);
    assert(battery_peripheral_status_get_state(NULL).level == 63);
    event.code = 0x18; event.value = toucan_peripheral_status_pack(90, true, true);
    peripheral_status_input(&event);
    assert(battery_peripheral_status_get_state(NULL).level == 63);
    sources_count = 0;
    s = battery_peripheral_status_get_state(NULL);
    assert(!s.connected && !s.usb_present && !atomic_get(&peripheral_status_payload));
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
    if sys.platform != 'win32':
        cmd += ['-fsanitize=undefined,address']
    subprocess.run(cmd, check=True)
    subprocess.run([str(executable)], check=True)
print('Right battery: boot, missed event, reconnect, unknown/zero, source filtering, unchanged redraw and TP/SYS isolation passed')
