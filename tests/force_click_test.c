/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include "../modules/azoteq/drivers/input/tps43_force.h"
#include "../modules/azoteq/drivers/input/tps43_tap.h"
#include "../modules/azoteq/drivers/input/tps43_hold.h"
#include <toucan/packed_xy.h>
#include <toucan/touch_lease.h>

static const struct tps43_force_config config = {
    .press_delta = 30, .release_delta = 16, .press_percent = 8, .release_percent = 3,
    .baseline_ms = 40, .debounce_ms = 16, .settle_ms = 64,
    .motion_threshold = 6, .drag_threshold = 24,
    .drag_delay_ms = 180, .repeat_ms = 250, .repeat_distance = 64,
};
struct fixture { struct tps43_force_state s; int64_t t; uint16_t x, y; };
static struct fixture fresh(void) { return (struct fixture){.x=1000, .y=1000}; }
static int send_frame(struct fixture *f, uint8_t fingers, uint16_t strength, bool valid) {
    f->t += 8;
    return tps43_force_step(&f->s, &config, f->t, fingers, strength, valid, f->x, f->y);
}
static int sample(struct fixture *f, uint16_t strength) { return send_frame(f,1,strength,true); }
static void rest(struct fixture *f, uint16_t strength) {
    for (int i=0;i<32;i++) assert(sample(f,strength)==0);
    assert(f->s.ready && !f->s.down);
}
static void press(struct fixture *f, uint16_t strength) {
    assert(sample(f,strength)==0);
    assert(sample(f,strength)==0);
    assert(sample(f,strength)==TPS43_FORCE_PRESS);
    assert(f->s.down && f->s.suppress_motion);
}
static void release(struct fixture *f, uint16_t strength) {
    assert(sample(f,strength)==0);
    assert(sample(f,strength)==0);
    assert(sample(f,strength)==TPS43_FORCE_RELEASE);
}
static void wait_drag_ready(struct fixture *f, uint16_t strength) {
    for (int i=0;i<24;i++) {
        assert(sample(f,strength)==0 && f->s.suppress_motion);
    }
    assert(f->s.drag_ready && !f->s.dragging);
}
static void finish_repeat_guard(struct fixture *f) {
    for (int i=0;i<32;i++) assert(sample(f,1000)==0);
    assert(!f->s.suppress_motion);
}

static void test_rest_noise_and_taps(void) {
    struct fixture f=fresh();
    for (int i=0;i<12;i++) assert(sample(&f,(uint16_t)(200+i*80))==0);
    rest(&f,1000);
    for (int i=0;i<7500;i++) assert(sample(&f,1000)==0);
    assert(sample(&f,1300)==0);
    assert(sample(&f,1000)==0);
    assert(!f.s.down && !f.s.suppress_motion);
    press(&f,1300);
    assert(send_frame(&f,0,0,true)==-1 && f.s.tap_consumed);

    /* Restore ordinary quick taps, including two successive contacts. */
    for (int i=0;i<2;i++) {
        assert(sample(&f,1000)==0);
        assert(!f.s.tap_consumed);
        assert(send_frame(&f,0,0,true)==0);
        assert(!f.s.tap_consumed);
    }
}

