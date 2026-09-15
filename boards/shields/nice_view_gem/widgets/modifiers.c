/*
 * SPDX-License-Identifier: MIT
 *
 * 기존 그래프 영역에 활성화된 모디키 아이콘을 표시합니다.
 * 외부 글꼴 없이 선과 사각형으로 그립니다.
 */

#include <zephyr/kernel.h>
#include <dt-bindings/zmk/modifiers.h>

#include "modifiers.h"
#include "util.h"

#define MOD_AREA_X 12
#define MOD_AREA_Y 78
#define MOD_AREA_WIDTH 120
#define MOD_AREA_HEIGHT 24

#define MOD_ICON_SIZE 20
#define MOD_ICON_GAP 8

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

static void draw_icon(lv_obj_t *canvas, int x, int y, uint8_t modifier) {
    lv_draw_line_dsc_t line;

    init_line_dsc(&line, LVGL_FOREGROUND, 2);
    line.opa = LV_OPA_COVER;

    if (modifier == MOD_LCTL) {
        /* Ctrl: 위쪽 꺾쇠 */
        lv_point_t points[] = {
            {x + 3, y + 12},
            {x + 10, y + 5},
            {x + 17, y + 12},
        };

        lv_canvas_draw_line(canvas, points, ARRAY_SIZE(points), &line);

    } else if (modifier == MOD_LSFT) {
        /* Shift: 위쪽 화살표 */
        lv_point_t points[] = {
            {x + 10, y + 2},
            {x + 18, y + 10},
            {x + 14, y + 10},
            {x + 14, y + 18},
            {x + 6, y + 18},
            {x + 6, y + 10},
            {x + 2, y + 10},
            {x + 10, y + 2},
        };

        lv_canvas_draw_line(canvas, points, ARRAY_SIZE(points), &line);

    } else if (modifier == MOD_LALT) {
        /* Alt: 옵션 기호 */
        lv_point_t lower[] = {
            {x + 2, y + 5},
            {x + 7, y + 5},
            {x + 13, y + 15},
            {x + 18, y + 15},
        };

        lv_point_t upper[] = {
            {x + 12, y + 5},
            {x + 18, y + 5},
        };

        lv_canvas_draw_line(canvas, lower, ARRAY_SIZE(lower), &line);
        lv_canvas_draw_line(canvas, upper, ARRAY_SIZE(upper), &line);

    } else if (modifier == MOD_LGUI) {
        /* Win: 네 칸 창 모양 */
        lv_draw_rect_dsc_t rect;

        init_rect_dsc(&rect, LVGL_FOREGROUND);
        rect.bg_opa = LV_OPA_COVER;
        rect.border_width = 0;
        rect.radius = 0;

        for (int row = 0; row < 2; row++) {
            for (int col = 0; col < 2; col++) {
                lv_canvas_draw_rect(
                    canvas,
                    x + 2 + col * 9,
                    y + 2 + row * 9,
                    7,
                    7,
                    &rect
                );
            }
        }
    }
}

void draw_modifiers_status(lv_obj_t *canvas, uint8_t modifiers) {
    static const uint8_t order[] = {
        MOD_LCTL,
        MOD_LSFT,
        MOD_LALT,
        MOD_LGUI,
    };

    /* 기존 그래프가 있던 영역만 지웁니다. */
    lv_draw_rect_dsc_t background;

    init_rect_dsc(&background, LVGL_BACKGROUND);
    background.bg_opa = LV_OPA_COVER;
    background.border_width = 0;
    background.radius = 0;

    lv_canvas_draw_rect(
        canvas,
        MOD_AREA_X,
        MOD_AREA_Y,
        MOD_AREA_WIDTH,
        MOD_AREA_HEIGHT,
        &background
    );

    modifiers = modifiers_normalize(modifiers);

    int count = 0;

    for (size_t i = 0; i < ARRAY_SIZE(order); i++) {
        if (modifiers & order[i]) {
            count++;
        }
    }

    if (count == 0) {
        return;
    }

    /* 활성 모디키 개수에 맞춰 가운데 정렬합니다. */
    int width = count * MOD_ICON_SIZE + (count - 1) * MOD_ICON_GAP;
    int x = MOD_AREA_X + (MOD_AREA_WIDTH - width) / 2;
    int y = MOD_AREA_Y + (MOD_AREA_HEIGHT - MOD_ICON_SIZE) / 2;

    for (size_t i = 0; i < ARRAY_SIZE(order); i++) {
        if (modifiers & order[i]) {
            draw_icon(canvas, x, y, order[i]);
            x += MOD_ICON_SIZE + MOD_ICON_GAP;
        }
    }
}
