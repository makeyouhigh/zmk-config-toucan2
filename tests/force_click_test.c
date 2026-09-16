/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include "../modules/azoteq/drivers/input/tps43_force.h"

static const struct tps43_force_config config = {
    .press_delta = 40, .release_delta = 20,
    .baseline_ms = 80, .debounce_ms = 32,
    .press_percent = 12, .release_percent = 6,
    .motion_threshold = 16, .drag_threshold = 64, .settle_ms = 120,
};

static int sample(struct tps43_force_state *s, int64_t t, uint16_t strength,
                  uint16_t x, uint16_t y) {
    return tps43_force_step(s, &config, t, 1, strength, true, x, y);
}

static void rest(struct tps43_force_state *s, int64_t t, uint16_t strength) {
    for (int i = 0; i <= 120; i += 8) {
        assert(sample(s, t + i, strength, 1000, 1000) == TPS43_FORCE_NONE);
    }
    assert(s->ready && !s->down);
}

static void press(struct tps43_force_state *s, int64_t t, uint16_t strength) {
    for (int i = 0; i < 32; i += 8) {
        assert(sample(s, t + i, strength, 1000, 1000) == TPS43_FORCE_NONE);
        assert(s->suppress_motion);
    }
    assert(sample(s, t + 32, strength, 1000, 1000) == TPS43_FORCE_PRESS);
    assert(s->down && s->tap_consumed && s->suppress_motion);
}

static void test_rest_landing_noise(void) {
    struct tps43_force_state s = {0};
    /* A gradual landing must not click against an artificially low baseline. */
    for (int t = 0; t <= 80; t += 8) {
        assert(sample(&s, t, (uint16_t)(200 + t * 10), 1000, 1000) == 0);
    }
    for (int t = 88; t <= 160; t += 8) {
        assert(sample(&s, t, 1000, 1000, 1000) == 0);
    }
    assert(s.ready && s.baseline == 1000 && !s.down);
    for (int t = 168; t < 60000; t += 8) {
        assert(sample(&s, t, 1000, 1000, 1000) == 0);
        assert(!s.suppress_motion);
    }
    assert(sample(&s, 60000, 1300, 1002, 1001) == 0);
    assert(s.suppress_motion);
    assert(sample(&s, 60008, 1000, 1002, 1001) == 0);
    assert(!s.down && !s.suppress_motion); /* A spike cannot leave motion frozen. */
    press(&s, 60016, 1300);
}

static void test_motion_then_press(void) {
    struct tps43_force_state s = {0};
    rest(&s, 0, 1000);
    /* Travel with large contact-strength changes must not trigger a click. */
    for (int t = 128, x = 1060; t <= 288; t += 8, x += 60) {
        assert(sample(&s, t, (uint16_t)(1500 + t), (uint16_t)x, 1000) == 0);
        assert(!s.down && !s.suppress_motion);
    }
    rest(&s, 296, 2000);
    press(&s, 424, 2400);
    assert(sample(&s, 464, 2400, 1200, 1000) == 0);
    assert(s.down && s.dragging && !s.suppress_motion && s.baseline == 2000);
}

static void test_relaxation_and_scaling(void) {
    struct tps43_force_state s = {0};
    rest(&s, 0, 3000);
    /* Relax a firm initial contact without lifting, then squeeze from rest. */
    for (int t = 128; t <= 928; t += 8) {
        assert(sample(&s, t, 1000, 1000, 1000) == 0);
    }
    assert(s.baseline >= 1000 && s.baseline < 1005);
    press(&s, 936, 1250);
    assert(tps43_force_threshold(1000, 40, 12) == 120);
    assert(tps43_force_threshold(200, 40, 12) == 40);
    assert(tps43_force_threshold(65535, 40, 12) == 7865);

    s = (struct tps43_force_state){0};
    rest(&s, 0, 200);
    press(&s, 128, 260);
    s = (struct tps43_force_state){0};
    rest(&s, 0, 4000);
    assert(sample(&s, 128, 4300, 1000, 1000) == 0);
    assert(!s.candidate); /* Raw minimum alone is insufficient at high strength. */
    press(&s, 136, 4700);
}

static void test_slow_move_before_strength_change(void) {
    struct tps43_force_state s = {0};
    rest(&s, 0, 1000);
    /* Movement begins first. A subsequent strength increase must not drag. */
    assert(sample(&s, 128, 1000, 1004, 1000) == 0);
    for (int t = 136, x = 1008; t <= 456; t += 8, x += 4) {
        assert(sample(&s, t, 1400, (uint16_t)x, 1000) == 0);
        assert(!s.down && !s.dragging);
    }
    /* After stopping, the elevated moving strength becomes the new rest. */
    for (int t = 464; t <= 584; t += 8) {
        assert(sample(&s, t, 1400, 1168, 1000) == 0);
        assert(!s.down);
    }
    assert(s.ready && s.baseline == 1400);
    for (int t = 592; t < 624; t += 8) {
        assert(sample(&s, t, 1700, 1168, 1000) == 0);
    }
    assert(sample(&s, 624, 1700, 1168, 1000) == TPS43_FORCE_PRESS);
    assert(sample(&s, 632, 1700, 1250, 1000) == 0);
    assert(s.dragging && !s.suppress_motion);
}

