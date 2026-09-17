/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <toucan/mouse_queue.h>
#include <toucan/ble_mouse_policy.h>
#include <toucan/mouse_window.h>

static struct toucan_mouse_packet packet(int64_t t, uint16_t buttons, int16_t x) {
    return (struct toucan_mouse_packet){.buttons=buttons, .x=x, .y=-x, .motion_ms=t};
}
static void test_continuous_motion(void) {
    const int periods[] = {8, 15, 30, 60, 100, 500};
    for (unsigned int i=0; i<sizeof(periods)/sizeof(periods[0]); i++) {
        struct toucan_mouse_queue q={0};
        struct toucan_mouse_packet p;
        int64_t next=periods[i];
        int16_t expected=0;
        /* Ten minutes at 125 Hz, even with a very slow consumer. */
        for (int64_t t=0; t<600000; t+=8) {
            toucan_mouse_push(&q,packet(t,0,2),t);
            expected+=2;
            assert(q.count==1);
            if (t>=next) {
                assert(toucan_mouse_pop(&q,&p,t));
                assert(p.x==expected && p.y==-expected);
                assert(t-p.motion_ms<=TOUCAN_MOUSE_MOTION_MAX_AGE_MS);
                expected=0;
                next=t+periods[i];
            }
        }
    }
}
static void test_edges_and_drag(void) {
    struct toucan_mouse_queue q={0}; struct toucan_mouse_packet p;
    /* Two presses/releases survive a 0.5 s transport stall and busy movement. */
    for (int i=0;i<4;i++) {
        uint16_t buttons=(i%2)==0 ? 1 : 0;
        for (int j=0;j<10;j++) {
            int64_t t=i*80+j*8;
            toucan_mouse_push(&q,packet(t,buttons,2),t);
        }
    }
    assert(q.count==4);
    for (int i=0;i<4;i++) {
        assert(toucan_mouse_pop(&q,&p,800));
        assert(p.buttons==((i%2)==0 ? 1 : 0));
        assert(p.x==0 && p.y==0); /* No replay of stale cursor travel. */
    }
    assert(!toucan_mouse_pop(&q,&p,800));
    toucan_mouse_push(&q,packet(900,1,5),900);
    toucan_mouse_push(&q,packet(908,1,5),908);
    toucan_mouse_push(&q,packet(916,0,0),916);
    assert(toucan_mouse_pop(&q,&p,920) && p.buttons==1 && p.x==10);
    assert(toucan_mouse_pop(&q,&p,920) && p.buttons==0 && p.x==0);
}
static void test_retry_disconnect_overflow(void) {
    struct toucan_mouse_queue q={0}; struct toucan_mouse_packet p;
    toucan_mouse_push(&q,packet(0,1,5),0);
    assert(toucan_mouse_pop(&q,&p,0));
    toucan_mouse_push(&q,packet(8,1,6),8);
    toucan_mouse_push(&q,packet(16,0,0),16);
    toucan_mouse_retry(&q,p,20);
    assert(q.count==2);
    assert(toucan_mouse_pop(&q,&p,20) && p.buttons==1 && p.x==11);
    assert(toucan_mouse_pop(&q,&p,20) && p.buttons==0);
    toucan_mouse_push(&q,packet(30,1,5),30);
    q=(struct toucan_mouse_queue){0}; /* Connection epoch reset. */
    assert(!toucan_mouse_pop(&q,&p,40));
    for (int i=0;i<100;i++) {
        toucan_mouse_push(&q,packet(i,(i%2)==0 ? 1 : 0,0),i);
        assert(q.count<=TOUCAN_MOUSE_QUEUE_CAPACITY);
    }
    while (toucan_mouse_pop(&q,&p,120)) {}
    assert(p.buttons==0);
    assert(toucan_mouse_add(INT16_MAX,1)==INT16_MAX);
    assert(toucan_mouse_add(INT16_MIN,-1)==INT16_MIN);
}
static void test_completion_epochs(void) {
    struct toucan_mouse_window w = {0};
    uint32_t a = toucan_mouse_window_acquire(&w, 10);
    uint32_t b = toucan_mouse_window_acquire(&w, 20);
    assert(a && b && a != b);
    assert(!toucan_mouse_window_available(&w));
    assert(!toucan_mouse_window_acquire(&w, 10));
    toucan_mouse_window_disconnect(&w, 10);
    uint32_t c = toucan_mouse_window_acquire(&w, 10);
    assert(c && c != a);
    assert(!toucan_mouse_window_complete(&w, a)); /* Old connection callback. */
    assert(!toucan_mouse_window_available(&w));
    assert(toucan_mouse_window_complete(&w, b)); /* Other peer unaffected. */
    assert(!toucan_mouse_window_complete(&w, b)); /* Duplicate completion. */
    assert(toucan_mouse_window_complete(&w, c));
    assert(!toucan_mouse_window_complete(&w, 0));
    w.generation = 0x3fffffffU;
    a = toucan_mouse_window_acquire(&w, 10);
    assert(a && a <= INT32_MAX);
    assert(toucan_mouse_window_complete(&w, a));
}

