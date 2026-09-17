/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include "../modules/azoteq/drivers/input/tps43_force.h"
#include "../modules/azoteq/drivers/input/tps43_tap.h"
#include "../modules/azoteq/drivers/input/tps43_three_tap.h"
#include <toucan/touch_lease.h>
#include <toucan/mouse_queue.h>

static const struct tps43_force_config config = {
    .press_delta = 30, .release_delta = 16, .press_percent = 8, .release_percent = 3,
    .baseline_ms = 40, .debounce_ms = 16, .settle_ms = 64,
    .motion_threshold = 6, .drag_threshold = 48,
    .moving_press_delta = 90, .moving_press_percent = 24, .motion_settle_ms = 32,
    .drag_hold_ms = 300, .repeat_ms = 400,
    .click_margin_percent = 25,
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

static void hold_for_drag(struct fixture *f, uint16_t strength) {
    while (f->t - f->s.pressed_ms < config.drag_hold_ms) {
        assert(sample(f,strength)==TPS43_FORCE_NONE);
        assert(f->s.down && !f->s.dragging && f->s.suppress_motion);
    }
    assert(f->s.drag_armed);
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
        assert(sample(&f,(uint16_t)(1100+i*10))==0);
        assert(!f.s.down && !f.s.suppress_motion);
    }
    rest(&f,1500);
    /* A squeeze now works at the new position, allowing centroid displacement. */
    f.x += 30; assert(sample(&f,2100)==0);
    f.x += 20; assert(sample(&f,2100)==0);
    f.x += 10; assert(sample(&f,2100)==TPS43_FORCE_PRESS);
    assert(f.s.suppress_motion);

    f=fresh(); rest(&f,1000);
    /* A gradual squeeze deforms the finger before reaching the full threshold. */
    f.x+=12; assert(sample(&f,1040)==0);
    f.x+=12; assert(sample(&f,1080)==0 && f.s.suppress_motion && !f.s.candidate);
    f.x+=12; assert(sample(&f,1140)==0);
    f.x+=12; assert(sample(&f,1180)==0);
    f.x+=12; assert(sample(&f,1220)==TPS43_FORCE_PRESS);

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
    assert(f.s.baseline==1000 && !f.s.suppress_motion);
    f.x=stopped_x+1; assert(sample(&f,1100)==0);
    f.x=stopped_x+3; assert(sample(&f,1100)==0);
    f.x=stopped_x+2; assert(sample(&f,1100)==TPS43_FORCE_NONE);
    f.x=stopped_x+1; assert(sample(&f,1100)==TPS43_FORCE_NONE);
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
        for (int i=0;i<2;i++) assert(sample(&slow,1000)==0);
        uint16_t origin=slow.x;
        /* Click jitter is held only inside the spatial deadband. Establish
         * intentional travel before testing moving pressure fluctuations. */
        while (slow.x-origin <= config.motion_threshold) {
            slow.x+=step; assert(sample(&slow,1000)==0);
            assert(!slow.s.down);
            assert(slow.s.suppress_motion == (slow.x-origin <= config.motion_threshold));
        }
        for (int i=0;i<100;i++) {
            slow.x+=step;
            assert(sample(&slow,(uint16_t)(1100+(i%3)*70))==0);
            assert(!slow.s.down && !slow.s.suppress_motion);
        }
    }
    struct fixture f=fresh(); rest(&f,1000); press(&f,1200);
    release(&f,1000);
    for (int i=0;i<2;i++) assert(sample(&f,1000)==0);
    f.x+=4; assert(sample(&f,1000)==0 && f.s.suppress_motion);
    f.x+=4; assert(sample(&f,1000)==0 && !f.s.suppress_motion);
    for (int i=0;i<100;i++) {
        f.x+=4;
        assert(sample(&f,(uint16_t)(1100+(i%3)*70))==0);
        assert(!f.s.down && !f.s.suppress_motion);
    }
    /* Relaxing during a drag keeps the button held until contact ends. */
    rest(&f,1000); press(&f,1200); hold_for_drag(&f,1200);
    f.x+=config.drag_threshold+1; assert(sample(&f,1200)==0 && f.s.dragging);
    for (int i=0;i<3;i++) {
        f.x+=10;
        assert(sample(&f,1000)==TPS43_FORCE_NONE && f.s.down && f.s.dragging);
        assert(!f.s.suppress_motion);
    }
    for (int i=0;i<30;i++) {
        f.x+=10;
        assert(sample(&f,(uint16_t)(1200+(i%3)*70))==0);
        assert(f.s.down && f.s.dragging && !f.s.suppress_motion);
    }
    assert(send_frame(&f,0,0,true)==TPS43_FORCE_RELEASE && !f.s.down);
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
        assert(!tap_frame(&s,start+40,1,true,false,1003));
        assert(tap_frame(&s,start+48,0,true,false,0));
        assert(!tap_frame(&s,start+56,0,true,false,0));
    }
    /* Leaving the tap area, even if returning, must not click. */
    assert(!tap_frame(&s,300,1,true,false,1000));
    assert(!tap_frame(&s,308,1,true,false,1040));
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
    hold_for_drag(&f,1300);
    f.x += 10; assert(sample(&f,1300)==0 && f.s.suppress_motion);
    f.x += config.drag_threshold; assert(sample(&f,1300)==0 && !f.s.suppress_motion);
    /* Reproduce the old stutter: pressure repeatedly crosses the release line. */
    for (int i=0;i<30;i++) {
        f.x += 8; assert(sample(&f,1020)==0);
        assert(f.s.down && !f.s.suppress_motion);
        f.x += 8; assert(sample(&f,1300)==0);
        assert(f.s.down && !f.s.suppress_motion);
    }
    for (int i=0;i<30;i++) {
        f.x += 8;
        assert(sample(&f,(uint16_t)(i%2 ? 0 : 500))==0);
        assert(f.s.down && f.s.dragging && !f.s.suppress_motion);
    }
    assert(send_frame(&f,0,0,true)==TPS43_FORCE_RELEASE);
    f.x += 8; assert(sample(&f,1000)==0 && !f.s.suppress_motion);
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

