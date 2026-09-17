/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include "../modules/azoteq/drivers/input/tps43_force.h"
#include "../modules/azoteq/drivers/input/tps43_tap.h"
#include "../modules/azoteq/drivers/input/tps43_three_tap.h"
#define CONFIG_TOUCAN_FORCE_TRACE 1
#include "../modules/azoteq/drivers/input/tps43_trace.h"
#include <toucan/touch_lease.h>

static const struct tps43_force_config config = {
    .lock_level=4250, .press_level=4500, .release_level=4000,
    .moving_lock_level=4750, .moving_press_level=5000,
    .debounce_ms=8, .motion_threshold=6, .motion_settle_ms=32,
    .drag_threshold=48, .drag_hold_ms=300, .touch_hold_ms=250,
};
struct fixture { struct tps43_force_state s; int64_t t; uint16_t x,y; };
static struct fixture fresh(void) { return (struct fixture){.x=1000,.y=1000}; }
static int frame(struct fixture *f, uint8_t fingers, uint16_t strength, bool valid) {
    f->t+=8;
    return tps43_force_step(&f->s,&config,f->t,fingers,strength,valid,f->x,f->y);
}
static int sample(struct fixture *f, uint16_t strength) { return frame(f,1,strength,true); }
static void click(struct fixture *f, uint16_t strength) {
    assert(sample(f,strength)==0);
    assert(sample(f,strength)==TPS43_FORCE_PRESS);
    assert(f->s.down && f->s.suppress_motion);
}

static void test_initial_contact_never_changes_levels(void) {
    const uint16_t initial[]={1,2000,3300,4000,4249,4400};
    for (unsigned n=0;n<sizeof(initial)/sizeof(initial[0]);n++) {
        struct fixture f=fresh();
        for(int i=0;i<150;i++) { assert(sample(&f,initial[n])==0); }
        assert(f.s.press_level==4500 && f.s.lock_level==4250);
        click(&f,4500);
        assert(sample(&f,4001)==0 && f.s.down);
        assert(sample(&f,4000)==TPS43_FORCE_RELEASE);
        assert(frame(&f,0,0,true)==0);
        click(&f,5500); /* Already firm at first contact still clicks. */
        assert(f.s.press_level==4500);
        assert(frame(&f,0,0,true)==TPS43_FORCE_RELEASE);
    }
}

static void test_fixed_hysteresis_and_short_release(void) {
    struct fixture f=fresh();
    assert(sample(&f,4250)==0 && f.s.suppress_motion && !f.s.down);
    assert(sample(&f,4499)==0 && f.s.suppress_motion && !f.s.down);
    assert(sample(&f,4249)==0 && !f.s.suppress_motion);
    assert(sample(&f,5500)==0); /* one-sample high spike must not click */
    assert(sample(&f,3300)==0 && !f.s.down && !f.s.suppress_motion);
    click(&f,5500);
    for(int i=0;i<20;i++) assert(sample(&f,4400)==0 && f.s.down);
    assert(sample(&f,4000)==TPS43_FORCE_RELEASE); /* one frame is sufficient */
    click(&f,4500); /* second peak can be lower than the first peak */
    assert(sample(&f,3900)==TPS43_FORCE_RELEASE);
    assert(sample(&f,3300)==0 && !f.s.suppress_motion);
}

static void test_repeated_fast_and_slow_clicks(void) {
    const int held_frames[]={2,8,25};
    for(unsigned k=0;k<sizeof(held_frames)/sizeof(held_frames[0]);k++) {
        struct fixture f=fresh();
        for(int n=0;n<1000;n++) {
            int presses=0;
            for(int i=0;i<held_frames[k];i++) presses+=sample(&f,5200)==TPS43_FORCE_PRESS;
            assert(presses==1 && f.s.press_level==4500);
            assert(sample(&f,4000)==TPS43_FORCE_RELEASE);
            assert(!f.s.down && !f.s.dragging);
        }
        assert(sample(&f,3300)==0 && !f.s.suppress_motion);
        f.x+=20;
        assert(sample(&f,3300)==0 && !f.s.suppress_motion && !f.s.down);
    }
}