static void test_movement_order_and_reposition(void) {
    struct fixture f=fresh(); rest(&f,1000);
    f.x += 4; assert(sample(&f,1000)==0); /* Movement precedes rising strength. */
    for (int i=0;i<40;i++) {
        f.x += 4;
        assert(sample(&f,(uint16_t)(1300+i*10))==0);
        assert(!f.s.down && !f.s.suppress_motion);
    }
    rest(&f,1700);
    /* A squeeze now works at the new position, allowing centroid displacement. */
    f.x += 30; assert(sample(&f,2100)==0);
    f.x += 20; assert(sample(&f,2100)==0);
    f.x += 10; assert(sample(&f,2100)==TPS43_FORCE_PRESS);
    assert(f.s.suppress_motion);

    f=fresh(); rest(&f,1000);
    /* A gradual squeeze deforms the finger before reaching the full threshold. */
    f.x+=12; assert(sample(&f,1040)==0);
    f.x+=12; assert(sample(&f,1080)==0);
    f.x+=12; assert(sample(&f,1140)==0);
    f.x+=12; assert(sample(&f,1180)==TPS43_FORCE_PRESS);
    f.x+=12; assert(sample(&f,1220)==0);

    f=fresh(); rest(&f,1000);
    for (int i=0;i<20;i++) {
        f.x += 60;
        assert(sample(&f,1400)==0); /* Fast travel also cancels a nascent candidate. */
    }
    assert(!f.s.down);
}

static void test_stop_squeeze_with_jitter(void) {
    struct fixture f=fresh(); rest(&f,1000);
    for (int i=0;i<30;i++) {
        f.x += 12;
        assert(sample(&f,1000)==0 && !f.s.suppress_motion);
    }
    uint16_t stopped_x=f.x;
    /* Begin adding force immediately after stopping, with a wobbling centroid.
     * The old guard absorbed these samples into baseline for at least 48 ms. */
    f.x=stopped_x+2; assert(sample(&f,1040)==0);
    assert(f.s.baseline==1000 && f.s.suppress_motion);
    f.x=stopped_x+1; assert(sample(&f,1100)==0);
    f.x=stopped_x+3; assert(sample(&f,1100)==0);
    f.x=stopped_x+2; assert(sample(&f,1100)==TPS43_FORCE_PRESS);
    release(&f,1000);

    f=fresh(); rest(&f,1000);
    /* Bounded jitter cannot perpetually reset movement settling. */
    for (int i=0;i<60;i++) {
        f.x=(uint16_t)(1000+(i%2 ? 3 : -3));
        assert(sample(&f,1000)==0 && !f.s.suppress_motion);
    }
    press(&f,1100); /* 10% rise: deliberately below the old 12% threshold. */
}

static void test_motion_after_click_stays_fluid(void) {
    for (uint16_t step=1;step<=6;step++) {
        struct fixture slow=fresh(); rest(&slow,1000);
        press(&slow,1200); release(&slow,1000);
        slow.x+=step; assert(sample(&slow,1000)==0);
        slow.x+=step; assert(sample(&slow,1000)==0 && !slow.s.suppress_motion);
        for (int i=0;i<100;i++) {
            slow.x+=step;
            assert(sample(&slow,(uint16_t)(1100+(i%3)*70))==0);
            assert(!slow.s.down && !slow.s.suppress_motion);
        }
    }
    struct fixture f=fresh(); rest(&f,1000); press(&f,1200);
    release(&f,1000);
    finish_repeat_guard(&f);
    f.x+=4; assert(sample(&f,1000)==0 && !f.s.suppress_motion);
    for (int i=0;i<100;i++) {
        f.x+=4;
        assert(sample(&f,(uint16_t)(1100+(i%3)*70))==0);
        assert(!f.s.down && !f.s.suppress_motion);
    }
    /* Releasing during a drag must not re-arm another squeeze while travelling. */
    rest(&f,1000); press(&f,1200);
    wait_drag_ready(&f,1200);
    f.x+=30; assert(sample(&f,1200)==0 && f.s.dragging);
    for (int i=0;i<3;i++) {
        f.x+=10;
        assert(sample(&f,1000)==(i==2 ? TPS43_FORCE_RELEASE : TPS43_FORCE_NONE));
        assert(!f.s.suppress_motion);
    }
    for (int i=0;i<30;i++) {
        f.x+=10;
        assert(sample(&f,(uint16_t)(1200+(i%3)*70))==0);
        assert(!f.s.down && !f.s.suppress_motion);
    }
}

