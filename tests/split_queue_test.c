/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <toucan/split_queue.h>
#include <toucan/mouse_window.h>

static void move(struct toucan_split_queue *q,int64_t t,int x,int y) {
    toucan_split_push(q,(struct toucan_split_event){TS_REL,TS_X,x,false},t);
    toucan_split_push(q,(struct toucan_split_event){TS_REL,TS_Y,y,true},t);
}
static void test_continuous_stream_and_stop(void) {
    for (int interval=8;interval<=128;interval*=2) {
        struct toucan_split_queue q={0};
        struct toucan_split_event e; int64_t ms;
        long x=0,y=0;
        for (int t=0;t<600000;t+=8) {
            move(&q,t,3,-2);
            assert(q.count<=1);
            if (t%interval==0) {
                while (toucan_split_pop(&q,&e,&ms,t)) {
                    assert(e.type==TS_REL);
                    if (e.code==TS_X) { x+=e.value; } else { y+=e.value; }
                }
            }
        }
        while (toucan_split_pop(&q,&e,&ms,600000)) {
            if (e.code==TS_X) { x+=e.value; } else { y+=e.value; }
        }
        assert(x==225000 && y==-150000);
        move(&q,600008,100,100);
        assert(!toucan_split_pop(&q,&e,&ms,600100)); /* No trailing replay. */
    }
}
static void test_edges_and_sync(void) {
    struct toucan_split_queue q={0}; struct toucan_split_event e; int64_t ms;
    move(&q,0,7,9);
    assert(toucan_split_pop(&q,&e,&ms,1) && e.code==TS_X && !e.sync && e.value==7);
    move(&q,2,3,4); /* Never merge into a frame whose X was already sent. */
    assert(q.count==2);
    assert(toucan_split_pop(&q,&e,&ms,3) && e.code==TS_Y && e.sync && e.value==9);
    toucan_split_push(&q,(struct toucan_split_event){TS_KEY,TS_BTN0,1,true},4);
    move(&q,5,5,6);
    toucan_split_push(&q,(struct toucan_split_event){TS_KEY,TS_BTN0,0,true},6);
    toucan_split_push(&q,(struct toucan_split_event){TS_KEY,TS_BTN0,1,true},7);
    toucan_split_push(&q,(struct toucan_split_event){TS_KEY,TS_BTN0,0,true},8);
    int edges=0;
    while (toucan_split_pop(&q,&e,&ms,10)) {
        if (e.type==TS_KEY) { assert(e.value==(edges%2==0)); edges++; }
    }
    assert(edges==4);
    move(&q,20,10,20);
    assert(toucan_split_pop(&q,&e,&ms,21) && !e.sync);
    assert(toucan_split_pop(&q,&e,&ms,100) && e.code==TS_Y && e.sync && e.value==0);
    assert(!toucan_split_pop(&q,&e,&ms,100));
    /* Expiration never discards releases or a three-finger middle click. */
    for (int i=0;i<2;i++) {
        toucan_split_push(&q,(struct toucan_split_event){TS_KEY,TS_BTN0+2,1-i,true},110+i);
    }
    assert(toucan_split_pop(&q,&e,&ms,1000) && e.value==1);
    assert(toucan_split_pop(&q,&e,&ms,1000) && e.value==0);
}
static void test_overflow_release_and_stale_callbacks(void) {
    struct toucan_split_queue q={0}; struct toucan_split_event e; int64_t ms;
    for (int i=0;i<33;i++) {
        toucan_split_push(&q,(struct toucan_split_event){TS_KEY,TS_BTN0,i%2,true},i);
    }
    assert(q.overflows==1);
    int state=1;
    while (toucan_split_pop(&q,&e,&ms,40)) {
        if(e.code==TS_BTN0) { state=e.value; }
    }
    assert(state==0);
    struct toucan_mouse_window w={0};
    uint32_t a=toucan_mouse_window_acquire(&w,1), b=toucan_mouse_window_acquire(&w,1);
    assert(a && b && !toucan_mouse_window_available(&w));
    toucan_mouse_window_disconnect(&w,1);
    uint32_t next=toucan_mouse_window_acquire(&w,2);
    assert(next && !toucan_mouse_window_complete(&w,a));
    assert(toucan_mouse_window_complete(&w,next));
}
int main(void) {
    test_continuous_stream_and_stop();
    test_edges_and_sync();
    test_overflow_release_and_stale_callbacks();
    puts("Split backlog, XY synchronization, button order and recovery tests passed");
}
