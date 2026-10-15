"""Native contract checks. Uses frozen external calibration/image fixtures; does not rewrite them."""
import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--reader',type=Path,required=True);p.add_argument('--profile',type=Path,required=True);p.add_argument('--pgm',type=Path,required=True);a=p.parse_args()
profile=json.loads(a.profile.read_text(encoding='utf-8-sig')); image=a.pgm.read_bytes(); gray=image.split(b'\n',3)[3]
assert len(gray)==profile['width']*profile['height']
def run(path, raw):
    return subprocess.run([str(a.reader),str(path),'--stream'],input=raw,capture_output=True,timeout=20)
def rows(done):
    return [json.loads(line.split(' ',1)[1]) for line in done.stdout.decode().splitlines()]
r=run(a.profile,gray*20); assert r.returncode==0
values=rows(r); assert len(values)==20 and [v['sequence'] for v in values]==list(range(20))
assert all(abs(v['angle_deg']-5.71930248)<.00001 for v in values)
r=run(a.profile,bytes(len(gray))); assert r.returncode==0 and rows(r)[0]['angle_deg'] is None
r=run(a.profile,gray+b'123'); assert r.returncode==3 and len(rows(r))==1
second=bytearray(gray)
for i in profile['mask_indices']:
    x,y=i%profile['width'],i//profile['width']
    if (x-410)**2+(y-80)**2<32**2: second[i]=0
r=run(a.profile,second); assert rows(r)[0]['reason']=='ambiguous_shadows',rows(r)
output=a.reader.parent/'contract-invalid.json'
for change in [dict(schema_version=2),dict(min_area=0),dict(width=20),dict(calibration_version='bad"version'),dict(anchor_indices=[-1]*40),dict(nodes=list(reversed(profile['nodes'])))]:
    bad={**profile,**change};output.write_text(json.dumps(bad),encoding='utf-8')
    r=run(output,gray);assert r.returncode==2 and not r.stdout,change
output.write_bytes(a.profile.read_bytes()+b'garbage');r=run(output,gray);assert r.returncode==2
print('PASS: persistent signed reads, null failures, truncated stream, multiple shadows and 7 invalid calibration cases')