static void test_double_click_without_full_relaxation(void) {
    struct fixture f=fresh(); rest(&f,1000);
    int64_t start=f.t;
    press(&f,1300);
    /* The finger stays on the pad and does not return to the first 1000
     * baseline. The old code kept one continuous button-down for this trace. */
    release(&f,1180);
    assert(f.s.ready && !f.s.down);
    f.x += 12;
    press(&f,1350);
    release(&f,1210);
    assert(f.t-start==96 && !f.s.down && f.s.tap_consumed);
    assert(send_frame(&f,0,0,true)==TPS43_FORCE_NONE);

    /* A brief dip is not a release; two squeeze cycles must not chatter. */
    f=fresh(); rest(&f,1000); press(&f,1300);
    assert(sample(&f,1180)==0);
    assert(sample(&f,1300)==0 && f.s.down);
    for (int i=0;i<80;i++) {
        assert(sample(&f,(uint16_t)(i%2 ? 1280 : 1300))==0 && f.s.down);
    }
    release(&f,1180);
}

static void test_drag_latch_recovery(void) {
    for (int reason=0;reason<4;reason++) {
        struct fixture f=fresh(); rest(&f,1000); press(&f,1300);
        hold_for_drag(&f,1300);
        f.x += config.drag_threshold+1;
        assert(sample(&f,1300)==0 && f.s.dragging);
        for (int i=0;i<1000;i++) {
            f.x += (i%2 ? 2 : -2);
            assert(sample(&f,(uint16_t)(i%3 ? 500 : 0))==0);
            assert(f.s.down && f.s.dragging && !f.s.suppress_motion);
            assert(tps43_force_display_state(&f.s,1,true)==TOUCAN_TOUCH_PRESSED);
        }
        if (reason==0) assert(send_frame(&f,0,0,true)==TPS43_FORCE_RELEASE);
        if (reason==1) assert(send_frame(&f,1,500,false)==TPS43_FORCE_RELEASE);
        if (reason==2) {
            f.t += TPS43_FORCE_STALE_MS;
            assert(sample(&f,500)==TPS43_FORCE_RELEASE);
        }
        if (reason==3) assert(send_frame(&f,2,500,true)==TPS43_FORCE_RELEASE);
        assert(!f.s.down && !f.s.dragging);
        assert(send_frame(&f,0,0,true)==TPS43_FORCE_NONE);
        rest(&f,1000); press(&f,1300); release(&f,1000);
    }
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

static void test_drag_deadzone_and_unchanged_click(void) {
    struct fixture f=fresh(); rest(&f,1000); press(&f,1100);
    hold_for_drag(&f,1100);
    uint16_t anchor_x=f.x, anchor_y=f.y;
    /* These excursions used to start dragging after 24 units. They must now
     * stay a stationary click, even when repeated for longer than a click. */
    for (int i=0;i<100;i++) {
        f.x=(uint16_t)(anchor_x+(i%2 ? 48 : -48));
        f.y=(uint16_t)(anchor_y+(i%3 ? 30 : -30));
        assert(sample(&f,1100)==TPS43_FORCE_NONE);
        assert(f.s.down && !f.s.dragging && f.s.suppress_motion);
    }
    f.x=anchor_x+49;
    assert(sample(&f,1100)==TPS43_FORCE_NONE);
    assert(f.s.dragging && !f.s.suppress_motion); /* Same sample, no new timer. */
    f.x=anchor_x;
    assert(sample(&f,1100)==TPS43_FORCE_NONE && !f.s.suppress_motion);
    for (int i=0;i<3;i++) assert(sample(&f,1000)==0 && f.s.down && !f.s.suppress_motion);
    assert(send_frame(&f,0,0,true)==TPS43_FORCE_RELEASE);
    assert(!f.s.down);

    /* Small deformations may accompany two clicks without lifting. */
    f=fresh(); rest(&f,1000);
    for (int i=0;i<2;i++) {
        press(&f,1100); f.x+=30;
        assert(sample(&f,1100)==TPS43_FORCE_NONE && !f.s.dragging);
        release(&f,1000);
    }

    /* Increasing drag distance must NOT enlarge the v4 press candidate area
     * from 72 to 144 units or change its strength threshold. */
    f=fresh(); rest(&f,1000);
    assert(sample(&f,1100)==TPS43_FORCE_NONE && f.s.candidate);
    f.x+=73;
    assert(sample(&f,1100)==TPS43_FORCE_NONE);
    assert(!f.s.candidate && !f.s.down);
}

static void test_stationary_and_moving_thresholds(void) {
    struct fixture f=fresh(); rest(&f,1000);
    press(&f,1100); /* At rest, a 100-unit increase exceeds 8%. */
    release(&f,1000);
    f=fresh(); rest(&f,1000);
    for (int i=0;i<12;i++) { f.x+=12; assert(sample(&f,1000)==0); }
    for (int i=0;i<12;i++) {
        f.x+=12;
        assert(sample(&f,1150)==0 && !f.s.down && !f.s.suppress_motion);
    }
    /* The user explicitly permits a stronger click while already moving. */
    f=fresh(); rest(&f,1000);
    for (int i=0;i<12;i++) { f.x+=12; assert(sample(&f,1000)==0); }
    for (int i=0;i<3;i++) {
        f.x+=50;
        assert(sample(&f,1400)==(i==2 ? TPS43_FORCE_PRESS : TPS43_FORCE_NONE));
        assert(f.s.suppress_motion);
    }
    assert(f.s.down && !f.s.dragging);
    hold_for_drag(&f,1400);
    f.x+=49; assert(sample(&f,1400)==0 && f.s.dragging);
    assert(sample(&f,900)==0 && f.s.down && !f.s.suppress_motion);
    assert(send_frame(&f,0,0,true)==TPS43_FORCE_RELEASE);
}

static struct tps43_three_tap_result three_frame(struct tps43_three_tap_state *s,
                                                int t, int fingers, int x) {
    return tps43_three_tap_step(s,t,(uint8_t)fingers,true,false,(uint16_t)x,1000,200,16);
}
static void test_three_finger_taps_and_scroll_exit(void) {
    struct tps43_three_tap_state s={0};
    assert(!three_frame(&s,0,1,1000).click);
    assert(!three_frame(&s,16,2,1200).click);
    assert(three_frame(&s,32,3,1400).claimed);
    assert(!three_frame(&s,48,3,1403).click);
    assert(three_frame(&s,64,2,1200).claimed);
    assert(three_frame(&s,80,1,1000).claimed);
    struct tps43_three_tap_result r=three_frame(&s,96,0,0);
    assert(r.click && r.claimed); /* Exactly one middle click, on complete lift. */
    r=three_frame(&s,104,0,0); assert(!r.click && !r.claimed);
    /* Two-finger taps retain their native right-click path. */
    assert(!three_frame(&s,200,2,1000).claimed);
    r=three_frame(&s,248,0,0); assert(!r.click && !r.claimed);
    /* A hold, swipe, fourth finger, bad frame, timeout, or prior click rejects. */
    for (int reason=0;reason<7;reason++) {
        s=(struct tps43_three_tap_state){0};
        assert(three_frame(&s,0,3,1000).claimed);
        if (reason==0) three_frame(&s,208,3,1000);
        if (reason==1) { three_frame(&s,24,3,1020); three_frame(&s,40,3,1000); }
        if (reason==2) three_frame(&s,24,4,1000);
        if (reason==3) tps43_three_tap_step(&s,24,3,false,false,1000,1000,200,16);
        if (reason==4) tps43_three_tap_step(&s,24,3,true,true,1000,1000,200,16);
        if (reason==5) tps43_three_tap_cancel(&s);
        r=three_frame(&s,reason==0 || reason==6 ? 264 : 64,0,0);
        assert(!r.click && r.claimed);
    }
    assert(tps43_two_finger_motion(2,false));
    assert(!tps43_two_finger_motion(1,false)); /* Scroll flag remains, finger lifted. */
    assert(!tps43_two_finger_motion(0,false));
    assert(!tps43_two_finger_motion(3,true));
    assert(!tps43_two_finger_motion(2,true)); /* Uneven three-finger lift. */
}

static void test_squeeze_rebound_must_not_latch_drag(void) {
    struct fixture f=fresh(); rest(&f,1000);
    for (int click=0;click<2;click++) {
        press(&f,1300);
        /* Pressure deforms the finger after button-down, then relaxes. A
         * single 60-unit excursion must not permanently turn this into drag. */
        f.x+=60; assert(sample(&f,1300)==TPS43_FORCE_NONE);
        assert(!f.s.dragging && f.s.suppress_motion);
        f.x-=60; release(&f,1180);
        assert(!f.s.down);
    }
}

static void test_repeat_from_local_trough(void) {
    struct fixture f=fresh(); rest(&f,1000);
    press(&f,1100); release(&f,1060);
    assert(f.s.repeat_threshold==30 && !f.s.down);
    press(&f,1100); release(&f,1060);
    assert(!f.s.down && f.s.ready);
    /* A brief drop/rise or sub-band wobble is not another click. */
    for (int i=0;i<12;i++) assert(sample(&f,(uint16_t)(i%2 ? 1080 : 1060))==0);
    assert(!f.s.down);
    /* A trough between release and re-press is followed without waiting for
     * baseline filtering. The second squeeze still needs a full release band. */
    assert(sample(&f,1030)==0);
    assert(sample(&f,1060)==0);
    assert(sample(&f,1030)==0 && !f.s.down);
    press(&f,1080); release(&f,1030);
    for (int i=0;i<55;i++) assert(sample(&f,1030)==0);
    for (int i=0;i<8;i++) assert(sample(&f,1070)==0 && !f.s.down);
    /* Deliberate travel cancels repeat assistance. */
    f=fresh(); rest(&f,1000); press(&f,1100); release(&f,1060);
    f.x+=12; assert(sample(&f,1060)==0);
    assert(f.s.repeat_until_ms==0);
    for (int i=0;i<20;i++) {
        f.x+=12; assert(sample(&f,1100)==0 && !f.s.suppress_motion);
    }
}

static void test_early_motion_is_not_replayed_as_drag(void) {
    struct fixture f=fresh(); rest(&f,1000); press(&f,1300);
    f.x+=100; assert(sample(&f,1300)==0 && !f.s.dragging);
    hold_for_drag(&f,1300);
    assert(sample(&f,1300)==0 && !f.s.dragging);
    f.x+=48; assert(sample(&f,1300)==0 && !f.s.dragging);
    f.x++; assert(sample(&f,1300)==0 && f.s.dragging);
    assert(sample(&f,1000)==0 && f.s.down && !f.s.suppress_motion);
    assert(send_frame(&f,0,0,true)==TPS43_FORCE_RELEASE);
}

static int touch_hold_frame(struct fixture *f, int64_t now, uint8_t fingers,
                            uint16_t strength, bool valid) {
    struct tps43_force_config timed = config;
    timed.touch_hold_ms = 250;
    f->t = now;
    return tps43_force_step(&f->s,&timed,now,fingers,strength,valid,f->x,f->y);
}
static void touch_hold_rest(struct fixture *f, int64_t end) {
    for (int64_t t=0;t<=end;t+=8) {
        assert(touch_hold_frame(f,t,1,1000,true)==TPS43_FORCE_NONE);
        assert(!f->s.down && !f->s.suppress_motion);
    }
}
static void test_touch_hold_boundary_and_latch(void) {
    struct fixture f=fresh(); touch_hold_rest(&f,248);
    f.x+=7; assert(touch_hold_frame(&f,249,1,1000,true)==0);
    assert(!f.s.down); /* Movement before 250 ms restarts the rest interval. */
    for (int64_t t=257;t<499;t+=8) assert(touch_hold_frame(&f,t,1,1000,true)==0);
    f.x+=7; assert(touch_hold_frame(&f,499,1,1000,true)==TPS43_FORCE_PRESS);
    assert(f.s.dragging && f.s.down && f.s.tap_consumed && !f.s.suppress_motion);
    for (int i=1;i<=120;i++) {
        f.x++;
        assert(touch_hold_frame(&f,499+i*8,1,(uint16_t)(i%2 ? 0 : 1600),true)==0);
        assert(f.s.down && f.s.dragging && !f.s.suppress_motion);
    }
    assert(touch_hold_frame(&f,f.t+8,0,0,true)==TPS43_FORCE_RELEASE);
    assert(touch_hold_frame(&f,f.t+8,0,0,true)==TPS43_FORCE_NONE);
}
static void test_touch_hold_waiting_and_jitter(void) {
    struct fixture f=fresh();
    for (int64_t t=0;t<=4000;t+=8) {
        f.x=(uint16_t)(1000+(t%16 ? 3 : -3));
        assert(touch_hold_frame(&f,t,1,1000,true)==0);
        assert(!f.s.down && !f.s.suppress_motion);
    }
    /* Holding/jitter alone does not emit a click, even beyond double-click time. */
    assert(touch_hold_frame(&f,4008,0,0,true)==0);
    f=fresh(); touch_hold_rest(&f,248);
    f.x+=7; assert(touch_hold_frame(&f,250,1,1000,true)==TPS43_FORCE_PRESS);
}
static void test_touch_hold_travel_then_stop(void) {
    for (int step=1;step<=10;step+=9) {
        struct fixture f=fresh();
        for (int64_t t=0;t<=2000;t+=8) {
            f.x=(uint16_t)(f.x+step);
            assert(touch_hold_frame(&f,t,1,1000,true)==0);
            assert(!f.s.down && !f.s.suppress_motion);
        }
        for (int64_t t=2008;t<=2256;t+=8)
            assert(touch_hold_frame(&f,t,1,1000,true)==0);
        f.x+=7;
        assert(touch_hold_frame(&f,2264,1,1000,true)==TPS43_FORCE_PRESS);
        assert(!f.s.suppress_motion);
    }
}
static void test_touch_hold_force_priority(void) {
    struct fixture f=fresh(); touch_hold_rest(&f,400);
    /* Centroid shift during a gradual squeeze must not become timed drag. */
    f.x+=12; assert(touch_hold_frame(&f,408,1,1040,true)==0 && !f.s.dragging);
    f.x+=12; assert(touch_hold_frame(&f,416,1,1100,true)==0 && !f.s.dragging);
    f.x+=12; assert(touch_hold_frame(&f,424,1,1100,true)==0 && !f.s.dragging);
    assert(touch_hold_frame(&f,432,1,1100,true)==TPS43_FORCE_PRESS);
    assert(f.s.down && !f.s.dragging && f.s.suppress_motion);
    assert(touch_hold_frame(&f,440,1,1000,true)==0);
    assert(touch_hold_frame(&f,448,1,1000,true)==0);
    assert(touch_hold_frame(&f,456,1,1000,true)==TPS43_FORCE_RELEASE);
    /* Time passing between squeezes cannot take ownership of the button. */
    for (int64_t t=464;t<=744;t+=8) assert(touch_hold_frame(&f,t,1,1000,true)==0);
    assert(touch_hold_frame(&f,752,1,1100,true)==0);
    assert(touch_hold_frame(&f,760,1,1100,true)==0);
    assert(touch_hold_frame(&f,768,1,1100,true)==TPS43_FORCE_PRESS);
    assert(!f.s.dragging);
    assert(touch_hold_frame(&f,776,1,1000,true)==0);
    assert(touch_hold_frame(&f,784,1,1000,true)==0);
    assert(touch_hold_frame(&f,792,1,1000,true)==TPS43_FORCE_RELEASE);
    for (int64_t t=800;t<=1600;t+=8) assert(touch_hold_frame(&f,t,1,1000,true)==0);
    f.x+=7; assert(touch_hold_frame(&f,1608,1,1000,true)==0 && !f.s.dragging);
}
static void test_touch_hold_tap_and_gesture_recovery(void) {
    for (int drag=0;drag<2;drag++) {
        struct fixture f=fresh(); struct tps43_tap_state tap={0};
        int presses=0, releases=0, taps=0;
        int end=drag ? 320 : 120;
        for (int t=0;t<=end;t+=8) {
            bool lift=t==end;
            if (drag && t>=256 && !lift) f.x+=7;
            int event=touch_hold_frame(&f,t,lift ? 0 : 1,lift ? 0 : 1000,true);
            presses+=event==TPS43_FORCE_PRESS; releases+=event==TPS43_FORCE_RELEASE;
            taps+=tps43_tap_step(&tap,t,lift ? 0 : 1,true,f.s.tap_consumed,
                                 f.x,f.y,200,16);
        }
        assert(presses==drag && releases==drag && taps==!drag);
    }
    for (int reason=0;reason<3;reason++) {
        struct fixture f=fresh(); touch_hold_rest(&f,248);
        f.x+=7; assert(touch_hold_frame(&f,250,1,1000,true)==TPS43_FORCE_PRESS);
        int64_t cancel=reason==2 ? 501 : 258;
        assert(touch_hold_frame(&f,cancel,reason==0 ? 2 : 1,1000,
                                 reason!=1)==TPS43_FORCE_RELEASE);
        for (int64_t t=cancel+8;t<cancel+400;t+=8) {
            f.x+=7; assert(touch_hold_frame(&f,t,1,1000,true)==0);
            assert(!f.s.down); /* No rearm after scroll/error without a lift. */
        }
        assert(touch_hold_frame(&f,f.t+8,0,0,true)==0);
        int64_t start=f.t+8;
        for (int64_t t=start;t<start+250;t+=8)
            assert(touch_hold_frame(&f,t,1,1000,true)==0);
        f.x+=7;
        assert(touch_hold_frame(&f,start+250,1,1000,true)==TPS43_FORCE_PRESS);
    }
}

/* Feed the actual force events and unsuppressed relative motion into the host
 * mouse queue. Two button pairs alone are insufficient: their cursor positions
 * and timing also have to satisfy the host's double-click requirements. */
static void test_repeat_jitter_button_order_and_position(void) {
    const int pauses[] = {0,40,160,300};
    for (unsigned k=0;k<sizeof(pauses)/sizeof(*pauses);k++) {
        for (int direction=-1;direction<=1;direction+=2) {
            struct fixture f=fresh(); touch_hold_rest(&f,120);
            struct toucan_mouse_queue q={0};
            int presses=0, releases=0, cursor_x=0, cursor_y=0;
            int64_t first_press=0, second_press=0;
            const uint16_t strength[]={1100,1100,1100,1060,1060,1060,
                1060,1060,1060,1100,1100,1100,1060,1060,1060};
            for (unsigned i=0;i<sizeof(strength)/sizeof(*strength);i++) {
                if (i==6) {
                    for (int delay=0;delay<pauses[k];delay+=8)
                        assert(touch_hold_frame(&f,f.t+8,1,1060,true)==0);
                }
                int delta=i>=6 ? direction : 0;
                f.x=(uint16_t)(f.x+delta); f.y=(uint16_t)(f.y-delta);
                int e=touch_hold_frame(&f,f.t+8,1,strength[i],true);
                if (e) {
                    if (e==TPS43_FORCE_PRESS) {
                        if (!presses) first_press=f.t; else second_press=f.t;
                        presses++;
                        assert(cursor_x==0 && cursor_y==0);
                    } else { releases++; }
                    toucan_mouse_push(&q,(struct toucan_mouse_packet){
                        .buttons=f.s.down ? 1 : 0,.motion_ms=f.t},f.t);
                }
                if (!f.s.suppress_motion) {
                    cursor_x+=delta; cursor_y-=delta;
                    if (delta) toucan_mouse_push(&q,(struct toucan_mouse_packet){
                        .buttons=f.s.down ? 1 : 0,.x=(int16_t)delta,
                        .y=(int16_t)-delta,.motion_ms=f.t},f.t);
                }
                assert(!f.s.dragging);
            }
            assert(presses==2 && releases==2 && !f.s.down);
            assert(second_press-first_press<500);
            assert(cursor_x==0 && cursor_y==0 && q.count==4);
            for (int i=0;i<4;i++) {
                struct toucan_mouse_packet packet;
                assert(toucan_mouse_pop(&q,&packet,f.t));
                assert(packet.buttons==(i%2 ? 0 : 1) && packet.x==0 && packet.y==0);
            }
            /* Deliberate travel exits on the very next frame, even during the
             * existing post-release quiet time. No old deltas are replayed. */
            f.x=(uint16_t)(f.x+direction*7);
            assert(touch_hold_frame(&f,f.t+8,1,1060,true)==0);
            assert(!f.s.down && !f.s.suppress_motion && !f.s.repeat_until_ms);
        }
    }
}
static void test_repeat_candidate_travel_and_expiry(void) {
    struct fixture f=fresh(); rest(&f,1000); press(&f,1100); release(&f,1060);
    f.x++;
    assert(sample(&f,1100)==0 && f.s.candidate);
    f.x+=TPS43_FORCE_PRESS_TRAVEL_LIMIT+1;
    assert(sample(&f,1100)==0 && !f.s.down && !f.s.candidate &&
           !f.s.suppress_motion && !f.s.repeat_until_ms);
    f=fresh(); rest(&f,1000); press(&f,1100); release(&f,1060);
    int64_t end=f.s.repeat_until_ms;
    while (f.t+8<end) {
        assert(sample(&f,1060)==0 && !f.s.down && f.s.suppress_motion);
    }
    f.t=end-8;
    assert(sample(&f,1060)==0 && !f.s.suppress_motion);
}

static void test_v9_lock_and_click_are_separate(void) {
    const uint16_t baselines[]={100,1000,50000};
    for (unsigned k=0;k<sizeof(baselines)/sizeof(*baselines);k++) {
        uint16_t base=baselines[k];
        uint16_t lock=(uint16_t)tps43_force_threshold(base,30,8);
        uint16_t click=(uint16_t)((lock*125U+99U)/100U);
        struct fixture f=fresh(); rest(&f,base);
        assert(sample(&f,(uint16_t)(base+lock-1))==0 && !f.s.suppress_motion);
        f=fresh(); rest(&f,base);
        /* Holding the old click strength must neither click nor be absorbed
         * into a rising baseline, even after the 250 ms touch-hold interval. */
        for (int i=0;i<250;i++) {
            f.x=(uint16_t)(1000+(i%2 ? 20 : -20));
            assert(touch_hold_frame(&f,f.t+8,1,(uint16_t)(base+lock),true)==0);
            assert(f.s.prepress && !f.s.candidate && !f.s.down && f.s.suppress_motion);
            assert(f.s.baseline==base && !f.s.dragging);
        }
        assert(sample(&f,(uint16_t)(base+click-1))==0 && !f.s.candidate);
        press(&f,(uint16_t)(base+click));
        assert(!f.s.dragging);
        release(&f,base);
    }
    /* Releasing an unclicked squeeze restores motion in this same report. */
    struct fixture f=fresh(); rest(&f,1000);
    assert(sample(&f,1080)==0 && f.s.prepress);
    for (int i=0;i<10;i++) assert(sample(&f,1041)==0 && f.s.suppress_motion);
    f.x+=8;
    assert(sample(&f,1040)==0 && !f.s.prepress && !f.s.suppress_motion && !f.s.down);
}

static void test_v9_moving_lock_and_drag_bypass(void) {
    struct fixture f=fresh(); rest(&f,1000);
    for (int i=0;i<12;i++) { f.x+=12; assert(sample(&f,1000)==0); }
    f.x+=12;
    assert(sample(&f,1239)==0 && !f.s.suppress_motion && !f.s.down);
    f=fresh(); rest(&f,1000);
    for (int i=0;i<12;i++) { f.x+=12; assert(sample(&f,1000)==0); }
    f.x+=12;
    assert(sample(&f,1240)==0 && f.s.prepress && f.s.suppress_motion);
    /* Stopping while already squeezing cannot silently select the easier
     * stationary click threshold and generate a click at unchanged force. */
    for (int i=0;i<50;i++) {
        assert(sample(&f,1240)==0 && !f.s.down && f.s.suppress_motion);
        assert(f.s.baseline==1000);
    }
    assert(sample(&f,1299)==0 && !f.s.candidate);
    press(&f,1300);
    hold_for_drag(&f,1300);
    f.x+=49; assert(sample(&f,1300)==0 && f.s.dragging && !f.s.suppress_motion);
    for (int i=0;i<50;i++) {
        f.x+=12;
        assert(sample(&f,(uint16_t)(i%2 ? 1000 : 1300))==0 &&
               f.s.down && f.s.dragging && !f.s.suppress_motion);
    }
    assert(send_frame(&f,0,0,true)==TPS43_FORCE_RELEASE);
}

static void test_v9_prepress_cancel_and_repeat(void) {
    for (int reason=0;reason<5;reason++) {
        struct fixture f=fresh(); rest(&f,1000);
        assert(sample(&f,1080)==0 && f.s.prepress);
        if (reason==0) {
            f.x+=TPS43_FORCE_PRESS_TRAVEL_LIMIT+1;
            assert(sample(&f,1080)==0 && !f.s.suppress_motion);
        } else {
            if (reason==4) f.t+=TPS43_FORCE_STALE_MS;
            assert(send_frame(&f,reason==1 ? 0 : reason==2 ? 2 : 1,1080,reason!=3)==0);
        }
        assert(!f.s.down && !f.s.prepress && !f.s.candidate);
    }
    struct fixture f=fresh(); rest(&f,1000);
    assert(sample(&f,1080)==0 && f.s.suppress_motion && !f.s.candidate);
    press(&f,1100); release(&f,1060);
    /* The second squeeze also locks first and then clicks, from the actual
     * relaxed trough. It never waits for the first resting baseline to return. */
    f.x++;
    assert(sample(&f,1090)==0 && f.s.prepress && !f.s.candidate && f.s.suppress_motion);
    for (int i=0;i<4;i++) {
        f.x++;
        assert(sample(&f,1097)==0 && !f.s.down && f.s.suppress_motion);
        assert(f.s.baseline==1060);
    }
    press(&f,1098); release(&f,1060);
    assert(!f.s.down && f.s.tap_consumed);
}

int main(void) {
    test_v9_lock_and_click_are_separate();
    test_v9_moving_lock_and_drag_bypass();
    test_v9_prepress_cancel_and_repeat();
    test_repeat_jitter_button_order_and_position();
    test_repeat_candidate_travel_and_expiry();
    test_touch_hold_boundary_and_latch();
    test_touch_hold_waiting_and_jitter();
    test_touch_hold_travel_then_stop();
    test_touch_hold_force_priority();
    test_touch_hold_tap_and_gesture_recovery();
    puts("test_squeeze_rebound_must_not_latch_drag"); fflush(stdout); test_squeeze_rebound_must_not_latch_drag();
    puts("test_repeat_from_local_trough"); fflush(stdout); test_repeat_from_local_trough();
    puts("test_early_motion_is_not_replayed_as_drag"); fflush(stdout); test_early_motion_is_not_replayed_as_drag();
    puts("test_stationary_and_moving_thresholds"); fflush(stdout); test_stationary_and_moving_thresholds();
    puts("test_three_finger_taps_and_scroll_exit"); fflush(stdout); test_three_finger_taps_and_scroll_exit();
    puts("test_double_click_without_full_relaxation"); fflush(stdout); test_double_click_without_full_relaxation();
    puts("test_drag_latch_recovery"); fflush(stdout); test_drag_latch_recovery();
    puts("test_rest_noise_and_taps"); fflush(stdout); test_rest_noise_and_taps();
    puts("test_movement_order_and_reposition"); fflush(stdout); test_movement_order_and_reposition();
    puts("test_stop_squeeze_with_jitter"); fflush(stdout); test_stop_squeeze_with_jitter();
    puts("test_motion_after_click_stays_fluid"); fflush(stdout); test_motion_after_click_stays_fluid();
    puts("test_software_taps"); fflush(stdout); test_software_taps();
    puts("test_drag_without_repeated_freezes"); fflush(stdout); test_drag_without_repeated_freezes();
    puts("test_drag_deadzone_and_unchanged_click"); fflush(stdout); test_drag_deadzone_and_unchanged_click();
    puts("test_force_double_click"); fflush(stdout); test_force_double_click();
    puts("test_strength_and_safety"); fflush(stdout); test_strength_and_safety();
    puts("test_lost_touch_release_and_heartbeat"); fflush(stdout); test_lost_touch_release_and_heartbeat();
    puts("Force click, drag, double click and touch-release recovery tests passed");
    return 0;
}
