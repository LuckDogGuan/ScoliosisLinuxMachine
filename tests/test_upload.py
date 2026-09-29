"""Host-only contract check: binary POST must survive embedded NUL bytes."""

import http.server
import json
import pathlib
import subprocess
import tempfile
import threading

ROOT = pathlib.Path(__file__).resolve().parents[1]
PAYLOAD = b"\x00\x00\x00\x18ftypisom\x00\x01\x00\x00" + bytes(range(256)) * 20


class Handler(http.server.BaseHTTPRequestHandler):
    received = None
    accepted = True
    status = 200
    hello_requests = []
    hello_volume = 15

    def do_POST(self):
        if self.path == "/api/v1/hello":
            size = int(self.headers.get("Content-Length", 0))
            payload = json.loads(self.rfile.read(size).decode()) if size else {}
            type(self).hello_requests.append(payload)
            resp = {"ok": True, "paired": True, "received": True}
            if type(self).hello_volume is not None:
                resp["volume"] = type(self).hello_volume
            body = json.dumps(resp).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        assert self.path == "/api/v1/capture/file"
        assert self.headers["Capture-ID"] == "CAM-000123-1234abcd"
        size = int(self.headers["Content-Length"])
        type(self).received = self.rfile.read(size)
        body = json.dumps({"ok": type(self).accepted}).encode()
        self.send_response(type(self).status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_args):
        pass


server = http.server.HTTPServer(("127.0.0.1", 0), Handler)
worker = threading.Thread(target=server.serve_forever, daemon=True)
worker.start()
try:
    with tempfile.TemporaryDirectory() as directory:
        path = pathlib.Path(directory)
        config = json.loads((ROOT / "config.example.json").read_text())
        config["pc_base_url"] = f"http://127.0.0.1:{server.server_port}"
        (path / "config.json").write_text(json.dumps(config))
        (path / "sample.mp4").write_bytes(PAYLOAD)
        command = [str(ROOT / "instrument_app_host"), "--upload-file",
                   str(path / "config.json"), "CAM-000123-1234abcd", str(path / "sample.mp4")]
        assert subprocess.run(command, check=False).returncode == 0
        assert Handler.received == PAYLOAD
        Handler.accepted = False
        assert subprocess.run(command, check=False).returncode == 1
        Handler.accepted = True
        Handler.status = 500
        assert subprocess.run(command, check=False).returncode == 1

        # Test hello volume sync: host returns volume 15, device updates and persists
        config = json.loads((ROOT / "config.example.json").read_text())
        config["pc_base_url"] = f"http://127.0.0.1:{server.server_port}"
        config["audio"]["volume"] = 10
        (path / "config.json").write_text(json.dumps(config))
        Handler.hello_requests.clear()
        Handler.hello_volume = 15

        res = subprocess.run([str(ROOT / "instrument_app_host"), "--hello", str(path / "config.json")],
                             check=False, capture_output=True, text=True)
        assert res.returncode == 0
        assert "[APP] Host requested volume change: 10 -> 15" in res.stdout
        assert len(Handler.hello_requests) == 1
        assert Handler.hello_requests[0]["volume"] == 10

        # Check config.json was atomically persisted with volume 15
        updated_config = json.loads((path / "config.json").read_text())
        assert updated_config["audio"]["volume"] == 15

        # Run hello again; config now has volume 15, request sends volume 15, no change requested
        res2 = subprocess.run([str(ROOT / "instrument_app_host"), "--hello", str(path / "config.json")],
                              check=False, capture_output=True, text=True)
        assert res2.returncode == 0
        assert len(Handler.hello_requests) == 2
        assert Handler.hello_requests[1]["volume"] == 15
        assert "[APP] Host requested volume change" not in res2.stdout

        # Test --save-volume CLI directly
        assert subprocess.run([str(ROOT / "instrument_app_host"), "--save-volume",
                               str(path / "config.json"), "20"], check=False).returncode == 0
        assert json.loads((path / "config.json").read_text())["audio"]["volume"] == 20

        # Test boundary / invalid volumes
        assert subprocess.run([str(ROOT / "instrument_app_host"), "--save-volume",
                               str(path / "config.json"), "31"], check=False).returncode != 0
        assert subprocess.run([str(ROOT / "instrument_app_host"), "--save-volume",
                               str(path / "config.json"), "-1"], check=False).returncode != 0

        config["capture"]["codec"] = "h264"
        (path / "config.json").write_text(json.dumps(config))
        assert subprocess.run([str(ROOT / "instrument_app_host"), "--check-config",
                               str(path / "config.json")], check=False,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 2
finally:
    server.shutdown()
    worker.join()

print("binary upload, hello volume sync, save-volume, and invalid config OK")
