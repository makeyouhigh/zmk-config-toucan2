#include <stdio.h>
#include <stdlib.h>
#include "../modules/azoteq/drivers/input/tps43_force.h"
int main(int argc,char **argv) {
    if(argc!=4) return 2;
    struct tps43_force_config c={
        .lock_level=atoi(argv[1]),.press_level=atoi(argv[2]),.release_level=atoi(argv[3]),
        .debounce_ms=8,.pulse_delta=200,.motion_threshold=6,.motion_settle_ms=32,
        .drag_threshold=16,.touch_hold_ms=250,
    };
    c.moving_lock_level=c.lock_level+500;c.moving_press_level=c.press_level+500;
    struct tps43_force_state s={0};
    unsigned t,fingers,strength,valid,x,y,clicks=0,drags=0;
    while(scanf("%u %u %u %u %u %u",&t,&fingers,&strength,&valid,&x,&y)==6) {
        int event=tps43_force_step(&s,&c,t,fingers,strength,valid,x,y);
        if(event==TPS43_FORCE_CLICK) clicks++;
        if(event==TPS43_FORCE_PRESS) drags++;
        if(event) printf("%u,%d,%u\n",t,event,strength);
    }
    printf("clicks=%u drags=%u held_at_end=%d\n",clicks,drags,s.down);
}
