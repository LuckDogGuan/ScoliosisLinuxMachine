"""Run the exact C GPIO55 mux command against a register model and fail readback."""
import ast
import os
from pathlib import Path
import re
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / 'src/platform/light_test.c').read_text()
body = source.split('static int backlight_gpio_mux(void)', 1)[1].split('int platform_backlight_percent', 1)[0]
script = ''.join(ast.literal_eval(s) for s in re.findall(r'"(?:[^"\\]|\\.)*"', body.split('static const char setup[] =', 1)[1].split('char *args[]', 1)[0]))
with tempfile.TemporaryDirectory(prefix='gpio55-mux-') as folder:
    root = Path(folder)
    mock = root / 'devmem'
    mock.write_text("#!/usr/bin/env python3\nimport os,sys\nfrom pathlib import Path\np=Path(os.environ['MUX_REGISTER'])\nassert sys.argv[1:3]==['0xff538014','32']\nn=int(p.read_text(),0)\nif len(sys.argv)==4:\n v=int(sys.argv[3],0);mask=v>>16\n assert mask==0xf000\n if not os.environ.get('IGNORE_MUX_WRITE'):p.write_text(hex((n&~mask)|(v&mask)))\nelse:print(hex(n))\n")
    mock.chmod(0o755)
    register = root / 'register'
    env = {**os.environ, 'PATH':str(root)+':'+os.environ['PATH'], 'MUX_REGISTER':str(register)}
    for before in [0x3000,0x3333,0xa2345678,0,0xffffffff]:
        register.write_text(hex(before))
        subprocess.run(['sh','-c',script],env=env,check=True)
        assert int(register.read_text(),0)==before&0xffff0fff
    register.write_text('0x3000')
    assert subprocess.run(['sh','-c',script],env={**env,'IGNORE_MUX_WRITE':'1'}).returncode!=0
print('PASS: GPIO55 mux nibble restored, neighbouring fields preserved, bad readback rejected')
