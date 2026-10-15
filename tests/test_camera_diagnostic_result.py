"""Exercise the real snapshot RPC/file loop, with a local RKiPC fixture."""
import json, os, socket, struct, subprocess, tempfile, threading
from pathlib import Path
source=Path(__file__).resolve().parents[1]
endpoint=Path('/var/tmp/rkipc')
assert not endpoint.exists(), 'Host has a live RKiPC endpoint; do not replace it'
with tempfile.TemporaryDirectory(prefix='camera-result-') as folder:
    root=Path(folder);video=root/'video0';video.mkdir();exe=root/'probe'
    harness=root/'probe.c'
    harness.write_text(r'''
#include "v3.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
uint64_t v3_now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
void instrument_trace(const char*a,const char*b,const char*c,const char*d,const char*e){(void)a;(void)b;(void)c;(void)d;(void)e;}
int http_diagnostic_frame(const Config*c,const char*t,const char*p,cJSON*j){(void)c;(void)t;(void)p;int status=atoi(getenv("UPLOAD_STATUS"));cJSON_AddNumberToObject(j,"http_status",status);return status==200?0:-1;}
int main(void){Config cfg={0};cJSON*j=cJSON_CreateObject();cJSON_AddStringToObject(j,"code","pending");int rc=camera_diagnostic_frame(&cfg,"round",j);char*s=cJSON_PrintUnformatted(j);puts(s);free(s);cJSON_Delete(j);return rc?1:0;}
''')
    subprocess.run(['gcc','-std=c11','-D_POSIX_C_SOURCE=200809L','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Werror',
        f'-DVIDEO_DIR="{video}"','-Iinclude','-Ithird_party/cjson',str(harness),
        'src/platform/luckfox_camera.c','third_party/cjson/cJSON.c','-o',str(exe)],cwd=source,check=True)
    def probe(photo=None, status=200, rpc=True):
        worker=None;server=None
        if rpc:
            server=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);server.bind(str(endpoint));server.listen(1)
            def reply():
                ch,_=server.accept()
                with ch:
                    ch.sendall(struct.pack('<i',0))
                    length=struct.unpack('<i',ch.recv(4))[0]
                    name=ch.recv(length);assert name==b'rk_take_photo\0',name
                    if photo is not None:(video/'new.jpeg').write_bytes(photo)
                    ch.sendall(struct.pack('<i',0))
            worker=threading.Thread(target=reply);worker.start()
        try:
            result=subprocess.run([exe],env={**os.environ,'UPLOAD_STATUS':str(status)},text=True,capture_output=True,timeout=8)
            value=json.loads(result.stdout)
            return result.returncode,value
        finally:
            if worker:worker.join(2)
            if server:server.close();endpoint.unlink()
            if (video/'new.jpeg').exists():(video/'new.jpeg').unlink()
    assert probe(rpc=False)[1]['code']=='snapshot_rpc_failed'
    assert probe()[1]['code']=='jpeg_timeout'
    assert probe(b'\xff\xd8incomplete')[1]['code']=='jpeg_incomplete'
    rc,value=probe(b'\xff\xd8valid-fixture\xff\xd9')
    assert rc==0 and value['code']=='ok' and value['file_bytes']>4,value
    rc,value=probe(b'\xff\xd8valid-fixture\xff\xd9',status=409)
    assert rc==1 and value['code']=='frame_upload_failed' and value['http_status']==409,value
print('PASS: actual snapshot RPC failure, absent JPEG, incomplete JPEG, success, upload rejection')
