"""Check the real bundle installer with historical and new sound sets; never install."""
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="instrument-sound-count-") as temporary:
    package = Path(temporary)
    (package / "sounds").mkdir()
    (package / "deploy").mkdir()
    shutil.copyfile(root / "deploy/install_bundle.sh", package / "deploy/install_bundle.sh")
    for name in ("instrument_app", "luckfox_mux_adapter.so", "deploy/RkLunch.sh", "deploy/wpa_supplicant.default.conf"):
        (package / name).write_bytes(b"fixture")
    for index in range(20):
        (package / f"sounds/base{index}.wav").write_bytes(b"fixture")

    def check(success):
        files = sorted(p for p in package.rglob("*") if p.is_file() and p.name != "SHA256SUMS")
        manifest = "".join(hashlib.sha256(p.read_bytes()).hexdigest() + "  " + p.relative_to(package).as_posix() + "\n" for p in files)
        (package / "SHA256SUMS").write_text(manifest)
        result = subprocess.run(["sh", "deploy/install_bundle.sh", "--check"], cwd=package, capture_output=True, text=True)
        assert (result.returncode == 0) == success, result.stdout + result.stderr
        if success:
            assert "CHECK_OK" in result.stdout

    check(True)  # Historical 20-file payload.
    for name in ("pair_window_closed", "boot_ready", "detection_not_enabled", "class_completed"):
        (package / f"sounds/{name}.wav").write_bytes(b"fixture")
        check(True)  # Historical 21/22 and additive 23/24-file payloads.
    (package / "sounds/unexpected.wav").write_bytes(b"fixture")
    check(False)  # Even a hashed unknown extra sound must be rejected.
print("PASS: real installer accepts historical 20/21/22 and new 23/24 sounds, rejects unknown extra.")
