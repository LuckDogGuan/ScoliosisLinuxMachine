"""Host contract: pending media is deleted only after matching durable receipt."""

import hashlib
import http.server
import json
import pathlib
import subprocess
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
CAPTURE = "CAM-000123-abcd"
PAYLOAD = b"\x00\x00\x00\x18ftypisom" + bytes(range(256)) * 500


class Handler(http.server.BaseHTTPRequestHandler):
    receipt = None
    uploads = 0
    reject_upload = False
    upload_delay = 0

    def reply(self, status, body):
        data = json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        assert self.path == f"/api/v1/captures/{CAPTURE}/receipt"
        self.reply(200, self.receipt or {"ok": True, "stored": False})

    def do_POST(self):
        if self.path == "/api/v1/capture/status":
            self.reply(200, {"ok": True})
            return
        assert self.path == "/api/v1/capture/file"
        payload = self.rfile.read(int(self.headers["Content-Length"]))
        assert payload == PAYLOAD
        type(self).uploads += 1
        if self.upload_delay:
            time.sleep(self.upload_delay)
        if self.reject_upload:
            self.reply(500, {"ok": False})
        else:
            type(self).receipt = correct_receipt
            self.reply(200, {"ok": True, "stored": True})

    def log_message(self, *_args):
        pass


correct_receipt = {
    "ok": True,
    "stored": True,
    "capture_id": CAPTURE,
    "size_bytes": len(PAYLOAD),
    "sha256": hashlib.sha256(PAYLOAD).hexdigest(),
}

source_files = [
    str(path) for path in (ROOT / "src").rglob("*.c")
    if path.name not in {"app_controller.c", "legacy.c", "luckfox_mux_adapter.c"}
]
binary = ROOT / "tests" / ".pending_harness"
sample = ROOT / "tests" / ".pending_sample.mp4"
subprocess.run(
    ["gcc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-D_DEFAULT_SOURCE",
     "-Wall", "-Wextra", "-Werror", "-O2", "-Iinclude", "-Ithird_party/cjson", "-Ithird_party/lwbtn/include",
     "tests/pending_harness.c", *source_files, "third_party/cjson/cJSON.c", "third_party/lwbtn/lwbtn.c",
     "-o", str(binary)], cwd=ROOT, check=True,
)
server = http.server.HTTPServer(("127.0.0.1", 0), Handler)
worker = threading.Thread(target=server.serve_forever, daemon=True)
worker.start()
try:
    url = f"http://127.0.0.1:{server.server_port}"

    def run(expected, mode="sync"):
        sample.write_bytes(PAYLOAD)
        result = subprocess.run([str(binary), url, CAPTURE, str(sample), mode], check=False)
        assert result.returncode == expected
        assert sample.exists() == bool(expected)

    Handler.receipt = correct_receipt
    run(0)
    assert Handler.uploads == 0  # lost POST response: query receipt before retry

    Handler.receipt = dict(correct_receipt, sha256="0" * 64)
    run(1)
    assert Handler.uploads == 0  # bad hash must never delete

    Handler.receipt = None
    run(0)
    assert Handler.uploads == 1

    Handler.receipt = None
    Handler.reject_upload = True
    run(1)
    assert Handler.uploads == 2

    Handler.receipt = None
    Handler.reject_upload = False
    Handler.upload_delay = 1
    run(0, "async")  # worker remains active while the controller could record
    assert Handler.uploads == 3
finally:
    server.shutdown()
    worker.join()
    if sample.exists():
        sample.unlink()
    binary.unlink()

print("pending receipt, hash mismatch, retry, rejection, and async worker OK")
