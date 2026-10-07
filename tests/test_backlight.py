"""Actual gesture service + GPIO55 writes at exact timing boundaries."""
import os, subprocess, tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="backlight-check-") as tmp:
    tmp=Path(tmp)
    harness=tmp/"test.c"
    harness.write_text(r'''#define LIGHT_TEST_HOST
#include "light_test.c"
#include "buttons.c"
#include <assert.h>
void instrument_trace(const char *a,const char *b,const char *c,const char *d,const char *e) {
    (void)a;(void)b;(void)c;(void)d;(void)e;
}
int platform_button_raw(int *mask) { *mask=0; return 0; }
static int read_level(void) {
    char p[512];snprintf(p,sizeof(p),"%s/class/gpio/gpio55/value",sysroot());
    FILE *f=fopen(p,"r");int n=-1;assert(f && fscanf(f,"%d",&n)==1);fclose(f);return n;
}
static void tick(uint64_t ms,int keys,int allowed,int expected) {
    assert(backlight_tick(ms,keys,allowed)==0);assert(read_level()==expected);
}
int main(void) {
    assert(light_status_tick(0,10000)==0);
    tick(10000,5,1,0);tick(10010,0,1,0); /* startup hold disarmed */
    tick(10020,5,1,0);
    for(uint64_t t=10030;t<10520;t+=10) tick(t,5,1,0);
    tick(10519,5,1,0);tick(10520,5,1,1); /* exactly 500 ms */
    tick(10530,4,1,1); /* release either key */
    for(uint64_t t=10540;t<11530;t+=10) tick(t,4,1,1);
    tick(11529,4,1,1);tick(11530,0,1,0); /* exactly 1000 ms */
    tick(11540,5,1,0);
    for(uint64_t t=11550;t<=12040;t+=10) tick(t,5,1,t<12040?0:1);
    tick(12050,0,1,1);tick(12060,5,1,1); /* rehold cancels pending off */
    for(uint64_t t=12070;t<=13160;t+=10) tick(t,5,1,1);
    tick(13170,-1,1,0);tick(13180,5,1,0);tick(13190,0,1,0);
    tick(13200,5,1,0);tick(13400,5,1,0); /* sampling gap disarms */
    tick(13410,0,1,0);tick(13420,5,1,0);
    for(uint64_t t=13430;t<=13920;t+=10) tick(t,5,1,t<13920?0:1);
    tick(13930,5,0,0);tick(13940,5,1,0); /* lease/gate loss */
    light_status_close();assert(read_level()==0);
}
''')
    names=["class/gpio/export","class/gpio/unexport"]
    names += [f"class/gpio/gpio{pin}/{n}" for pin in (32,55) for n in ("direction","value")]
    names += ["class/pwm/pwmchip0/export","class/pwm/pwmchip0/unexport"]
    names += ["class/pwm/pwmchip0/pwm0/"+n for n in ("enable","duty_cycle","period","polarity")]
    for n in names:
        p=tmp/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_text("")
    exe=tmp/"test"
    subprocess.run(["gcc","-std=c11","-D_POSIX_C_SOURCE=200809L","-D_DEFAULT_SOURCE","-Wall","-Wextra","-Werror","-I"+str(root/"include"),"-I"+str(root/"third_party/cjson"),"-I"+str(root/"third_party/lwbtn/include"),"-I"+str(root/"src/platform"),"-I"+str(root/"src/middleware"),str(harness),str(root/"third_party/lwbtn/lwbtn.c"),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],env={**os.environ,"LIGHT_TEST_SYSFS":str(tmp)},check=True)
print("PASS: GPIO55 500ms hold / 1000ms release, short/invalid/gap/gate, rehold, shutdown")
