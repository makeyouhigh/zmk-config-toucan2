/* SPDX-License-Identifier: MIT */
#include <zephyr/kernel.h>
#include <dt-bindings/zmk/modifiers.h>
#include <toucan/force_display.h>

#include "modifiers.h"
#include "status_icons.h"
#include "util.h"

#define MOD_AREA_X 4
#define MOD_AREA_Y 64
#define MOD_AREA_WIDTH 136
#define MOD_AREA_HEIGHT 32
#define MOD_ICON_SIZE 24
#define MOD_ICON_GAP 2
#define TOUCH_ICON_SIZE 32
#define TOUCH_ICON_X 108

uint8_t modifiers_normalize(uint8_t hid_modifiers) {
    uint8_t result = 0;
    if (hid_modifiers & (MOD_LCTL | MOD_RCTL)) {
        result |= MOD_LCTL;
    }
    if (hid_modifiers & (MOD_LSFT | MOD_RSFT)) {
        result |= MOD_LSFT;
    }
    if (hid_modifiers & (MOD_LALT | MOD_RALT)) {
        result |= MOD_LALT;
    }
    if (hid_modifiers & (MOD_LGUI | MOD_RGUI)) {
        result |= MOD_LGUI;
    }
    return result;
}

/* Use the same pixel silhouette for outline and filled states. A pixel belongs
 * to the outline when at least one of its four neighbours is background. */
static void draw_icon(lv_obj_t *canvas, int x, int y, const uint32_t rows[],
                       int size, bool filled) {
    for (int py = 0; py < size; py++) {
        for (int px = 0; px < size; px++) {
            if (!(rows[py] & BIT(px))) {
                continue;
            }
            bool interior = true;
            for (int dy = -2; dy <= 2 && interior; dy++) {
                for (int dx = -2; dx <= 2; dx++) {
                    int nx = px + dx, ny = py + dy;
                    if (nx < 0 || nx >= size || ny < 0 || ny >= size ||
                        !(rows[ny] & BIT(nx))) {
                        interior = false;
                        break;
                    }
                }
            }
            if (filled || !interior) {
                lv_canvas_set_px_color(canvas, x + px, y + py, LVGL_FOREGROUND);
            }
        }
    }
}

void draw_modifiers_status(lv_obj_t *canvas, uint8_t modifiers, uint8_t touch_state) {
    static const uint8_t order[] = {MOD_LGUI, MOD_LALT, MOD_LCTL, MOD_LSFT};
    lv_draw_rect_dsc_t background;
    init_rect_dsc(&background, LVGL_BACKGROUND);
    background.bg_opa = LV_OPA_COVER;
    background.border_width = 0;
    background.radius = 0;
    lv_canvas_draw_rect(canvas, MOD_AREA_X, MOD_AREA_Y, MOD_AREA_WIDTH,
                        MOD_AREA_HEIGHT, &background);

    modifiers = modifiers_normalize(modifiers);
    for (size_t i = 0; i < ARRAY_SIZE(order); i++) {
        draw_icon(canvas, MOD_AREA_X + i * (MOD_ICON_SIZE + MOD_ICON_GAP),
                  MOD_AREA_Y + (MOD_AREA_HEIGHT - MOD_ICON_SIZE) / 2,
                  modifier_icons[i], MOD_ICON_SIZE, (modifiers & order[i]) != 0);
    }

    draw_icon(canvas, TOUCH_ICON_X, MOD_AREA_Y, touch_hand_icon, TOUCH_ICON_SIZE,
              touch_state == TOUCAN_TOUCH_CONTACT);
    if (touch_state == TOUCAN_TOUCH_PRESSED) {
        draw_icon(canvas, TOUCH_ICON_X, MOD_AREA_Y, touch_halo_icon, TOUCH_ICON_SIZE, true);
    }
    lv_obj_invalidate(canvas);
}