/* 125 Hz sensor, one air report per 15 ms event, notification completion one
 * event later. Exercise actual coalescing/window helpers, button order and a
 * 315 ms transport pause. This is a transport model, not a hardware result. */
static int simulate_window(unsigned int limit, bool stall) {
    struct toucan_mouse_window w = {0};
    struct toucan_mouse_queue q = {0};
    struct { uint32_t token; int completed_at; struct toucan_mouse_packet p; } tx[2] = {0};
    int next_send = 15, reports = 0, total = 0, sent_total = 0;
    unsigned int in_flight = 0;
    uint16_t delivered_buttons = 0;
    int transitions = 0;
    for (int t = 0; t < 3000; t++) {
        for (unsigned int i = 0; i < 2; i++) {
            if (tx[i].token && tx[i].completed_at <= t) {
                assert(toucan_mouse_window_complete(&w, tx[i].token));
                tx[i].token = 0;
                in_flight--;
            }
        }
        if (t < 2400 && t % 8 == 0) {
            uint16_t buttons = ((t >= 960 && t < 1040) || (t >= 1120 && t < 1200));
            toucan_mouse_push(&q, packet(t, buttons, 2), t);
            total += 2;
        }
        for (unsigned int i = 0; i < 2 && in_flight < limit && q.count; i++) {
            if (tx[i].token) { continue; }
            struct toucan_mouse_packet p;
            assert(toucan_mouse_pop(&q, &p, t));
            uint32_t token = toucan_mouse_window_acquire(&w, 10);
            assert(token);
            int air = next_send > t ? next_send : ((t / 15) + 1) * 15;
            if (stall && air >= 900 && air < 1215) { air = 1215; }
            next_send = air + 15;
            tx[i].token = token;
            tx[i].completed_at = air + 15;
            tx[i].p = p;
            in_flight++;
            sent_total += p.x;
            reports++;
            if (p.buttons != delivered_buttons) {
                transitions++;
                delivered_buttons = p.buttons;
            }
        }
        assert(in_flight <= limit && in_flight <= TOUCAN_MOUSE_IN_FLIGHT);
        assert(q.count <= 5); /* Two force clicks during the transport pause. */
    }
    assert(!in_flight && !q.count);
    if (!stall) {
        assert(sent_total == total);
    } else {
        /* Old movement attached to earlier button edges expires while the
         * newest movement stays fresh. Do not replay the old path on recovery. */
        assert(sent_total > 0 && sent_total <= total);
    }
    assert(transitions == 4 && delivered_buttons == 0);
    return reports;
}

static void test_pipelined_delivery(void) {
    test_completion_epochs();
    int single = simulate_window(1, false);
    int pipelined = simulate_window(2, false);
    assert(pipelined > single * 18 / 10);
    assert(simulate_window(2, true) > 0);
    printf("15 ms link model: one slot %d reports, two slots %d reports; stalled click ordering passed\n",
           single, pipelined);
}

int main(void) {
    test_pipelined_delivery();
    test_continuous_motion();
    test_edges_and_drag();
    test_retry_disconnect_overflow();
    /* The measured v4 host link was interval=12 (15 ms), latency=30. */
    assert(toucan_mouse_link_needs_update(12,30));
    assert(toucan_mouse_link_needs_update(40,0));
    assert(!toucan_mouse_link_needs_update(12,0));
    assert(!toucan_mouse_link_needs_update(6,0));
    struct toucan_mouse_queue q={0}; struct toucan_mouse_packet p;
    toucan_mouse_push(&q,packet(0,0,10),0);
    toucan_mouse_push(&q,packet(32,0,0),32);
    assert(toucan_mouse_pop(&q,&p,41) && p.x==0); /* Empty report cannot refresh. */
    puts("Mouse queue: sustained overload, ordered double clicks, stale motion, retry and recovery passed");
    return 0;
}
