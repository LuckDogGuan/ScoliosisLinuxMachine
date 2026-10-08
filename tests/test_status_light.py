"""Exercise business indication against the same sysfs writes as the board."""
import os, subprocess, tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='status-light-') as tmp:
    tmp=Path(tmp)
    harness=tmp/'test.c'
    harness.write_text('#define LIGHT_TEST_HOST\n#include "light_test.c"\n#include <assert.h>\nstatic long read_duty(void) {\n    char p[512]; snprintf(p,sizeof(p),"%s/class/pwm/pwmchip0/pwm0/duty_cycle",sysroot());\n    FILE *f=fopen(p,"r"); long n=-1; assert(f && fscanf(f,"%ld",&n)==1); fclose(f); return n;\n}\nint main(void) {\n    assert(light_status_tick(0,10000)==0 && read_duty()==1000000);\n    assert(light_status_tick(1,10100)==0 && read_duty()==0);\n    assert(light_status_tick(1,11600)==0 && read_duty()==1000000);\n    assert(light_status_tick(1,13100)==0 && read_duty()==0);\n    assert(light_status_tick(0,13101)==0 && read_duty()==1000000);\n    assert(light_status_mode(1,0,0,0,0)==LIGHT_PAIRING);\n    assert(light_status_mode(0,1,1,0,0)==LIGHT_CONNECTED);\n    assert(light_status_mode(0,1,0,0,0)==LIGHT_DISCONNECTED);\n    assert(light_status_mode(0,1,1,1,0)==LIGHT_FAULT);\n    assert(light_status_mode(0,1,1,0,1)==LIGHT_LOW_BATTERY);\n    assert(light_status_tick(LIGHT_DISCONNECTED,14000)==0 && read_duty()==1000000);\n    assert(light_status_tick(LIGHT_DISCONNECTED,15000)==0 && read_duty()==0);\n    assert(light_status_tick(LIGHT_FAULT,16000)==0 && read_duty()==1000000);\n    assert(light_status_tick(LIGHT_FAULT,16125)==0 && read_duty()==0);\n    assert(light_status_tick(LIGHT_LOW_BATTERY,17000)==0 && read_duty()==1000000);\n    assert(light_status_tick(LIGHT_LOW_BATTERY,17125)==0 && read_duty()==0);\n    assert(light_status_tick(LIGHT_LOW_BATTERY,17250)==0 && read_duty()==1000000);\n    assert(light_status_tick(LIGHT_OFF,18000)==0 && read_duty()==0);\n    light_status_close(); assert(read_duty()==0);\n    return 0;\n}\n')
    names=['class/gpio/export','class/gpio/unexport','class/gpio/gpio32/direction','class/gpio/gpio32/value','class/pwm/pwmchip0/export','class/pwm/pwmchip0/unexport']
    names += ['class/pwm/pwmchip0/pwm0/'+n for n in ('enable','duty_cycle','period','polarity')]
    for n in names:
        p=tmp/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('')
    exe=tmp/'test'
    subprocess.run(['gcc','-std=c11','-D_POSIX_C_SOURCE=200809L','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Werror','-I'+str(root/'include'),'-I'+str(root/'src/platform'),str(harness),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],env={**os.environ,'LIGHT_TEST_SYSFS':str(tmp)},check=True)
print('PASS: normal steady, 3-second pairing breath, return steady, shutdown off')
