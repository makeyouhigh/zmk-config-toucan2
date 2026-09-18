/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include "../modules/azoteq/drivers/input/tps43_force.h"
#include "../modules/azoteq/drivers/input/tps43_tap.h"
#include "../modules/azoteq/drivers/input/tps43_three_tap.h"
#define CONFIG_TOUCAN_FORCE_TRACE 1
#include "../modules/azoteq/drivers/input/tps43_trace.h"
#include <toucan/touch_lease.h>
#include <toucan/force_status.h>
#include <string.h>

static const struct tps43_force_config config = {
    .lock_level=4250, .press_level=4500, .release_level=4000,
    .moving_lock_level=4750, .moving_press_level=5000,
    .debounce_ms=8, .pulse_delta=200, .motion_threshold=6, .motion_settle_ms=32,
    .drag_threshold=16, .touch_hold_ms=250,
};
struct fixture { struct tps43_force_state s; int64_t t; uint16_t x,y; };
static struct fixture fresh(void) { return (struct fixture){.x=1000,.y=1000}; }
static int frame(struct fixture *f, uint8_t fingers, uint16_t strength, bool valid) {
    f->t+=8;
    return tps43_force_step(&f->s,&config,f->t,fingers,strength,valid,f->x,f->y);
}
static int sample(struct fixture *f, uint16_t strength) { return frame(f,1,strength,true); }
static void qualify(struct fixture *f, uint16_t strength) {
    assert(sample(f,strength)==0);
    assert(sample(f,strength)==0);
    assert(f->s.pulse_ready && !f->s.down && !f->s.dragging);
}
static void test_fixed_floors_and_waveform(void) {
    const uint16_t initial[]={1,2000,3300,4000,4249,4400,5100,9000};
    for (unsigned n=0;n<sizeof(initial)/sizeof(initial[0]);n++) {
        struct fixture f=fresh();
        for(int i=0;i<150;i++) {
            assert(sample(&f,initial[n])==0);
            assert(!f.s.suppress_motion && !f.s.down && !f.s.hold_cancelled);
        }
        assert(f.s.press_level==4500 && f.s.lock_level==4250);
        uint16_t peak=initial[n]+300;
        if(peak<4500) peak=4500;
        qualify(&f,peak);
        assert(sample(&f,peak-200)==TPS43_FORCE_CLICK);
        assert(!f.s.down && !f.s.dragging && f.s.tap_consumed);
        assert(frame(&f,0,0,true)==0); /* no duplicate ordinary tap */
    }
    struct fixture f=fresh(); sample(&f,3000);
    for(int i=0;i<50;i++) assert(sample(&f,4400)==0); /* below click floor */
    assert(!f.s.suppress_motion); /* prepress lock also expires */
    qualify(&f,4500);
    assert(sample(&f,4300)==TPS43_FORCE_CLICK);
    assert(!f.s.down);
}
static void test_two_peaks_above_release_and_noise(void) {
    struct fixture f=fresh(); sample(&f,5100);
    qualify(&f,5800);
    assert(sample(&f,5500)==TPS43_FORCE_CLICK);
    for(int i=0;i<4;i++) assert(sample(&f,5400)==0);
    qualify(&f,5650); /* smaller second peak, never below absolute release */
    assert(sample(&f,5450)==TPS43_FORCE_CLICK);
    assert(!f.s.down && !f.s.pulse_ready);
    for(int i=0;i<200;i++) {
        assert(sample(&f,i%2?5480:5380)==0);
        assert(!f.s.down && !f.s.dragging && !f.s.suppress_motion);
    }
    assert(frame(&f,0,0,true)==0);
    sample(&f,3300);
    assert(sample(&f,5500)==0); /* isolated one-frame spike rejected */
    assert(sample(&f,3300)==0 && !f.s.pulse_ready);
    f=fresh(); sample(&f,5100);
    assert(sample(&f,5301)==0 && f.s.suppress_motion);
    assert(sample(&f,5299)==0 && !f.s.suppress_motion);
    for(int i=0;i<200;i++) {
        assert(sample(&f,i%2?5301:5299)==0 && !f.s.suppress_motion);
    }
}
static void test_repeated_fast_and_slow_clicks(void) {
    const int held_frames[]={2,8,25};
    for(unsigned k=0;k<sizeof(held_frames)/sizeof(held_frames[0]);k++) {
        struct fixture f=fresh(); sample(&f,5100);
        int clicks=0;
        for(int n=0;n<1000;n++) {
            for(int i=0;i<held_frames[k];i++) {
                assert(sample(&f,5500)==0 && !f.s.down);
            }
            clicks+=sample(&f,5100)==TPS43_FORCE_CLICK;
            assert(!f.s.down && !f.s.dragging);
        }
        assert(clicks==1000);
        assert(sample(&f,5100)==0 && !f.s.suppress_motion);
        f.x+=20;
        assert(sample(&f,5100)==0 && !f.s.suppress_motion && !f.s.down);
    }
}
static void test_moving_profile_and_motion(void) {
    struct fixture f=fresh();
    for(int i=0;i<100;i++) {
        f.x+=3;
        assert(sample(&f,3300)==0 && !f.s.suppress_motion && !f.s.down);
    }
    for(int i=0;i<25;i++) {
        f.x+=12;
        assert(sample(&f,4600)==0 && !f.s.suppress_motion);
        assert(f.s.press_level==5000);
    }
    assert(sample(&f,4750)==0 && f.s.suppress_motion);
    f.x+=25; qualify(&f,5000);
    assert(f.s.press_level==5000); /* no profile switch within squeeze */
    assert(sample(&f,4800)==TPS43_FORCE_CLICK);
    assert(sample(&f,3300)==0 && !f.s.suppress_motion);
    for(int i=0;i<4;i++) assert(sample(&f,3300)==0);
    qualify(&f,4500); assert(f.s.press_level==4500);
    assert(sample(&f,4300)==TPS43_FORCE_CLICK);
}
static void test_force_never_drags_or_locks_forever(void) {
    struct fixture f=fresh(); sample(&f,5100);
    f.x+=13; sample(&f,5100); /* ordinary travel cancels initial hold */
    qualify(&f,12000);
    for(int i=0;i<500;i++) {
        f.x+=4;
        assert(sample(&f,12000)==0 && !f.s.down && !f.s.dragging);
        if(i>=15) {
            assert(!f.s.suppress_motion);
            assert(tps43_force_display_state(&f.s,1,true)==TOUCAN_TOUCH_CONTACT);
        }
    }
    assert(sample(&f,11800)==TPS43_FORCE_CLICK);
    for(int i=0;i<200;i++) {
        f.x+=4; assert(sample(&f,11800)==0 && !f.s.suppress_motion && !f.s.down);
    }
    assert(frame(&f,0,0,true)==0);
}
static void test_initial_hold_only_drag(void) {
    const uint16_t values[]={100,3300,9000};
    for(unsigned n=0;n<sizeof(values)/sizeof(values[0]);n++) {
        struct fixture f=fresh(); sample(&f,values[n]);
        for(int i=0;i<32;i++) assert(sample(&f,values[n])==0 && !f.s.down);
        /* Slow movement after a completed hold is allowed; no 120 ms deadline. */
        for(int i=0;i<16;i++) {
            f.x++;
            for(int j=0;j<4;j++) assert(sample(&f,values[n])==0);
        }
        f.x++;
        assert(sample(&f,values[n])==TPS43_FORCE_PRESS && f.s.dragging && f.s.down);
        for(int i=0;i<200;i++) {
            f.x++; assert(sample(&f,i%2?0:UINT16_MAX)==0 && f.s.down && !f.s.suppress_motion);
        }
        assert(frame(&f,0,0,true)==TPS43_FORCE_RELEASE);
    }
    struct fixture f=fresh();
    for(int i=0;i<300;i++) {
        f.x++; assert(sample(&f,3300)==0 && !f.s.suppress_motion && !f.s.down);
    }
    assert(f.s.hold_cancelled); /* travel before 250 ms cannot become drag later */
    for(int i=0;i<50;i++) assert(sample(&f,3300)==0);
    f.x+=30; assert(sample(&f,3300)==0 && !f.s.dragging);
    /* A qualified force pulse takes priority over the timed hold. */
    f=fresh(); sample(&f,3300); qualify(&f,5500);
    for(int i=0;i<32;i++) assert(sample(&f,5500)==0);
    f.x+=17; assert(sample(&f,5500)==0 && !f.s.dragging && !f.s.down);
    assert(f.s.pulse_ready && f.s.hold_cancelled);
    assert(sample(&f,3300)==TPS43_FORCE_CLICK && !f.s.down);
    assert(frame(&f,0,0,true)==0);
    /* A completed click cannot unexpectedly arm hold during a double click. */
    f=fresh(); sample(&f,3300); qualify(&f,5500);
    assert(sample(&f,5300)==TPS43_FORCE_CLICK);
    for(int i=0;i<50;i++) assert(sample(&f,5300)==0);
    f.x+=17; assert(sample(&f,5300)==0 && !f.s.dragging);
}
static void test_shallow_valley_and_force_priority(void) {
    struct fixture f=fresh(); sample(&f,5100); qualify(&f,5500);
    assert(sample(&f,5477)==0); /* observed 23-unit valley */
    assert(sample(&f,5477)==0);
    assert(sample(&f,5486)==TPS43_FORCE_CLICK); /* small recovery, still below peak */
    assert(!f.s.down && f.s.hold_cancelled);
    qualify(&f,5620); /* second rise 134, above the fixed absolute floor */
    assert(sample(&f,5590)==0);
    assert(sample(&f,5590)==0);
    assert(sample(&f,5590)==TPS43_FORCE_CLICK);
    for(int i=0;i<100;i++) assert(sample(&f,5590)==0);
    assert(!f.s.down);
    /* Outside the repeat window, small rises do not qualify new clicks. */
    assert(sample(&f,5700)==0); assert(sample(&f,5700)==0);
    assert(!f.s.pulse_ready);
    /* One-frame drop then recovery must not split a sustained press. */
    f=fresh(); sample(&f,5100); qualify(&f,5500);
    for(int i=0;i<100;i++) {
        assert(sample(&f,i%2?5500:5460)==0);
        assert(!f.s.down);
    }
    assert(frame(&f,0,0,true)==TPS43_FORCE_CLICK);
    /* The squeeze and a >16-unit centroid shift arrive after 250 ms together. */
    f=fresh(); sample(&f,3300);
    for(int i=0;i<32;i++) assert(sample(&f,3300)==0);
    f.x+=30; qualify(&f,5500);
    assert(!f.s.down && !f.s.dragging && f.s.hold_cancelled);
    assert(sample(&f,5300)==TPS43_FORCE_CLICK);
    /* Once that force contact owns clicks, later travel never starts drag. */
    for(int i=0;i<200;i++) {
        f.x++; assert(sample(&f,5300)==0 && !f.s.down);
    }
}
static void test_invalid_lift_and_bounds(void) {
    assert(tps43_force_config_valid(&config));
    struct tps43_force_config bad=config; bad.pulse_delta=0;
    assert(!tps43_force_config_valid(&bad));
    struct fixture f=fresh(); sample(&f,65000); qualify(&f,UINT16_MAX);
    assert(sample(&f,65335)==TPS43_FORCE_CLICK); /* no uint16 overflow */
    assert(frame(&f,0,0,true)==0);
    sample(&f,3300); qualify(&f,5000);
    assert(frame(&f,0,0,true)==TPS43_FORCE_CLICK); /* lift completes one pulse */
    assert(frame(&f,0,0,true)==0);
    sample(&f,3300); qualify(&f,5000);
    assert(frame(&f,2,5000,true)==0 && f.s.blocked); /* invalidates pending pulse */
    assert(frame(&f,0,0,true)==0);
    sample(&f,3300); qualify(&f,5000);
    assert(frame(&f,1,5000,false)==0 && f.s.blocked);
    assert(frame(&f,0,0,true)==0);
    sample(&f,3300); qualify(&f,5000);
    f.t+=TPS43_FORCE_STALE_MS;
    assert(frame(&f,0,0,true)==0); /* no late click after stale input */
    sample(&f,3300);
    for(int i=0;i<32;i++) sample(&f,3300);
    f.x+=17; assert(sample(&f,3300)==TPS43_FORCE_PRESS);
    assert(frame(&f,2,5000,true)==TPS43_FORCE_RELEASE);
    assert(frame(&f,0,0,true)==0);
    sample(&f,3300);
    for(int i=0;i<32;i++) sample(&f,3300);
    f.x+=17; assert(sample(&f,3300)==TPS43_FORCE_PRESS);
    f.t+=TPS43_FORCE_STALE_MS;
    assert(sample(&f,3300)==TPS43_FORCE_RELEASE);
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
    sample(&f,3300);
    tps43_tap_step(&tap,f.t,1,true,f.s.tap_consumed,f.x,f.y,200,16);
    qualify(&f,5000);
    assert(!tps43_tap_step(&tap,f.t,1,true,f.s.tap_consumed,f.x,f.y,200,16));
    assert(frame(&f,0,0,true)==TPS43_FORCE_CLICK);
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
    struct fixture f=fresh(); sample(&f,3300); qualify(&f,5200);
    struct tps43_trace_record r={.sample_ms=(uint32_t)f.t};
    tps43_trace_state(&r,&f.s,&config);
    assert(r.lock_level==4250 && r.press_level==4500 && r.release_level==4000);
    assert(r.after_flags&(1<<11)); /* pending pulse, no held button */
    assert(!(r.after_flags&((1<<2)|(1<<8))));
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

static void test_adjustable_levels(void) {
    const struct toucan_force_levels original={4250,4500,4000,4750,5000};
    struct toucan_force_levels v=original;
    assert(toucan_force_levels_valid(&v));
    for (unsigned command=0;command<=FORCE_RELEASE_DOWN;command++) {
        v=original;
        if (command==FORCE_RELEASE_UP) v.release-=100;
        const struct toucan_force_levels start=v;
        assert(toucan_force_levels_adjust(&v,command,100));
        assert(toucan_force_levels_valid(&v));
        assert(v.moving_lock-v.lock==500 && v.moving_press-v.press==500);
        assert(toucan_force_levels_adjust(&v,command^1,100));
        assert(v.lock==start.lock && v.press==start.press && v.release==start.release);
    }
    /* Reject overlap, underflow, overflow and malformed commands atomically. */
    const uint32_t bad[][2]={{FORCE_LOCK_UP,250},{FORCE_LOCK_DOWN,250},
        {FORCE_CLICK_DOWN,250},{FORCE_RELEASE_UP,250},{FORCE_CLICK_UP,0},
        {FORCE_CLICK_UP,2001},{FORCE_CLICK_UP,UINT32_MAX},{UINT32_MAX,100}};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        v=original;
        assert(!toucan_force_levels_adjust(&v,bad[i][0],bad[i][1]));
        assert(v.lock==original.lock && v.press==original.press && v.release==original.release &&
               v.moving_lock==original.moving_lock && v.moving_press==original.moving_press);
    }
    v=(struct toucan_force_levels){10,20,1,30,65535};
    assert(!toucan_force_levels_adjust(&v,FORCE_RELEASE_DOWN,1) && v.release==1);
    assert(!toucan_force_levels_adjust(&v,FORCE_CLICK_UP,1) && v.press==20 && v.moving_press==65535);
    v=original;
    assert(toucan_force_levels_adjust(&v,FORCE_CLICK_UP,100));
    struct tps43_force_config runtime=config;
    tps43_force_set_levels(&runtime,&v);
    assert(tps43_force_config_valid(&runtime));
    assert(runtime.press_level==4600 && runtime.moving_press_level==5100);
    assert(runtime.touch_hold_ms==250 && runtime.debounce_ms==8);
    struct tps43_force_state s={0};
    assert(tps43_force_step(&s,&runtime,0,1,4000,true,1000,1000)==0);
    assert(tps43_force_step(&s,&runtime,8,1,4500,true,1000,1000)==0 && !s.down);
    assert(tps43_force_step(&s,&runtime,16,1,4600,true,1000,1000)==0);
    assert(tps43_force_step(&s,&runtime,24,1,4600,true,1000,1000)==0 && s.pulse_ready);
    assert(toucan_force_levels_adjust(&v,FORCE_RELEASE_DOWN,100));
    /* Pending settings leave the active contact's release boundary unchanged. */
    assert(tps43_force_step(&s,&runtime,32,1,4000,true,1000,1000)==TPS43_FORCE_CLICK);
    assert(tps43_force_step(&s,&runtime,40,0,0,true,1000,1000)==0);
    tps43_force_set_levels(&runtime,&v);
    assert(runtime.release_level==3900);
}

