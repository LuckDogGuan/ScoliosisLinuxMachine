"""Actual UDP coordinator: lease, range, identity, saved brightness and restart."""
import json, os, secrets, socket, subprocess, tempfile, time
from pathlib import Path
source=Path(__file__).resolve().parents[1]
root=Path(tempfile.mkdtemp(prefix="diagnostics-backlight-")); exe=root/"app"; buttons=root/"buttons"; port=37024
flags='-std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O2 -Iinclude -Ithird_party/cjson -Ithird_party/lwbtn/include '+"-DINSTRUMENT_ROOT="+chr(39)+chr(34)+str(root/'state')+chr(34)+chr(39)+f' -DDISCOVER_PORT={port}'
subprocess.run(['make','CC=gcc',f'TARGET={exe}',f'CFLAGS={flags}'],cwd=source,check=True)
config=json.loads((source/'config.example.json').read_text()); config['pc_base_url']='http://127.0.0.1:49159'
cfg=root/'config.json';cfg.write_text(json.dumps(config));buttons.write_text('0')
state=root/'state/state';state.mkdir(parents=True)
(state/'pair-v3.json').write_text(json.dumps(dict(host_id='test-host',peer_ip='127.0.0.1',pc_base_url=config['pc_base_url'])))
proc=subprocess.Popen([str(exe),str(cfg)],env={**os.environ,'INSTRUMENT_HOST_TEST':'1','INSTRUMENT_BUTTON_FILE':str(buttons)},stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);sock.settimeout(1);boot=None;sequence=0

def ask(op,**fields):
    global sequence,boot
    sequence+=1;nonce=secrets.token_hex(8)
    msg=dict(version=3,op=op,device_id=config['device_id'],host_id='test-host',sequence=sequence,nonce=nonce,**fields)
    if boot:msg['boot_id']=boot
    sock.sendto(json.dumps(msg).encode(),('127.0.0.1',port))
    while True:
        reply=json.loads(sock.recv(4096))
        if reply.get('nonce')==nonce:return reply
try:
    for _ in range(20):
        try:boot=ask('hello_query')['boot_id'];break
        except socket.timeout:time.sleep(.05)
    assert 'backlight_brightness' in ask('hello_query')['capabilities']
    assert not ask('diagnostics_backlight',diagnostics_id='diag',brightness=50)['ok']
    assert ask('diagnostics_start',diagnostics_id='diag',ttl_ms=5000)['ok']
    for value in [-1,101,0.5]:assert not ask('diagnostics_backlight',diagnostics_id='diag',brightness=value)['ok']
    assert not ask('diagnostics_backlight',diagnostics_id='wrong',brightness=50)['ok']
    for value in [0,1,25,50,75,99,100]:
        reply=ask('diagnostics_backlight',diagnostics_id='diag',brightness=value)
        assert reply['ok'] and reply['brightness']==value,reply
        assert json.loads((state/'backlight.json').read_text())['brightness']==value
    assert ask('diagnostics_renew',diagnostics_id='diag',ttl_ms=5000)['brightness']==100
    assert ask('diagnostics_end',diagnostics_id='diag')['ok']
    assert not ask('diagnostics_backlight',diagnostics_id='diag',brightness=20)['ok']
    assert ask('diagnostics_start',diagnostics_id='expire',ttl_ms=100)['ok'];time.sleep(.2)
    assert not ask('diagnostics_backlight',diagnostics_id='expire',brightness=20)['ok']
    print('PASS: actual UDP brightness range/token/lease/end/expiry and persistence')
finally:
    proc.terminate();proc.wait(timeout=10);sock.close()
print('evidence:',root)
