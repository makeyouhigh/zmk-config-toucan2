/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include "../modules/azoteq/drivers/input/tps43_force.h"

static const struct tps43_force_config config = {180, 100, 50, 20};

static void rest(struct tps43_force_state *s, int64_t start, uint16_t strength) {
    for (int i = 0; i <= 50; i += 10) {
        assert(tps43_force_step(s, &config, start + i, 1, strength, true) == 0);
    }
    assert(s->ready && !s->down);
}

static void press(struct tps43_force_state *s, int64_t start, uint16_t strength) {
    assert(tps43_force_step(s, &config, start, 1, strength, true) == 0);
    assert(tps43_force_step(s, &config, start + 10, 1, strength, true) == 0);
    assert(tps43_force_step(s, &config, start + 20, 1, strength, true) == 1);
    assert(s->down);
}

int main(void) {
    struct tps43_force_state s = {0};

    /* A resting finger never becomes a timed click, even over a long hold. */
    rest(&s, 0, 1000);
    for (int t = 60; t < 60000; t += 10) {
        assert(tps43_force_step(&s, &config, t, 1, 1000, true) == 0);
    }
    assert(!s.down);

    /* A transient spike must not click; sustained force presses exactly once. */
    assert(tps43_force_step(&s, &config, 60000, 1, 1300, true) == 0);
    assert(tps43_force_step(&s, &config, 60010, 1, 1000, true) == 0);
    press(&s, 60020, 1300);
    for (int t = 60050; t < 61000; t += 10) {
        assert(tps43_force_step(&s, &config, t, 1, 1150, true) == 0);
        assert(s.down); /* hysteresis keeps dragging between the thresholds */
    }

    /* Releasing force while still touching releases; another squeeze clicks. */
    assert(tps43_force_step(&s, &config, 61000, 1, 1050, true) == 0);
    assert(tps43_force_step(&s, &config, 61010, 1, 1150, true) == 0);
    assert(s.down); /* a release spike is also debounced */
    assert(tps43_force_step(&s, &config, 61020, 1, 1050, true) == 0);
    assert(tps43_force_step(&s, &config, 61040, 1, 1050, true) == -1);
    press(&s, 61050, 1300);
    assert(tps43_force_step(&s, &config, 61080, 0, 0, true) == -1);
    assert(tps43_force_step(&s, &config, 61090, 0, 0, true) == 0);

    /* Each new contact gets its own baseline, without unsigned underflow. */
    rest(&s, 62000, 3000);
    assert(tps43_force_step(&s, &config, 62060, 1, 1000, true) == 0);
    press(&s, 62070, 3300);

    /* Scrolling releases a click and cannot re-click as one finger lifts first. */
    assert(tps43_force_step(&s, &config, 62100, 2, 4000, true) == -1);
    for (int t = 62110; t < 62300; t += 10) {
        assert(tps43_force_step(&s, &config, t, 1, 6000, true) == 0);
    }
    assert(s.blocked && !s.down);
    assert(tps43_force_step(&s, &config, 62300, 0, 0, true) == 0);
    rest(&s, 62400, 1000);
    press(&s, 62460, 1300);

    /* Palm/invalid data, transport failure and sleep all release immediately. */
    assert(tps43_force_step(&s, &config, 62490, 0, 0, false) == -1);
    assert(s.blocked);
    tps43_force_step(&s, &config, 62500, 0, 0, true);
    rest(&s, 62600, 1000);
    press(&s, 62660, 1300);
    assert(tps43_force_cancel(&s, true) == -1);
    assert(tps43_force_cancel(&s, true) == 0);
    tps43_force_step(&s, &config, 62700, 0, 0, true);
    rest(&s, 62800, 1000);
    press(&s, 62860, 1300);
    assert(tps43_force_step(&s, &config, 64000, 1, 1300, true) == -1);
    assert(s.blocked);

    /* Full-range 16-bit strengths must not overflow the delta calculation. */
    tps43_force_step(&s, &config, 65000, 0, 0, true);
    rest(&s, 65100, 65000);
    press(&s, 65160, 65535);
    assert(tps43_force_step(&s, &config, 65190, 1, 64000, true) == 0);
    assert(tps43_force_step(&s, &config, 65210, 1, 64000, true) == -1);

    puts("Force-click behavioural tests passed");
    return 0;
}