static bool tap_frame(struct tps43_tap_state *s, int64_t t, uint8_t fingers,
                       bool valid, bool consumed, uint16_t x) {
    return tps43_tap_step(s,t,fingers,valid,consumed,x,1000,200,16);
}

static void test_software_taps(void) {
    struct tps43_tap_state s={0};
    /* A tap clicks on lift; no wait for a potential second tap. */
    for (int64_t start=0;start<200;start+=100) {
        assert(!tap_frame(&s,start,1,true,false,1000));
        assert(s.suppress_motion);
        assert(!tap_frame(&s,start+40,1,true,false,1003));
        assert(s.suppress_motion);
        assert(tap_frame(&s,start+48,0,true,false,0));
        assert(!tap_frame(&s,start+56,0,true,false,0));
    }
    /* Leaving the tap area, even if returning, must not click. */
    assert(!tap_frame(&s,300,1,true,false,1000));
    assert(!tap_frame(&s,308,1,true,false,1040));
    assert(!s.suppress_motion); /* Travel starts without waiting for tap timeout. */
    assert(!tap_frame(&s,316,1,true,false,1000));
    assert(!tap_frame(&s,324,0,true,false,0));
    assert(!tap_frame(&s,400,1,true,false,1000));
    assert(!tap_frame(&s,601,0,true,false,0));
    assert(!tap_frame(&s,700,1,true,false,1000));
    assert(!tap_frame(&s,716,1,true,true,1000));
    assert(!tap_frame(&s,740,0,true,true,0));
    assert(!tap_frame(&s,800,1,true,false,1000));
    assert(!tap_frame(&s,808,2,true,false,1000));
    assert(!tap_frame(&s,816,1,true,false,1000));
    assert(!tap_frame(&s,824,0,true,false,0));
    assert(!tap_frame(&s,900,1,true,false,1000));
    assert(!tap_frame(&s,908,1,false,false,1000));
    assert(!tap_frame(&s,916,0,true,false,0));
    assert(!tap_frame(&s,1000,1,true,false,1000));
    tps43_tap_cancel(&s); /* Sensor watchdog / I2C recovery cannot make a tap. */
    assert(!tap_frame(&s,1016,0,true,false,0));
    assert(!tps43_tap_step(&s,1100,1,true,false,1000,1000,1000,16));
    assert(!tps43_tap_step(&s,1400,0,true,false,0,0,1000,16));
}

static void test_drag_without_repeated_freezes(void) {
    struct fixture f=fresh(); rest(&f,1000); press(&f,1300);
    wait_drag_ready(&f,1300);
    f.x += 10; assert(sample(&f,1300)==0 && f.s.suppress_motion);
    f.x += 20; assert(sample(&f,1300)==0 && !f.s.suppress_motion);
    /* Reproduce the old stutter: pressure repeatedly crosses the release line. */
    for (int i=0;i<30;i++) {
        f.x += 8; assert(sample(&f,1020)==0);
        assert(f.s.down && !f.s.suppress_motion);
        f.x += 8; assert(sample(&f,1300)==0);
        assert(f.s.down && !f.s.suppress_motion);
    }
    release(&f,1000);
    assert(!f.s.suppress_motion);
    f.x += 8; assert(sample(&f,1000)==0 && !f.s.suppress_motion);
}

