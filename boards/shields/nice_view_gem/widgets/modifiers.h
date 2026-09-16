#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <lvgl.h>

/* 좌우 모디키를 Ctrl, Shift, Alt, Win 네 종류로 묶습니다. */
uint8_t modifiers_normalize(uint8_t hid_modifiers);

/* 고정된 모디 윤곽/채움 아이콘과 오른쪽 세 단계 터치 아이콘을 그립니다. */
void draw_modifiers_status(lv_obj_t *canvas, uint8_t modifiers, uint8_t touch_state);
