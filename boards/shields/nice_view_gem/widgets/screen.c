#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/sys/atomic.h>
#include <toucan/force_display.h>
#include <toucan/force_status.h>
#include <stdio.h>
#include <lvgl.h>
#include "../assets/custom_fonts.h"

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>

/* 기존 WPM 이벤트 헤더: 원본 보존 */
// #include <zmk/events/wpm_state_changed.h>

/* 현재 모디키 상태를 읽기 위한 헤더 */
#include <zmk/hid.h>

#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/display/widgets/battery_status.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#include <zmk/usb.h>
#include <zmk/split/central.h>

#if defined(CONFIG_TOUCAN_STATUS_SCREEN) && CONFIG_TOUCAN_STATUS_SCREEN == 2
#include "battery_arc.h"
#include "battery_arc_peripheral.h"
#include "profile_arc.h"
#include "output_arc.h"

/* 기존 그래프 헤더: 원본 보존 */
// #include "chart.h"

#include "modifiers.h"
#else
#include "battery.h"
#include "battery_peripheral.h"
#include "profile.h"
#include "output.h"
#endif

#if defined(CONFIG_TOUCAN_STATUS_SCREEN) && CONFIG_TOUCAN_STATUS_SCREEN == 1
#include "layer_logo.h"
#elif defined(CONFIG_TOUCAN_STATUS_SCREEN) && CONFIG_TOUCAN_STATUS_SCREEN == 2
#include "layer_arc.h"
#else
#include "layer.h"
#endif

#include "screen.h"
#include "sleep.h"

struct connection_status_state {
    bool connected;
};

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

static void settings_text(lv_obj_t *canvas, int x, int y, int width,
                          const lv_font_t *font, const char *text) {
    lv_draw_label_dsc_t label;
    init_label_dsc(&label,LVGL_FOREGROUND,font,LV_TEXT_ALIGN_LEFT);
    lv_canvas_draw_text(canvas,x,y,width,&label,text);
}
static void settings_number(lv_obj_t *canvas,int x,int y,bool known,uint16_t value) {
    char text[8];
    if (known) snprintf(text,sizeof(text),"%u",(unsigned)value);
    else snprintf(text,sizeof(text),"----");
    settings_text(canvas,x,y,64,&quinquefive_12,text);
}
static void draw_force_settings(lv_obj_t *canvas,const struct status_state *state) {
    const struct toucan_force_levels *v=&state->force_levels;
    settings_text(canvas,4,2,94,&quinquefive_12,"FORCE");
    settings_text(canvas,108,4,36,&quinquefive_8,"v17");
    settings_text(canvas,4,20,64,&quinquefive_8,"REST");
    settings_text(canvas,78,20,64,&quinquefive_8,"MOVE");
    settings_text(canvas,4,34,136,&quinquefive_8,"Y/H LOCK");
    settings_number(canvas,4,47,state->force_known,v->lock);
    settings_number(canvas,78,47,state->force_known,v->moving_lock);
    settings_text(canvas,4,71,136,&quinquefive_8,"U/J CLICK");
    settings_number(canvas,4,84,state->force_known,v->press);
    settings_number(canvas,78,84,state->force_known,v->moving_press);
    settings_text(canvas,4,108,136,&quinquefive_8,"I/K RELEASE");
    settings_number(canvas,4,121,state->force_known,v->release);
    settings_number(canvas,78,121,state->force_known,v->release);
    settings_text(canvas,4,148,136,&quinquefive_8,
                  state->force_known ? "LIMITS ACTIVE" : "WAIT RIGHT");
}

/**
 * Draw buffers
 **/

static void draw_top(lv_obj_t *widget, lv_color_t cbuf[],
                     const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 0);
    fill_background(canvas);

    if (is_sleep_screen_active()) {
        draw_sleep_screen(canvas);
        return;
    }

    if (state->layer_index == TOUCAN_FORCE_SYS_LAYER) {
        draw_force_settings(canvas,state);
        return;
    }

    // Draw widgets
    draw_output_status(canvas, state);

#if defined(CONFIG_TOUCAN_STATUS_SCREEN) && CONFIG_TOUCAN_STATUS_SCREEN == 2
    /* 기존 그래프 그리기: 원본 보존 */
    // draw_chart_status(canvas, state);

    draw_modifiers_status(canvas, state->modifiers, state->touch_state);