static void test_click_target_lock(void) {
    struct fixture f=fresh(); rest(&f,1000);
    f.x+=5; assert(sample(&f,1040)==0 && f.s.suppress_motion);
    f.x+=5; assert(sample(&f,1140)==0 && f.s.suppress_motion);
    f.x+=5; assert(sample(&f,1140)==0 && f.s.suppress_motion);
    f.x+=5; assert(sample(&f,1140)==TPS43_FORCE_PRESS && f.s.suppress_motion);
    /* Click deformation beyond the old 24-unit gate cannot instantly drag. */
    for (int i=0;i<12;i++) {
        f.x+=5;
        assert(sample(&f,1140)==0 && f.s.suppress_motion && !f.s.dragging);
    }
    release(&f,1000);
    /* The repeat window preserves calibration, without freezing normal XY. */
    assert(sample(&f,1000)==0);
    assert(sample(&f,1000)==0 && !f.s.suppress_motion);
    /* Sideways motion during the second squeeze remains suppressed. */
    for (int i=0;i<3;i++) {
        f.x+=3;
        assert(sample(&f,1140)==(i==2 ? TPS43_FORCE_PRESS : TPS43_FORCE_NONE));
        assert(f.s.suppress_motion && !f.s.dragging);
    }
    release(&f,1000);
    /* A large intentional move exits the post-click lock without waiting. */
    f.x+=65;
    assert(sample(&f,1000)==0);
    f.x+=10;
    assert(sample(&f,1000)==0 && !f.s.suppress_motion);
    /* An incomplete weak squeeze cannot freeze pointing indefinitely. */
    f=fresh(); rest(&f,1000);
    for (int i=0;i<20;i++) assert(sample(&f,1040)==0);
    assert(!f.s.suppress_motion && !f.s.down);
}

static void test_timed_press_hold(void) {
    struct tps43_hold_state s={0};
    for (int64_t t=0;t<300;t+=8) {
        assert(tps43_hold_step(&s,t,1,true,false,1000,1000,300,16,24)==0);
        assert(!s.down);
    }
    assert(tps43_hold_step(&s,304,1,true,false,1000,1000,300,16,24)==TPS43_FORCE_PRESS);
    assert(s.down && !s.dragging);
    assert(tps43_hold_step(&s,312,1,true,false,1010,1000,300,16,24)==0 && !s.dragging);
    assert(tps43_hold_step(&s,320,1,true,false,1040,1000,300,16,24)==0 && s.dragging);
    for (int i=0;i<60;i++) {
        assert(tps43_hold_step(&s,328+i*8,1,true,false,(uint16_t)(1040+i*3),1000,
                               300,16,24)==0 && s.down && s.dragging);
    }
    assert(tps43_hold_step(&s,816,0,true,false,0,0,300,16,24)==TPS43_FORCE_RELEASE);
    /* Early travel or a force click must not later trigger a second held click. */
    assert(tps43_hold_step(&s,1100,1,true,false,1000,1000,300,16,24)==0);
    assert(tps43_hold_step(&s,1108,1,true,false,1020,1000,300,16,24)==0);
    for (int i=0;i<100;i++) {
        assert(tps43_hold_step(&s,1116+i*8,1,true,false,1020,1000,300,16,24)==0);
    }
    assert(!s.down && s.blocked);
    tps43_hold_step(&s,1920,0,true,false,0,0,300,16,24);
    tps43_hold_step(&s,2000,1,true,false,1000,1000,300,16,24);
    tps43_hold_step(&s,2008,1,true,true,1000,1000,300,16,24);
    assert(s.blocked && !s.down);
    /* A stale or invalid held contact must release the button. */
    tps43_hold_cancel(&s,false);
    tps43_hold_step(&s,3000,1,true,false,1000,1000,0,16,24);
    assert(s.down);
    assert(tps43_hold_step(&s,3008,2,true,false,1000,1000,0,16,24)==TPS43_FORCE_RELEASE);
    tps43_hold_cancel(&s,false);
    tps43_hold_step(&s,4000,1,true,false,1000,1000,0,16,24);
    assert(tps43_hold_step(&s,4300,1,true,false,1000,1000,0,16,24)==TPS43_FORCE_RELEASE);
}

static void test_packed_pointer(void) {
    const int16_t values[]={INT16_MIN,-1000,-1,0,1,1000,INT16_MAX};
    for (unsigned int i=0;i<sizeof(values)/sizeof(values[0]);i++) {
        for (unsigned int j=0;j<sizeof(values)/sizeof(values[0]);j++) {
            int32_t packed=toucan_pack_xy(values[i],values[j]);
            assert(toucan_unpack_x(packed)==values[i]);
            assert(toucan_unpack_y(packed)==values[j]);
        }
    }
}

