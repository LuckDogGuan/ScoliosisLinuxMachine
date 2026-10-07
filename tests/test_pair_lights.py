"""Real coordinator: physical middle, pairing deadline, cancellation and success."""
import json, os, secrets, socket, subprocess, tempfile, time
from pathlib import Path
source=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="pair-light-check-") as tmp:
    root=Path(tmp); exe=root/"app"; buttons=root/"buttons"
    port=37023
    flags='-std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O2 -Iinclude -Ithird_party/cjson -Ithird_party/lwbtn/include '+"-DINSTRUMENT_ROOT="+chr(39)+chr(34)+str(root/'state')+chr(34)+chr(39)+f' -DDISCOVER_PORT={port}'
    subprocess.run(['make','CC=gcc',f'TARGET={exe}',f'CFLAGS={flags}'],cwd=source,check=True)
    config=json.loads((source/'config.example.json').read_text()); config['pc_base_url']='http://127.0.0.1:49159'
    cfg=root/'config.json';cfg.write_text(json.dumps(config));buttons.write_text('0')
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
    def press():
        buttons.write_text('2');time.sleep(3.2);buttons.write_text('0');time.sleep(.1)
    def traces():
        return [json.loads(line) for line in (root/'state/state/runtime.jsonl').read_text().splitlines()]
    try:
        for _ in range(20):
            try:boot=ask('hello_query')['boot_id'];break
            except socket.timeout:time.sleep(.05)
        time.sleep(.1);press()
        remaining=ask('hello_query')['pair_window_remaining_ms']
        assert 44000 < remaining <=45000, remaining
        print('PASS: physical long press opens a 45-second window',flush=True)
        press();assert ask('hello_query')['pair_window_remaining_ms']==0
        assert sum(r['event']=='pair_window_closed' for r in traces())==1
        print('PASS: second long press cancels once',flush=True)
        press();assert ask('pair_request',txn_id='test-txn',http_port=49159)['ok']
        assert ask('pair_commit',txn_id='test-txn',http_port=49159)['ok']
        assert ask('hello_query')['pair_window_remaining_ms']==0
        assert sum(r['event']=='pair_window_closed' for r in traces())==1
        print('PASS: pair success closes window without exit-timeout announcement',flush=True)
        press();assert not ask('hello_query')['paired_host_id']
        press();deadline=time.monotonic()+46
        while time.monotonic()<deadline:
            if ask('hello_query')['pair_window_remaining_ms']==0:break
            time.sleep(.2)
        else:raise AssertionError('window did not expire')
        assert not ask('pair_request',txn_id='late-txn',http_port=49159)['ok']
        rows=[r for r in traces() if r['event']=='pair_window_closed']
        assert len(rows)==2 and rows[-1]['detail']=='timeout', rows
        time.sleep(.2);assert len([r for r in traces() if r['event']=='pair_window_closed'])==2
        print('PASS: 45-second expiry clears transaction and announces exactly once',flush=True)
    finally:
        proc.terminate();proc.wait(timeout=10);sock.close()
