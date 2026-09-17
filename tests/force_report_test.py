"""Compile the production force report function and inspect actual button edges."""
from pathlib import Path
import subprocess
import sys
import tempfile

root=Path(__file__).resolve().parents[1]
driver=(root/'modules/azoteq/drivers/input/tps43.c').read_text(encoding='utf-8')
start=driver.index('static int tps43_force_report(')
end=driver.index('\n}\n',start)+3
report=driver[start:end]
source=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "modules/azoteq/drivers/input/tps43_force.h"
struct tps43_drv_data { bool force_release_pending; };
struct device { struct tps43_drv_data *data; };
#define INPUT_BTN_0 0x110
#define K_MSEC(ms) (ms)
#define LOG_DBG(...) ((void)0)
static int edges[20000], count, calls, fail_call;
static int input_report_key(const struct device *dev,int code,int value,bool sync,int timeout) {
    assert(dev && code==INPUT_BTN_0 && sync && timeout==5);
    if(++calls==fail_call) return -12;
    assert(count < (int)(sizeof(edges)/sizeof(edges[0])));
    edges[count++]=value;
    return 0;
}
''' + report + r'''
int main(void) {
    struct tps43_drv_data data={0};
    struct device dev={&data};
    assert(tps43_force_report(&dev,TPS43_FORCE_NONE)==INT16_MAX && count==0);
    /* Each pulse is a complete click, not a synthetic held press. */
    for(int i=0;i<3000;i++) {
        assert(tps43_force_report(&dev,TPS43_FORCE_CLICK)==0);
        assert(!data.force_release_pending);
        assert(edges[2*i]==1 && edges[2*i+1]==0);
    }
    int n=count;
    assert(tps43_force_report(&dev,TPS43_FORCE_PRESS)==0);
    for(int i=0;i<100;i++) tps43_force_report(&dev,TPS43_FORCE_NONE);
    assert(count==n+1 && edges[n]==1);
    assert(tps43_force_report(&dev,TPS43_FORCE_RELEASE)==0 && edges[n+1]==0);
    /* Failed down never sends an unmatched up. */
    n=count;fail_call=calls+1;
    assert(tps43_force_report(&dev,TPS43_FORCE_CLICK)==-12);
    assert(count==n && !data.force_release_pending);
    /* Failed up is remembered and retried by next frame/watchdog. */
    fail_call=calls+2;
    assert(tps43_force_report(&dev,TPS43_FORCE_CLICK)==-12);
    assert(count==n+1 && edges[n]==1 && data.force_release_pending);
    fail_call=calls+1;
    assert(tps43_force_report(&dev,TPS43_FORCE_CLICK)==-12 && count==n+1);
    fail_call=0;
    tps43_force_report(&dev,TPS43_FORCE_NONE);
    assert(count==n+2 && edges[n+1]==0 && !data.force_release_pending);
    /* Retry finishes the previous click before the next pulse's edges. */
    n=count;fail_call=calls+2;
    assert(tps43_force_report(&dev,TPS43_FORCE_CLICK)==-12);
    fail_call=0;
    assert(tps43_force_report(&dev,TPS43_FORCE_CLICK)==0);
    assert(count==n+4);
    for(int i=0;i<4;i++) assert(edges[n+i]==(i%2==0));
    /* Failed drag release follows the same recovery. */
    n=count;fail_call=calls+1;
    assert(tps43_force_report(&dev,TPS43_FORCE_RELEASE)==-12);
    fail_call=0;
    assert(tps43_force_report(&dev,TPS43_FORCE_RELEASE)==0);
    assert(count==n+1 && edges[n]==0);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    temp=Path(d)
    p=temp/'report.c';p.write_text(source,encoding='utf-8')
    executable=temp/('report.exe' if sys.platform=='win32' else 'report')
    cmd=(sys.argv[1:] or ['cc'])+['-std=c11','-Wall','-Wextra','-Werror',
        '-I'+str(root),'-I'+str(root/'modules/azoteq/include'),str(p),'-o',str(executable)]
    if sys.platform!='win32': cmd+=['-fsanitize=undefined,address']
    subprocess.run(cmd,check=True)
    subprocess.run([str(executable)],check=True)
print('Production force report: 3000 down/up pairs, hold ownership and queue-error recovery passed')
