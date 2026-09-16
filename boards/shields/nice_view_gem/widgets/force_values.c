/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <toucan/force_values.h>
#include "force_values.h"
#include "util.h"
#include "../assets/custom_fonts.h"

static struct k_spinlock values_lock;
static int32_t pending_values, current_values, current_peak;
static int64_t updated_ms;
static bool received;

static void values_input_event(struct input_event *event) {
    if (event->type != INPUT_EV_ABS) { return; }
    k_spinlock_key_t key = k_spin_lock(&values_lock);
    if (event->code == TOUCAN_INPUT_FORCE_VALUES_CODE) {
        pending_values = event->value;
    } else if (event->code == TOUCAN_INPUT_FORCE_PEAK_CODE) {
        current_values = pending_values;
        current_peak = event->value;
        updated_ms = k_uptime_get();
        received = true;
    }
    k_spin_unlock(&values_lock, key);
}
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(trackpad_split)), values_input_event);

void draw_force_values(lv_obj_t *canvas, bool full_redraw) {
    static int64_t last_draw_ms;
    static char previous[4][18];
    int64_t now = k_uptime_get();
    if (!full_redraw && now - last_draw_ms < TOUCAN_FORCE_VALUES_INTERVAL_MS) { return; }
    last_draw_ms = now;
    k_spinlock_key_t key = k_spin_lock(&values_lock);
    int32_t values = current_values, peak = current_peak;
    bool available = received;
    bool fresh = available && now - updated_ms < 750;
    k_spin_unlock(&values_lock, key);
    uint16_t raw = toucan_force_pair_high(values);
    uint16_t base = toucan_force_pair_low(values);
    uint16_t delta = toucan_force_pair_low(peak);
    char lines[4][18];
    if (fresh) { snprintf(lines[0], sizeof(lines[0]), "S %u", raw); }
    else { snprintf(lines[0], sizeof(lines[0]), "S --"); }
    if (available) { snprintf(lines[1], sizeof(lines[1]), "B %u", base); }
    else { snprintf(lines[1], sizeof(lines[1]), "B --"); }
    if (available && delta) {
        snprintf(lines[2], sizeof(lines[2]), "C %lu", (unsigned long)base + delta);
    } else { snprintf(lines[2], sizeof(lines[2]), "C --"); }
    if (available) { snprintf(lines[3], sizeof(lines[3]), "P %u", toucan_force_pair_high(peak)); }
    else { snprintf(lines[3], sizeof(lines[3]), "P --"); }
    /* Diagnostic panel replaces only the lower layer/output text. Battery,
     * modifier/finger icons (including all positions) and profile dots stay put. */
    lv_draw_rect_dsc_t bg;
    init_rect_dsc(&bg, LVGL_BACKGROUND);
    lv_draw_label_dsc_t label;
    init_label_dsc(&label, LVGL_FOREGROUND, &quinquefive_8, LV_TEXT_ALIGN_LEFT);
    if (full_redraw) { lv_canvas_draw_rect(canvas, 0, 98, 122, 68, &bg); }
    for (unsigned int i=0; i<4; i++) {
        if (full_redraw || strcmp(lines[i], previous[i])) {
            int y = 101 + 16 * i;
            lv_canvas_draw_rect(canvas, 4, y, 116, 16, &bg);
            lv_canvas_draw_text(canvas, 6, y, 114, &label, lines[i]);
            strcpy(previous[i], lines[i]);
        }
    }
}