#endif

    draw_layer_status(canvas, state);
    draw_profile_status(canvas, state);
    draw_battery_status(canvas, state);
    draw_battery_peripheral_status(canvas, state);
}

/**
 * Battery status
 **/

// L
static void set_battery_status(struct zmk_widget_screen *widget,
                               struct battery_status_state state) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging = state.usb_present;
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

    widget->state.battery = state.level;

    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void battery_status_update_cb(struct battery_status_state state) {
    struct zmk_widget_screen *widget;

    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        set_battery_status(widget, state);
    }
}

static struct battery_status_state battery_status_get_state(
    const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev =
        as_zmk_battery_state_changed(eh);

    return (struct battery_status_state){
        .level = (ev != NULL)
                     ? ev->state_of_charge
                     : zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(
    widget_battery_status,
    struct battery_status_state,
    battery_status_update_cb,
    battery_status_get_state
);

ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

// R
static void set_battery_peripheral_status(
    struct zmk_widget_screen *widget,
    struct battery_peripheral_status_state state) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging_p = state.usb_present;
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

    /* 기존 배터리 조회 코드: 원본 보존 */
    // uint8_t level;
    // zmk_split_central_get_peripheral_battery_level(0, &level);
    //
    // widget->state.battery_p = level;

    widget->state.battery_p = state.level;

    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void battery_peripheral_status_update_cb(
    struct battery_peripheral_status_state state) {
    struct zmk_widget_screen *widget;

    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        set_battery_peripheral_status(widget, state);
    }
}

static struct battery_peripheral_status_state
battery_peripheral_status_get_state(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);

    uint8_t level = 0;

    if (ev != NULL) {
        level = ev->state_of_charge;
    } else {
        /* 화면 초기화 때는 배터리 이벤트가 없을 수 있습니다. */
        (void)zmk_split_central_get_peripheral_battery_level(0, &level);
    }

    return (struct battery_peripheral_status_state){
        /* 기존 이벤트 직접 접근: 원본 보존 */
        // .level = ev->state_of_charge,

        .level = level,
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(
    widget_battery_peripheral_status,
    struct battery_peripheral_status_state,
    battery_peripheral_status_update_cb,
    battery_peripheral_status_get_state
);

ZMK_SUBSCRIPTION(
    widget_battery_peripheral_status,
    zmk_peripheral_battery_state_changed
);

/**
 * Layer status
 **/

static void set_layer_status(struct zmk_widget_screen *widget,
                             struct layer_status_state state) {
    if (state.index==TOUCAN_FORCE_SYS_LAYER && widget->state.layer_index!=TOUCAN_FORCE_SYS_LAYER) {
        toucan_force_status_request();
        widget->state.force_known=false;
    }
    widget->state.layer_index = zmk_keymap_highest_layer_active();

    draw_top(widget->obj, widget->cbuf3, &widget->state);
}

static void layer_status_update_cb(struct layer_status_state state) {
    struct zmk_widget_screen *widget;

    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        set_layer_status(widget, state);
    }
}

static struct layer_status_state layer_status_get_state(
    const zmk_event_t *eh) {
    uint8_t index = zmk_keymap_highest_layer_active();

    return (struct layer_status_state){
        .index = index
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(
    widget_layer_status,
    struct layer_status_state,
    layer_status_update_cb,
    layer_status_get_state
)

ZMK_SUBSCRIPTION(widget_layer_status, zmk_layer_state_changed);

/**
 * Output status
 **/

static void set_output_status(struct zmk_widget_screen *widget,
                              const struct output_status_state *state) {
    widget->state.selected_endpoint = state->selected_endpoint;
    widget->state.active_profile_index = state->active_profile_index;
    widget->state.active_profile_connected = state->active_profile_connected;
    widget->state.active_profile_bonded = state->active_profile_bonded;

    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void output_status_update_cb(struct output_status_state state) {
    struct zmk_widget_screen *widget;

    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        set_output_status(widget, &state);
    }
}

static struct output_status_state output_status_get_state(
    const zmk_event_t *_eh) {
    return (struct output_status_state){
        .selected_endpoint = zmk_endpoints_selected(),
        .active_profile_index = zmk_ble_active_profile_index(),
        .active_profile_connected = zmk_ble_active_profile_is_connected(),
        .active_profile_bonded = !zmk_ble_active_profile_is_open(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(
    widget_output_status,
    struct output_status_state,
    output_status_update_cb,
    output_status_get_state
)

ZMK_SUBSCRIPTION(widget_output_status, zmk_endpoint_changed);

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_output_status, zmk_usb_conn_state_changed);
#endif

#if defined(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(widget_output_status, zmk_ble_active_profile_changed);
#endif

/*
 * 모디키 상태 갱신
 *
 * 50ms마다 상태를 확인합니다.
 * 실제 그리기는 상태가 바뀌었을 때만 수행합니다.
 */
#if defined(CONFIG_TOUCAN_STATUS_SCREEN) && CONFIG_TOUCAN_STATUS_SCREEN == 2

#define MODIFIERS_REFRESH_MS 50
#define FORCE_DISPLAY_STALE_MS 750

static atomic_t touch_state;
static atomic_t force_updated_ms;

static void force_input_event(struct input_event *event) {
    if (event->type == INPUT_EV_ABS && event->code == TOUCAN_INPUT_TOUCH_STATE_CODE) {
        atomic_set(&touch_state, CLAMP(event->value, TOUCAN_TOUCH_NONE, TOUCAN_TOUCH_PRESSED));
        atomic_set(&force_updated_ms, (atomic_val_t)k_uptime_get_32());
    }
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(trackpad_split)), force_input_event);

static uint8_t current_touch_state(void) {
    uint32_t age = k_uptime_get_32() - (uint32_t)atomic_get(&force_updated_ms);
    return age > FORCE_DISPLAY_STALE_MS ? TOUCAN_TOUCH_NONE : (uint8_t)atomic_get(&touch_state);
}

static void modifiers_refresh_work_cb(struct k_work *work);

K_WORK_DELAYABLE_DEFINE(
    modifiers_refresh_work,
    modifiers_refresh_work_cb
);

static void modifiers_refresh_work_cb(struct k_work *work) {
    (void)work;

    if (is_sleep_screen_active()) {
        return;
    }

    const uint8_t modifiers = modifiers_normalize(
        zmk_hid_get_keyboard_report()->body.modifiers
    );
    const uint8_t touch = current_touch_state();

    struct zmk_widget_screen *widget;

    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        if (widget->state.layer_index == TOUCAN_FORCE_SYS_LAYER) {
            struct toucan_force_levels levels;
            uint32_t revision;
            bool known=toucan_force_status_get(&levels,&revision);
            if (widget->state.force_revision != revision || widget->state.force_known != known) {
                widget->state.force_revision=revision;
                widget->state.force_known=known;
                widget->state.force_levels=levels;
                draw_top(widget->obj,widget->cbuf,&widget->state);
            }
            continue;
        }
        if (widget->state.modifiers != modifiers || widget->state.touch_state != touch) {
            widget->state.modifiers = modifiers;
            widget->state.touch_state = touch;

            /* 가운데 모디키 영역만 갱신합니다. */
            lv_obj_t *canvas = lv_obj_get_child(widget->obj, 0);
            draw_modifiers_status(canvas, modifiers, touch);
        }
    }

    if (!is_sleep_screen_active()) {
        k_work_schedule_for_queue(
            zmk_display_work_q(),
            &modifiers_refresh_work,
            K_MSEC(MODIFIERS_REFRESH_MS)
        );
    }
}

#endif

/**
 * Activity state handling for sleep screen
 **/

static void force_redraw_all_widgets(void) {
    struct zmk_widget_screen *widget;

    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        draw_top(widget->obj, widget->cbuf, &widget->state);
    }
}

/* 절전에서 복귀할 때 표시 작업 큐에서 화면을 다시 그립니다. */
static void wake_redraw_work_cb(struct k_work *work) {
    (void)work;
    force_redraw_all_widgets();
}

K_WORK_DEFINE(wake_redraw_work, wake_redraw_work_cb);

static int display_activity_event_handler(const zmk_event_t *eh) {
    struct zmk_activity_state_changed *ev =
        as_zmk_activity_state_changed(eh);

    if (ev == NULL) {
        return -ENOTSUP;
    }

    switch (ev->state) {
    /* 기존 분기 시작: 원본 보존 */
    // case ZMK_ACTIVITY_ACTIVE:

    case ZMK_ACTIVITY_ACTIVE: {
        bool was_sleeping = is_sleep_screen_active();

        set_sleep_screen_active(false);

        /* 기존 복귀 처리 주석: 원본 보존 */
        // No need to force a redraw, it will happen automatically if really coming back from sleep (ACTIVE also comes after IDLE)
        //force_redraw_all_widgets();

        /*
         * WPM에 의한 주기 갱신을 제거했으므로,
         * 절전 복귀 시 화면을 명시적으로 복구합니다.
         */
        if (was_sleeping && zmk_display_is_initialized()) {
            k_work_submit_to_queue(
                zmk_display_work_q(),
                &wake_redraw_work
            );
        }

#if defined(CONFIG_TOUCAN_STATUS_SCREEN) && CONFIG_TOUCAN_STATUS_SCREEN == 2
        if (zmk_display_is_initialized()) {
            k_work_reschedule_for_queue(
                zmk_display_work_q(),
                &modifiers_refresh_work,
                K_NO_WAIT
            );
        }
#endif
        break;
    }

    case ZMK_ACTIVITY_SLEEP:
        set_sleep_screen_active(true);

#if defined(CONFIG_TOUCAN_STATUS_SCREEN) && CONFIG_TOUCAN_STATUS_SCREEN == 2
        k_work_cancel_delayable(&modifiers_refresh_work);
#endif

        force_redraw_all_widgets();

        // Force LVGL to process pending updates and flush to display hardware
        // before the CPU enters deep sleep
        lv_task_handler();
        lv_refr_now(NULL);
        break;

    default:
        break; // ignore other states (like IDLE)
    }

    return 0;
}

ZMK_LISTENER(nice_view_gem_display, display_activity_event_handler);
ZMK_SUBSCRIPTION(nice_view_gem_display, zmk_activity_state_changed);

/*
 * 기존 WPM 그래프 이벤트 처리: 원본 보존
 *
 * 아래 코드는 삭제하지 않고 전부 주석 처리했습니다.
 */

// #if defined(CONFIG_TOUCAN_STATUS_SCREEN) && CONFIG_TOUCAN_STATUS_SCREEN == 2
// /**
//  * WPM status
//  */
//     static void set_chart_status(struct zmk_widget_screen *widget, struct chart_status_state state) {
//     widget->state.wpm = state.wpm;
//     draw_top(widget->obj, widget->cbuf, &widget->state);
// }
// static void chart_status_update_cb(struct chart_status_state state) {
//     struct zmk_widget_screen *widget;
//     SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
//         set_chart_status(widget, state);
//     }
// }
//
// static struct chart_status_state chart_status_get_state(const zmk_event_t *eh) {
//     const struct zmk_wpm_state_changed *ev = as_zmk_wpm_state_changed(eh);
//     return (struct chart_status_state){
//         .wpm = (ev != NULL) ? ev->state : zmk_wpm_get_state(),
//     };
// }
// ZMK_DISPLAY_WIDGET_LISTENER(widget_chart_status, struct chart_status_state,
//                             chart_status_update_cb, chart_status_get_state);
// ZMK_SUBSCRIPTION(widget_chart_status, zmk_wpm_state_changed);
// #endif

/**
 * Initialization
 **/

int zmk_widget_screen_init(struct zmk_widget_screen *widget,
                           lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, SCREEN_WIDTH, SCREEN_HEIGHT);

    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_canvas_set_buffer(
        top,
        widget->cbuf,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        LV_IMG_CF_TRUE_COLOR
    );

    sys_slist_append(&widgets, &widget->node);

    widget_battery_status_init();
    widget_battery_peripheral_status_init();
    widget_layer_status_init();
    widget_output_status_init();

#if defined(CONFIG_TOUCAN_STATUS_SCREEN) && CONFIG_TOUCAN_STATUS_SCREEN == 2
    /* 기존 WPM 위젯 초기화: 원본 보존 */
    // widget_chart_status_init();

    k_work_reschedule_for_queue(
        zmk_display_work_q(),
        &modifiers_refresh_work,
        K_NO_WAIT
    );
#endif

    return 0;
}

lv_obj_t *zmk_widget_screen_obj(struct zmk_widget_screen *widget) {
    return widget->obj;
}
