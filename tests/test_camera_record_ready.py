"""Only a new successful mux generation may acknowledge actual recording."""
from datetime import datetime, timezone
import json,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
work=root/'validation/camera-record-ready'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%f')
(work/'state').mkdir(parents=True)
c=work/'check.c'
c.write_text('#include "platform.h"\n#include <stdlib.h>\nint main(int argc,char **argv){return argc!=2 || camera_record_ready(1000)!=atoi(argv[1]);}\n')
exe=work/'check'
subprocess.run(['gcc','-std=c11','-D_DEFAULT_SOURCE','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Werror',
    '-ffunction-sections','-fdata-sections',f'-DINSTRUMENT_ROOT="{work}"','-Iinclude','-Ithird_party/cjson',str(c),
    'src/platform/luckfox_camera.c','third_party/cjson/cJSON.c','-Wl,--gc-sections','-o',str(exe)],cwd=root,check=True)
def row(event,at,result=0,frames=0):return dict(event=event,monotonic_ms=at,result=result,frames=frames)
cases=[([],0),([row('mux_init',900),row('video_frame',999,frames=1)],0),
    ([row('mux_init',1000)],0),([row('mux_init',1000,-1)],-1),
    ([row('mux_init',1000),row('video_frame',1001,frames=1)],1),
    ([row('mux_init',1000),row('video_frame',1001,-1,1)],-1),
    ([row('mux_init',1000),row('video_frame',1001,frames=1),row('mux_init',1002)],0),
    ([row('video_frame',1001,frames=1)],0)]
for rows,expected in cases:
    (work/'state/mux.jsonl').write_text(''.join(json.dumps(x)+'\n' for x in rows)+'{"incomplete":')
    subprocess.run([str(exe),str(expected)],check=True)
print('PASS: 8 mux generation/first-frame/stale-event/write-failure/partial-append cases')
