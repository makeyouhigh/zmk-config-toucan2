/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <toucan/mouse_queue.h>

static struct toucan_mouse_packet packet(int64_t t, uint16_t buttons, int16_t x) {
    return (struct toucan_mouse_packet){.buttons=buttons, .x=x, .y=-x, .motion_ms=t};
}
static void test_continuous_motion(void) {
    const int periods[] = {8, 15, 30, 60, 100, 500};
    for (unsigned int i=0; i<sizeof(periods)/sizeof(periods[0]); i++) {
        struct toucan_mouse_queue q={0};
        struct toucan_mouse_packet p;
        int64_t next=periods[i];
        /* Ten minutes at 125 Hz, even with a very slow consumer. */
        for (int64_t t=0; t<600000; t+=8) {
            toucan_mouse_push(&q,packet(t,0,2),t);
            assert(q.count==1);
            if (t>=next) {
                assert(toucan_mouse_pop(&q,&p,t));
                assert(p.x>=2 && p.x<=12 && p.y==-p.x);
                assert(t-p.motion_ms<=TOUCAN_MOUSE_MOTION_MAX_AGE_MS);
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
int main(void) {
    test_continuous_motion();
    test_edges_and_drag();
    test_retry_disconnect_overflow();
    puts("Mouse queue: sustained overload, ordered double clicks, stale motion, retry and recovery passed");
    return 0;
}
