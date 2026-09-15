#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <lvgl.h>

/* 좌우 모디키를 Ctrl, Shift, Alt, Win 네 종류로 묶습니다. */
uint8_t modifiers_normalize(uint8_t hid_modifiers);

/* 기존 그래프 영역만 지우고 활성 모디키를 그립니다. */
void draw_modifiers_status(lv_obj_t *canvas, uint8_t modifiers);