static void test_moving_profile_and_immediate_motion(void) {
    struct fixture f=fresh();
    for(int i=0;i<100;i++) {
        f.x+=3;
        assert(sample(&f,3300)==0 && !f.s.suppress_motion && !f.s.down);
    }
    for(int i=0;i<25;i++) {
        f.x+=12;
        assert(sample(&f,4600)==0 && !f.s.suppress_motion && !f.s.down);
        assert(f.s.press_level==5000);
    }
    assert(sample(&f,4750)==0 && f.s.suppress_motion && !f.s.down);
    f.x+=25; click(&f,5000);
    assert(f.s.press_level==5000); /* squeeze centroid cannot change profile */
    assert(sample(&f,4000)==TPS43_FORCE_RELEASE);
    assert(sample(&f,3300)==0 && !f.s.suppress_motion);
    f=fresh();
    for(int i=0;i<25;i++) { f.x+=12; assert(sample(&f,3300)==0); }
    for(int i=0;i<4;i++) assert(sample(&f,3300)==0 && !f.s.suppress_motion);
    click(&f,4500); assert(f.s.press_level==4500);
    f=fresh(); assert(sample(&f,3300)==0);
    f.x+=25; click(&f,4500); /* a stationary squeeze can deform its centroid */
}

static void test_force_drag_latches_until_lift(void) {
    struct fixture f=fresh(); click(&f,5200);
    while(f.t-f.s.pressed_ms<300) {
        f.x+=4; assert(sample(&f,5200)==0 && !f.s.dragging && f.s.suppress_motion);
    }
    assert(f.s.drag_armed);
    f.x+=49; assert(sample(&f,5200)==0 && f.s.dragging && !f.s.suppress_motion);
    for(int i=0;i<200;i++) {
        f.x+=1; assert(sample(&f,i%2?3300:0)==0 && f.s.down && !f.s.suppress_motion);
    }
    assert(frame(&f,0,0,true)==TPS43_FORCE_RELEASE);
    assert(!f.s.down && !f.s.active);
}

static void test_initial_hold_and_slow_precision(void) {
    struct fixture f=fresh();
    for(int i=0;i<32;i++) assert(sample(&f,3300)==0 && !f.s.suppress_motion);
    f.x+=10; assert(sample(&f,3300)==0);
    f.x+=39; assert(sample(&f,3300)==TPS43_FORCE_PRESS && f.s.dragging);
    assert(sample(&f,100)==0 && f.s.down);
    assert(frame(&f,0,0,true)==TPS43_FORCE_RELEASE);
    f=fresh();
    for(int i=0;i<300;i++) {
        f.x+=1; assert(sample(&f,3300)==0 && !f.s.suppress_motion && !f.s.down);
    }
    assert(f.s.hold_cancelled);
    f=fresh(); for(int i=0;i<33;i++) assert(sample(&f,3300)==0);
    f.x+=13;
    for(int i=0;i<30;i++) {
        if(i%4==0) f.x++;
        assert(sample(&f,3300)==0 && !f.s.suppress_motion);
    }
    f.x+=50; assert(sample(&f,3300)==0 && f.s.hold_cancelled && !f.s.down);
}

static void test_invalid_contact_and_bounds(void) {
    assert(tps43_force_config_valid(&config));
    struct tps43_force_config bad=config; bad.release_level=bad.press_level;
    assert(!tps43_force_config_valid(&bad));
    struct fixture f=fresh(); click(&f,UINT16_MAX);
    assert(frame(&f,2,6000,true)==TPS43_FORCE_RELEASE && f.s.blocked);
    assert(sample(&f,UINT16_MAX)==0);
    assert(frame(&f,0,0,true)==0);
    click(&f,UINT16_MAX);
    assert(frame(&f,1,5000,false)==TPS43_FORCE_RELEASE);
    assert(frame(&f,0,0,true)==0); click(&f,5000);
    f.t+=TPS43_FORCE_STALE_MS;
    assert(sample(&f,5000)==TPS43_FORCE_RELEASE && f.s.blocked);
    assert(frame(&f,0,0,true)==0); click(&f,5000);
    assert(tps43_force_display_state(&f.s,1,true)==TOUCAN_TOUCH_PRESSED);
    assert(sample(&f,4000)==TPS43_FORCE_RELEASE);
    assert(tps43_force_display_state(&f.s,1,true)==TOUCAN_TOUCH_CONTACT);
    assert(frame(&f,0,0,true)==0);
    assert(tps43_force_display_state(&f.s,0,true)==TOUCAN_TOUCH_NONE);
}