static void test_force_limits(void) {
    const struct { struct toucan_force_levels value; uint32_t blocked; } cases[]={
        {{1500,3000,1000,2000,3500},FORCE_LOCK_DOWN},
        {{5000,6000,1000,5500,6500},FORCE_LOCK_UP},
        {{1500,2000,1000,2000,2500},FORCE_CLICK_DOWN},
        {{2000,6000,1000,2500,6500},FORCE_CLICK_UP},
        {{2000,3000,1000,2500,3500},FORCE_RELEASE_DOWN},
        {{4500,5000,4000,5000,5500},FORCE_RELEASE_UP},
    };
    for (unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        struct toucan_force_levels v=cases[i].value;
        assert(toucan_force_levels_valid(&v));
        assert(!toucan_force_levels_adjust(&v,cases[i].blocked,1));
        assert(memcmp(&v,&cases[i].value,sizeof(v))==0);
        assert(toucan_force_levels_adjust(&v,cases[i].blocked^1,100));
        assert(toucan_force_levels_adjust(&v,cases[i].blocked,100));
        assert(memcmp(&v,&cases[i].value,sizeof(v))==0);
        assert(!toucan_force_levels_adjust(&v,cases[i].blocked,2000));
        assert(memcmp(&v,&cases[i].value,sizeof(v))==0);
    }
    struct toucan_force_levels observed={3550,4500,3000,4050,5000};
    for (int i=0;i<5;i++) assert(toucan_force_levels_adjust(&observed,FORCE_CLICK_DOWN,100));
    assert(observed.lock==3550 && observed.press==4000 && observed.release==3000 &&
           observed.moving_lock==4050 && observed.moving_press==4500);
}