static void test_force_double_click(void) {
    struct fixture f=fresh(); rest(&f,1000);
    int64_t start=f.t;
    press(&f,1300); release(&f,1000);
    assert(f.s.ready); /* No 80 ms recalibration between clicks. */
    press(&f,1300); release(&f,1000);
    assert(f.t-start==96 && !f.s.down);
    assert(send_frame(&f,0,0,true)==0 && f.s.tap_consumed);
}

static void test_strength_and_safety(void) {
    struct fixture f=fresh(); rest(&f,3000);
    for (int i=0;i<120;i++) assert(sample(&f,1000)==0);
    assert(f.s.baseline<1005); press(&f,1300);
    assert(send_frame(&f,2,2000,true)==-1);
    for (int i=0;i<20;i++) assert(sample(&f,4000)==0);
    assert(f.s.blocked);
    send_frame(&f,0,0,true); rest(&f,1000); press(&f,1300);
    assert(send_frame(&f,1,1300,false)==-1);
    send_frame(&f,0,0,true); rest(&f,1000); press(&f,1300);
    f.t+=TPS43_FORCE_STALE_MS;
    assert(sample(&f,1300)==-1 && f.s.blocked);
    send_frame(&f,0,0,true); rest(&f,1000); press(&f,1300);
    assert(tps43_force_cancel(&f.s,true)==-1);
    assert(tps43_force_cancel(&f.s,true)==0);
    f=fresh(); rest(&f,65000);
    for (int i=0;i<10;i++) assert(sample(&f,65535)==0);
    assert(sample(&f,1)==0 && !f.s.down);
    assert(tps43_force_moved(0,0,65535,65535,24));
    assert(tps43_force_display_state(&f.s,0,true)==TOUCAN_TOUCH_NONE);
}

static void test_lost_touch_release_and_heartbeat(void) {
    struct toucan_touch_lease l={0};
    toucan_touch_lease_update(&l,100,TOUCAN_TOUCH_CONTACT);
    assert(!toucan_touch_lease_expire(&l,849,750));
    /* A lost lift report or dead sensor must not leave TP on indefinitely. */
    assert(toucan_touch_lease_expire(&l,850,750));
    assert(l.state==TOUCAN_TOUCH_NONE);
    toucan_touch_lease_update(&l,900,TOUCAN_TOUCH_CONTACT);
    toucan_touch_lease_update(&l,1150,TOUCAN_TOUCH_CONTACT);
    assert(!toucan_touch_lease_expire(&l,1700,750));
    toucan_touch_lease_button(&l,1708,true);
    toucan_touch_lease_update(&l,1716,TOUCAN_TOUCH_PRESSED);
    /* Button-up lost in transport, but the following state snapshot survives. */
    toucan_touch_lease_update(&l,1800,TOUCAN_TOUCH_CONTACT);
    assert(l.release_pending);
    toucan_touch_lease_button(&l,1808,false);
    assert(!l.release_pending);
    toucan_touch_lease_button(&l,1900,true);
    assert(toucan_touch_lease_expire(&l,2650,750) && l.release_pending);
    toucan_touch_lease_button(&l,2658,false);
    assert(!l.left_down && !l.release_pending);
}

int main(void) {
    test_rest_noise_and_taps();
    test_movement_order_and_reposition();
    test_stop_squeeze_with_jitter();
    test_motion_after_click_stays_fluid();
    test_software_taps();
    test_click_target_lock();
    test_timed_press_hold();
    test_packed_pointer();
    test_drag_without_repeated_freezes();
    test_force_double_click();
    test_strength_and_safety();
    test_lost_touch_release_and_heartbeat();
    puts("Force click, timed hold, cursor lock, double click, packed XY and recovery tests passed");
    return 0;
}
