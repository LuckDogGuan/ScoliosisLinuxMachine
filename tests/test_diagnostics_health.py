"""Read-only sensor/service/RPC detection against controlled sysfs/proc fixtures."""
import json, os, subprocess, tempfile
from pathlib import Path
source=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='diagnostics-health-') as folder:
    root=Path(folder); harness=root/'probe.c'; exe=root/'probe'
    harness.write_text('#include "platform.h"\n#include <stdio.h>\n#include <stdlib.h>\nint camera_status(int *status) {*status=0;return getenv("RPC_FAIL") ? -1 : 0;}\nint main(void) {char json[512];camera_diagnostic_health(json,sizeof(json));puts(json);return 0;}\n')
    subprocess.run(['gcc','-std=c11','-D_POSIX_C_SOURCE=200809L','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Werror',f'-DDIAGNOSTICS_ROOT="{root}"','-Iinclude','-Ithird_party/cjson',str(harness),'src/platform/diagnostics_health.c','third_party/cjson/cJSON.c','-o',str(exe)],cwd=source,check=True)
    def probe(fail=False):
        return json.loads(subprocess.check_output([exe],env={**os.environ,**({'RPC_FAIL':'1'} if fail else {})},text=True))
    reply=probe();assert reply['camera_sensor']=='unavailable' and reply['camera_service']=='unavailable',reply
    video=root/'sys/class/video4linux';video.mkdir(parents=True)
    proc=root/'proc';proc.mkdir()
    reply=probe();assert reply['camera_sensor']=='not_detected' and reply['camera_service']=='stopped',reply
    sensor=video/'v4l-subdev0';sensor.mkdir();(sensor/'name').write_text('rockchip-csi2-dphy0\n')
    assert probe()['camera_sensor']=='not_detected'
    (sensor/'name').write_text('m00_b_sc3336 2-0030\n')
    reply=probe();assert reply['camera_sensor']=='detected' and reply['camera_sensor_name']=='m00_b_sc3336 2-0030',reply
    daemon=proc/'123';daemon.mkdir();(daemon/'comm').write_text('rkipc\n')
    reply=probe();assert reply['camera_service']=='running' and reply['camera_rpc']=='ready',reply
    assert probe(True)['camera_rpc']=='unavailable'
    before={str(p):p.read_bytes() for p in root.rglob('*') if p.is_file()}
    probe();assert before=={str(p):p.read_bytes() for p in root.rglob('*') if p.is_file()}
print('PASS: missing sysfs, absent sensor, unrelated subdevice, recognised sensor, stopped/running service, RPC failure, read-only probe')
