"""Camera support comes from the driver, not a static UI list. Includes real UDP/persistence checks."""
import json, os, secrets, socket, subprocess, tempfile, time
from pathlib import Path
source=Path(__file__).resolve().parents[1]
root=Path(tempfile.mkdtemp(prefix='camera-profiles-'))
ini=root/'rkipc.ini';node=root/'camera';node.write_bytes(b'')
ini.write_text('[video.0]\nmax_width=1280\nmax_height=1080\n[osd.1]\ntype=dateTime\nenabled=0\n')
# Simulate the board's real VIDIOC_ENUM_FRAMESIZES result, not application policy.
shim=root/'driver.c'
shim.write_text(r"""
#define _GNU_SOURCE
#include <time.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <errno.h>
#include <dlfcn.h>
int ioctl(int fd,unsigned long request,...) {
    va_list ap;va_start(ap,request);void *arg=va_arg(ap,void*);va_end(ap);
    if(request==VIDIOC_ENUM_FRAMESIZES) {
        struct v4l2_frmsizeenum *s=arg;
        if(s->index || s->pixel_format!=V4L2_PIX_FMT_NV12){errno=EINVAL;return -1;}
        s->type=V4L2_FRMSIZE_TYPE_STEPWISE;
        s->stepwise.min_width=s->stepwise.min_height=32;
        s->stepwise.max_width=1280;s->stepwise.max_height=1080;
        s->stepwise.step_width=s->stepwise.step_height=8;return 0;
    }
    int (*real_ioctl)(int,unsigned long,void*)=dlsym(RTLD_NEXT,"ioctl");
    return real_ioctl(fd,request,arg);
}
""")
lib=root/'driver.so'
subprocess.run(['gcc','-shared','-fPIC','-Wall','-Wextra','-Werror',str(shim),'-ldl','-o',str(lib)],check=True)
exe=root/'app';buttons=root/'buttons';buttons.write_text('0');port=37025
flags='-std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O2 -Iinclude -Ithird_party/cjson -Ithird_party/lwbtn/include '
for name,value in [('INSTRUMENT_ROOT',str(root/'state')),('INSTRUMENT_CAMERA_NODE',str(node)),('INSTRUMENT_CAMERA_INI',str(ini))]:
    flags+=f"-D{name}='\"{value}\"' "
flags+=f'-DDISCOVER_PORT={port}'
subprocess.run(['make','CC=gcc',f'TARGET={exe}',f'CFLAGS={flags}'],cwd=source,check=True)
config=json.loads((source/'config.example.json').read_text());config['pc_base_url']='http://127.0.0.1:49159'
cfg=root/'config.json';cfg.write_text(json.dumps(config));original=cfg.read_bytes()
state=root/'state/state';state.mkdir(parents=True)
(state/'pair-v3.json').write_text(json.dumps(dict(host_id='test-host',peer_ip='127.0.0.1',pc_base_url=config['pc_base_url'])))
env={**os.environ,'LD_PRELOAD':str(lib),'INSTRUMENT_HOST_TEST':'1','INSTRUMENT_BUTTON_FILE':str(buttons)}
proc=None;boot=None;sequence=0
sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);sock.settimeout(1)
def ask(op,**fields):
    global sequence
    sequence+=1;nonce=secrets.token_hex(8)
    message=dict(version=3,op=op,device_id=config['device_id'],host_id='test-host',sequence=sequence,nonce=nonce,**fields)
    if boot:message['boot_id']=boot
    sock.sendto(json.dumps(message).encode(),('127.0.0.1',port))
    while True:
        reply=json.loads(sock.recv(4096))
        if reply.get('nonce')==nonce:return reply

def start():
    global proc,boot
    boot=None
    proc=subprocess.Popen([str(exe),str(cfg)],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    for _ in range(20):
        try:boot=ask('hello_query')['boot_id'];return
        except socket.timeout:time.sleep(.05)
    raise AssertionError('Coordinator did not start')
try:
    start()
    assert 'camera_settings_v1' in ask('hello_query')['capabilities']
    reply=ask('camera_get_settings');assert reply['ok'],reply
    profile=reply['camera_settings'];assert (profile['width'],profile['height'])==(640,480)
    formats={(f['width'],f['height']) for f in profile['formats']}
    assert formats=={(640,480),(800,600),(1024,768),(1280,720),(1280,960),(1280,1080)},formats
    assert profile['timestamp_overlay'] is False
    for w,h in [(1920,1080),(640.5,480),(0,480),(640,481),('640',480)]:
        assert not ask('camera_set_settings',width=w,height=h)['ok']
    assert not (state/'camera-profile.json').exists()
    reply=ask('camera_set_settings',width=1280,height=720);assert reply['ok'],reply
    assert json.loads((state/'camera-profile.json').read_text())['width']==1280
    proc.terminate();proc.wait(timeout=10);start()
    assert ask('camera_get_settings')['camera_settings']['width']==1280
    assert ask('capture_enable',gate_id='gate',ttl_ms=5000)['ok']
    reply=ask('camera_set_settings',width=640,height=480)
    assert not reply['ok'] and reply['error']=='camera_settings_busy',reply
    assert ask('capture_disable',gate_id='gate')['ok']
    assert ask('diagnostics_start',diagnostics_id='diag',ttl_ms=5000)['ok']
    assert not ask('camera_set_settings',width=640,height=480,diagnostics_id='wrong')['ok']
    assert ask('camera_set_settings',width=640,height=480,diagnostics_id='diag')['ok']
    assert ask('diagnostics_end',diagnostics_id='diag')['ok']
    # Driver unavailable: never fabricate a supported format, including 640x480.
    assert ask('camera_set_settings',width=1280,height=720)['ok']
    node.rename(root/'camera-unavailable')
    proc.terminate();proc.wait(timeout=10);start()
    reply=ask('camera_get_settings');assert reply['camera_settings']['formats']==[],reply
    assert reply['camera_settings']['width']==1280,'Unavailable boot driver must not erase saved selection'
    assert not ask('camera_set_settings',width=1280,height=720)['ok']
    (root/'camera-unavailable').rename(node)
    assert ask('camera_get_settings')['camera_settings']['width']==1280
    assert cfg.read_bytes()==original,'Network/pair config changed'
    print('PASS: real UDP camera mode filtering, invalid/busy/token rejection, saved profile and restart; unavailable driver gives no choices')
finally:
    if proc:proc.terminate();proc.wait(timeout=10)
    sock.close()
# Execute the exact AWK policy from the boot script against all relevant OSD types.
text=(source/'deploy/RkLunch.sh').read_text()
program=text.split("if awk '",1)[1].split("' \"$rkipc_ini\"",1)[0]
fixture='[video.0]\nwidth=1280\n[osd.0]\ntype=channelName\nenabled=1\n[osd.1]\ntype=dateTime\nenabled=1\ndisplay_week_enabled=1\n[osd.4]\ntype=privacyMask\nenabled=1\n'
updated=subprocess.check_output(['awk',program],input=fixture,text=True)
assert '[osd.1]\ntype=dateTime\nenabled= 0\ndisplay_week_enabled=1' in updated,updated
assert '[osd.0]\ntype=channelName\nenabled=1' in updated
assert '[osd.4]\ntype=privacyMask\nenabled=1' in updated
assert 'width=1280' in updated
print('PASS: boot disables date-time OSD only, preserving channel/privacy/video fields')
print('Evidence:',root)