static void test_force_status_transfer(void) {
    const struct toucan_force_levels a={3550,4500,3000,4050,5000};
    const struct toucan_force_levels b={3550,4000,3000,4050,4500};
    struct toucan_force_levels out={0};
    struct toucan_force_status_rx rx={0};
    /* No predicted/partial values; changes commit only after all three parts. */
    assert(!toucan_force_status_receive(&rx,0x30,toucan_force_status_pack(&a,1,0),&out));
    assert(!toucan_force_status_receive(&rx,0x32,toucan_force_status_pack(&a,1,2),&out));
    assert(out.lock==0);
    assert(toucan_force_status_receive(&rx,0x31,toucan_force_status_pack(&a,1,1),&out));
    assert(memcmp(&out,&a,sizeof(a))==0);
    assert(!toucan_force_status_receive(&rx,0x30,toucan_force_status_pack(&a,2,0),&out));
    for (unsigned gen=3;gen<=31;gen++) {
        assert(!toucan_force_status_receive(&rx,0x31,toucan_force_status_pack(&b,gen,1),&out));
        assert(!toucan_force_status_receive(&rx,0x32,toucan_force_status_pack(&b,gen,2),&out));
    }
    assert(memcmp(&out,&a,sizeof(a))==0); /* lost first part, retain last complete snapshot */
    assert(toucan_force_status_receive(&rx,0x30,toucan_force_status_pack(&b,31,0),&out));
    assert(memcmp(&out,&b,sizeof(b))==0);
    for (unsigned i=0;i<3;i++) {
        bool done=toucan_force_status_receive(&rx,0x30+i,toucan_force_status_pack(&a,1,i),&out);
        assert(done==(i==2)); /* generation wraps */
    }
    assert(memcmp(&out,&a,sizeof(a))==0);
    struct toucan_force_levels bad=a; bad.press=7000;
    for (unsigned i=0;i<3;i++) assert(!toucan_force_status_receive(&rx,0x30+i,
        toucan_force_status_pack(&bad,2,i),&out));
    assert(memcmp(&out,&a,sizeof(a))==0);
    assert(!toucan_force_status_receive(&rx,0x18,2,&out));
    assert(!toucan_force_status_receive(&rx,0x30,-1,&out));
    assert(!toucan_force_status_receive(&rx,0x30,0,&out));
}

int main(void) {
    test_shallow_valley_and_force_priority();
    test_force_limits(); test_force_status_transfer(); test_adjustable_levels();
    test_fixed_floors_and_waveform(); test_two_peaks_above_release_and_noise();
    test_repeated_fast_and_slow_clicks(); test_moving_profile_and_motion();
    test_force_never_drags_or_locks_forever(); test_initial_hold_only_drag();
    test_invalid_lift_and_bounds(); test_taps_and_force_ownership();
    test_trace_contains_actual_fixed_levels(); test_three_tap_rejection_and_missing_first_slot();
    puts("3000 pulse clicks, high valleys/plateaus, independent 250 ms hold, taps and trace passed");
    return 0;
}
