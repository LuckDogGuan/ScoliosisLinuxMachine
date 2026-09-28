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

    def do_POST(self):
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
        config["capture"]["codec"] = "h264"
        (path / "config.json").write_text(json.dumps(config))
        assert subprocess.run([str(ROOT / "instrument_app_host"), "--check-config",
                               str(path / "config.json")], check=False,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 2
finally:
    server.shutdown()
    worker.join()

print("binary upload, rejection, and invalid config OK")