static void test_taps_and_force_ownership(void) {
    struct fixture f=fresh(); struct tps43_tap_state tap={0};
    for(int n=0;n<4;n++) {
        for(int i=0;i<10;i++) {
            assert(sample(&f,3300)==0);
            assert(!tps43_tap_step(&tap,f.t,1,true,f.s.tap_consumed,f.x,f.y,200,16));
        }
        assert(frame(&f,0,0,true)==0);
        assert(tps43_tap_step(&tap,f.t,0,true,f.s.tap_consumed,f.x,f.y,200,16));
    }
    for(int i=0;i<3;i++) {
        sample(&f,5000);
        assert(!tps43_tap_step(&tap,f.t,1,true,f.s.tap_consumed,f.x,f.y,200,16));
    }
    assert(frame(&f,0,0,true)==TPS43_FORCE_RELEASE);
    assert(!tps43_tap_step(&tap,f.t,0,true,f.s.tap_consumed,f.x,f.y,200,16));
    struct tps43_three_tap_state three={0};
    const uint8_t pattern[]={1,2,3,3,2,1,0};
    for(int n=0;n<10;n++) {
        int clicks=0;
        for(unsigned j=0;j<sizeof(pattern);j++) {
            int64_t t=f.t+8;
            struct tps43_three_tap_result result=tps43_three_tap_step(&three,t,pattern[j],
                true,tps43_three_tap_consumed(&f.s),f.x,f.y,200,64);
            frame(&f,pattern[j],3300,true); clicks+=result.click;
        }
        assert(clicks==1);
    }
    assert(!tps43_two_finger_motion(3,true));
    assert(tps43_two_finger_motion(2,false));
}

static void test_trace_contains_actual_fixed_levels(void) {
    struct fixture f=fresh(); click(&f,5200);
    struct tps43_trace_record r={.sample_ms=(uint32_t)f.t};
    tps43_trace_state(&r,&f.s,&config);
    assert(r.lock_level==4250 && r.press_level==4500 && r.release_level==4000);
    assert(r.after_flags&(1<<2));
}

static void test_three_tap_rejection_and_missing_first_slot(void) {
    struct tps43_three_tap_state s={0};
    assert(tps43_three_tap_step(&s,0,3,true,false,500,500,200,64).claimed);
    assert(tps43_three_tap_step(&s,80,2,true,false,UINT16_MAX,UINT16_MAX,200,64).claimed);
    assert(tps43_three_tap_step(&s,120,0,true,false,UINT16_MAX,UINT16_MAX,200,64).click);
    tps43_three_tap_step(&s,300,3,true,false,500,500,200,64);
    tps43_three_tap_step(&s,380,3,true,false,565,500,200,64);
    assert(!tps43_three_tap_step(&s,420,0,true,false,0,0,200,64).click);
    tps43_three_tap_step(&s,600,3,true,false,500,500,200,64);
    assert(!tps43_three_tap_step(&s,801,0,true,false,0,0,200,64).click);
    tps43_three_tap_step(&s,900,3,true,false,500,500,200,64);
    tps43_three_tap_step(&s,940,4,true,false,500,500,200,64);
    assert(!tps43_three_tap_step(&s,980,0,true,false,0,0,200,64).click);
}

int main(void) {
    test_initial_contact_never_changes_levels();
    test_fixed_hysteresis_and_short_release();
    test_repeated_fast_and_slow_clicks();
    test_moving_profile_and_immediate_motion();
    test_force_drag_latches_until_lift();
    test_initial_hold_and_slow_precision();
    test_invalid_contact_and_bounds();
    test_taps_and_force_ownership();
    test_trace_contains_actual_fixed_levels();
    test_three_tap_rejection_and_missing_first_slot();
    puts("Fixed-level force, 3000 repeated clicks, drag, hold, tap and trace tests passed");
    return 0;
}
