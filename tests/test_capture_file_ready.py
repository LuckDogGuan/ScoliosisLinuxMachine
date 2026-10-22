"""Real recording lifecycle with deterministic no-frame, delayed finalize and cancellation faults."""
from datetime import datetime, timezone
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
work=root/'validation/capture-file-ready'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%f')
work.mkdir(parents=True)
for mode in range(7):
    run=work/str(mode)
    for folder in ['video','capture/writing','capture/pending']:(run/folder).mkdir(parents=True,exist_ok=True)
    exe=run/'check'
    subprocess.run(['gcc','-std=c11','-D_DEFAULT_SOURCE','-D_POSIX_C_SOURCE=200809L',
        '-Wall','-Wextra','-Werror','-DCAPTURE_FIRST_FRAME_TIMEOUT_MS=150','-DCAPTURE_FINALIZE_TIMEOUT_MS=350',
        f'-DINSTRUMENT_ROOT="{run}"','-DVIDEO_DIR="video"','-Iinclude','-Ithird_party/cjson',
        'tests/test_capture_file_ready.c','third_party/cjson/cJSON.c','-o',str(exe)],cwd=root,check=True)
    subprocess.run([str(exe),str(mode)],cwd=run,check=True,timeout=8)
print('PASS: real recording lifecycle; no-frame recovery, persistent no-frame rejection, late finalize, ambiguity, corrupt MP4, cancellation')
