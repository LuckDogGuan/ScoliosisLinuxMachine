"""Exercise real camera RPC switching/readback/restore and config migration."""
import json, socket, struct, subprocess, tempfile, threading
from pathlib import Path
root = Path(__file__).resolve().parents[1]
endpoint = Path('/var/tmp/rkipc')
assert not endpoint.exists(), 'Do not replace a live camera endpoint'
with tempfile.TemporaryDirectory(prefix='capture-resolution-') as folder:
    folder = Path(folder)
    harness = folder / 'check.c'
    harness.write_text(r"""
#include "platform.h"
#include "app.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv) {
    if(argc==2) { Config cfg={0}; if(config_load(argv[1],&cfg))return 2;
        assert(cfg.width==640 && cfg.height==480); return 0; }
    CameraSettings before,target;
    assert(!camera_get_settings(&before));
    assert(!camera_capture_settings(30,4000,30,&target));
    assert(!strcmp(target.resolution,"640*480"));
    int result=camera_set_settings(&target);
    if(getenv("EXPECT_FAILURE")) assert(result); else assert(!result);
    assert(!camera_set_settings(&before));
    return 0;
}
""")
    exe = folder / 'check'
    subprocess.run(['gcc','-std=c11','-D_POSIX_C_SOURCE=200809L','-D_DEFAULT_SOURCE',
        '-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections',
        '-Iinclude','-Ithird_party/cjson',str(harness),'src/platform/luckfox_camera.c',
        'src/config/config.c','third_party/cjson/cJSON.c','-Wl,--gc-sections','-o',str(exe)],cwd=root,check=True)
    original = json.loads((root/'config.example.json').read_text())
    for width,height,ok in [(640,480,True),(1280,1080,True),(640,1080,False),(1920,1080,False)]:
        cfg=json.loads(json.dumps(original));cfg['capture'].update(width=width,height=height)
        config=folder/'config.json';config.write_text(json.dumps(cfg));before=config.read_bytes()
        result=subprocess.run([str(exe),str(config)])
        assert (result.returncode==0)==ok
        assert config.read_bytes()==before
    def exact(ch,n):
        data=b''
        while len(data)<n:
            part=ch.recv(n-len(data));assert part;data+=part
        return data
    def integer(ch):return struct.unpack('<i',exact(ch,4))[0]
    import os
    for mode in ['ok','set_failed','readback_mismatch']:
        state=dict(resolution='1280*1080',frame_rate='60/6',max_rate=640,gop=10)
        initial=state.copy();calls=[];errors=[]
        server=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
        server.bind(str(endpoint));server.listen(4);server.settimeout(.1)
        done=threading.Event()
        def serve():
            try:
                while not done.is_set():
                    try:ch,_=server.accept()
                    except socket.timeout:continue
                    with ch:
                        ch.sendall(struct.pack('<i',0))
                        name=exact(ch,integer(ch)).rstrip(b'\0').decode()
                        assert integer(ch)==0
                        operation,key=name.removeprefix('rk_video_').split('_',1)
                        strings=key in ('resolution','frame_rate');error=0
                        if operation=='get':
                            value=state[key]
                            if strings:
                                blob=value.encode();ch.sendall(struct.pack('<i',len(blob))+blob)
                            else:ch.sendall(struct.pack('<i',value))
                        else:
                            value=exact(ch,integer(ch)).rstrip(b'\0').decode() if strings else integer(ch)
                            calls.append((key,value))
                            if key=='resolution' and value=='640*480' and mode!='ok':
                                error=-1 if mode=='set_failed' else 0
                            else:state[key]=value
                        ch.sendall(struct.pack('<i',error))
            except Exception as error:errors.append(error)
        worker=threading.Thread(target=serve);worker.start()
        try:
            env=dict(os.environ)
            if mode!='ok':env['EXPECT_FAILURE']='1'
            subprocess.run([str(exe)],env=env,check=True,timeout=10)
        finally:
            done.set();worker.join();server.close();endpoint.unlink()
        assert not errors,errors
        assert state==initial,(mode,state)
        assert ('resolution','640*480') in calls
        if mode=='ok':assert ('resolution','1280*1080') in calls
    print('PASS: 640x480 switch, readback, preview restoration, RPC failure and legacy config migration')
