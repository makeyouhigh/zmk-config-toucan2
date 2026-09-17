/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include "../modules/azoteq/drivers/input/tps43_force.h"
#include "../modules/azoteq/drivers/input/tps43_tap.h"
#include "../modules/azoteq/drivers/input/tps43_three_tap.h"
#include <toucan/touch_lease.h>
#include <toucan/diagnostics.h>

static const struct tps43_force_config config = {
    .press_delta = 30, .release_delta = 16, .press_percent = 8, .release_percent = 3,
    .baseline_ms = 40, .debounce_ms = 16, .settle_ms = 64,
    .motion_threshold = 6, .drag_threshold = 48,
    .moving_press_delta = 90, .moving_press_percent = 24, .motion_settle_ms = 32,
    .drag_hold_ms = 300, .repeat_ms = 400,
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
        slow.x+=step; assert(sample(&slow,1000)==0);
        for (int i=0;i<100;i++) {
            slow.x+=step;
            assert(sample(&slow,(uint16_t)(1100+(i%3)*70))==0);
            assert(!slow.s.down && !slow.s.suppress_motion);
        }
    }
    struct fixture f=fresh(); rest(&f,1000); press(&f,1200);
    release(&f,1000);
    for (int i=0;i<2;i++) assert(sample(&f,1000)==0);
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

static void test_diagnostic_snapshot_loss(void) {
    for (int missing=-1;missing<TOUCAN_DIAG_SOURCE_WORDS;missing++) {
        struct toucan_diag_source s={0};
        assert(!toucan_diag_source_step(&s,TOUCAN_DIAG_CODE_BASE,123));
        for (int i=0;i<TOUCAN_DIAG_SOURCE_WORDS;i++) {
            if (i!=missing) assert(!toucan_diag_source_step(&s,
                (uint16_t)(TOUCAN_DIAG_CODE_BASE+1+i),(uint32_t)(100+i)));
        }
        bool completed=toucan_diag_source_step(&s,TOUCAN_DIAG_CODE_END,123);
        assert(completed==(missing<0));
        assert(s.drops==(missing<0 ? 0U : 1U));
        assert(!toucan_diag_source_step(&s,TOUCAN_DIAG_CODE_END,123));
    }
    struct toucan_diag_source s={0};
    assert(!toucan_diag_source_step(&s,TOUCAN_DIAG_CODE_BASE,321));
    for (int i=0;i<TOUCAN_DIAG_SOURCE_WORDS;i++)
        assert(!toucan_diag_source_step(&s,(uint16_t)(TOUCAN_DIAG_CODE_BASE+1+i),7));
    assert(!toucan_diag_source_step(&s,TOUCAN_DIAG_CODE_END,322));
    assert(s.drops==1);
}

int main(void) {
    test_diagnostic_snapshot_loss();
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