static void test_click_jitter_and_drag(void) {
    struct tps43_force_state s = {0};
    rest(&s, 0, 1000);
    /* Micro motion throughout the press decision is discarded. */
    for (int t = 128, x = 1002; t < 160; t += 8, x += 2) {
        assert(sample(&s, t, 1300, (uint16_t)x, 1000) == 0);
        assert(s.suppress_motion && !s.down);
    }
    assert(sample(&s, 160, 1300, 1010, 1000) == TPS43_FORCE_PRESS);
    assert(s.suppress_motion && !s.dragging);
    for (int t = 168; t <= 248; t += 8) {
        assert(sample(&s, t, 1300, 1018, 1010) == 0);
        assert(s.down && s.suppress_motion && !s.dragging);
    }
    assert(sample(&s, 256, 1300, 1066, 1000) == 0);
    assert(s.suppress_motion); /* Exactly at the dead-zone edge. */
    assert(sample(&s, 264, 1300, 1067, 1000) == 0);
    assert(s.dragging && !s.suppress_motion);
    assert(sample(&s, 272, 1300, 1002, 1000) == 0);
    assert(!s.suppress_motion); /* Returning to the anchor cannot re-lock a drag. */
    assert(sample(&s, 280, 1090, 1002, 1000) == 0);
    assert(s.down); /* Hysteresis maintains button down. */
    assert(sample(&s, 288, 1030, 1002, 1000) == 0);
    assert(s.suppress_motion);
    assert(sample(&s, 296, 1090, 1002, 1000) == 0);
    assert(s.down && !s.suppress_motion); /* Ignore a transient release. */
    for (int t = 304; t < 336; t += 8) {
        assert(sample(&s, t, 1000, 1002, 1000) == 0);
        assert(s.suppress_motion);
    }
    assert(sample(&s, 336, 1000, 1002, 1000) == TPS43_FORCE_RELEASE);
    assert(!s.down && s.suppress_motion);
    assert(sample(&s, 344, 1000, 1003, 1000) == 0);
    assert(s.suppress_motion);
    for (int t = 352; t <= 416; t += 8) {
        assert(sample(&s, t, 1000, 1003, 1000) == 0);
    }
    assert(s.ready && !s.suppress_motion);
    press(&s, 424, 1300); /* Another squeeze without lifting. */
}

static void test_safety(void) {
    struct tps43_force_state s = {0};
    rest(&s, 0, 1000);
    press(&s, 128, 1300);
    assert(tps43_force_step(&s, &config, 168, 0, 0, true, 0, 0) == -1);
    assert(s.tap_consumed && s.suppress_motion);
    rest(&s, 176, 1000);
    press(&s, 304, 1300);
    assert(tps43_force_step(&s, &config, 344, 2, 4000, true, 1000, 1000) == -1);
    for (int t = 352; t < 552; t += 8) {
        assert(sample(&s, t, 6000, 1000, 1000) == 0);
    }
    assert(s.blocked && !s.down);
    assert(tps43_force_step(&s, &config, 552, 0, 0, true, 0, 0) == 0);
    rest(&s, 560, 1000);
    press(&s, 688, 1300);
    assert(tps43_force_step(&s, &config, 728, 1, 1300, false, 1000, 1000) == -1);
    assert(s.blocked);
    tps43_force_step(&s, &config, 736, 0, 0, true, 0, 0);
    rest(&s, 744, 1000);
    press(&s, 872, 1300);
    assert(tps43_force_cancel(&s, true) == -1); /* I2C error / sleep / watchdog. */
    assert(tps43_force_cancel(&s, true) == 0);
    tps43_force_step(&s, &config, 912, 0, 0, true, 0, 0);
    rest(&s, 920, 1000);
    press(&s, 1048, 1300);
    assert(sample(&s, 1400, 1300, 1000, 1000) == -1);
    assert(s.blocked);
}

static void test_boundaries(void) {
    struct tps43_force_state s = {0};
    rest(&s, 0, 65000);
    for (int t = 128; t <= 168; t += 8) {
        assert(sample(&s, t, 65535, 1000, 1000) == 0);
    }
    assert(!s.down); /* Saturation cannot satisfy a percentage increase. */
    assert(sample(&s, 176, 1, 1000, 1000) == 0); /* No unsigned underflow. */
    assert(tps43_force_moved(0, 0, 65535, 65535, 40));
    assert(tps43_force_moved(65535, 65535, 0, 0, 40));
    assert(!tps43_force_moved(1000, 1040, 1000, 1000, 40));
}

int main(void) {
    test_rest_landing_noise();
    test_motion_then_press();
    test_slow_move_before_strength_change();
    test_relaxation_and_scaling();
    test_click_jitter_and_drag();
    test_safety();
    test_boundaries();
    puts("Force-click behavioural tests passed");
    return 0;
}
