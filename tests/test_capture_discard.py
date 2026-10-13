"""Proof-bound discard regression; leaves its isolated evidence directory."""
import pathlib
import subprocess
import tempfile
root = pathlib.Path(__file__).resolve().parents[1]
evidence = tempfile.mkdtemp(prefix="scoliosis-discard-")
subprocess.run(["gcc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra", "-Werror", "-O2", "-ffunction-sections", "-fdata-sections", f'-DINSTRUMENT_ROOT="{evidence}"', "-Iinclude", "-Ithird_party/cjson", "-Ithird_party/lwbtn/include", "tests/test_capture_discard.c", "src/middleware/persistence.c", "src/platform/clock.c", "third_party/cjson/cJSON.c", "-Wl,--gc-sections", "-o", "tests/.discard_check"], cwd=root, check=True)
subprocess.run(["tests/.discard_check"], cwd=root, check=True)
print("evidence:", evidence)
