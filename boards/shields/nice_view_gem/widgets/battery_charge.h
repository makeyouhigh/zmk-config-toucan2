#pragma once
#include <lvgl.h>
#include "util.h"

/* A 10 x 12 bolt, centered in the existing numeric cell below L or R. */
static inline void draw_battery_charge(lv_obj_t *canvas, int center_x) {
    static const uint16_t rows[12] = {
        0x01c, 0x038, 0x070, 0x0e0, 0x1c0, 0x3fe,
        0x1fc, 0x038, 0x070, 0x0e0, 0x0c0, 0x080,
    };
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = LVGL_FOREGROUND;
    dsc.bg_opa = LV_OPA_COVER;
    dsc.border_width = 0;
    dsc.radius = 0;
    for (int y = 0; y < 12; y++) {
        for (int x = 0; x < 10; x++) {
            if (rows[y] & (1U << (9 - x))) {
                lv_canvas_draw_rect(canvas, center_x - 5 + x, 45 + y, 1, 1, &dsc);
            }
        }
    }
}
