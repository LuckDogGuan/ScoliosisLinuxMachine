"""Run actual prompt policy and maintenance transitions without board access."""
import json
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
evidence = pathlib.Path(tempfile.mkdtemp(prefix="startup-network-prompts-"))
sources = [str(p) for p in (root / "src").rglob("*.c")
           if p.name not in {"runtime_v3.c", "app_controller.c", "luckfox_mux_adapter.c"}]
subprocess.run(["gcc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-D_DEFAULT_SOURCE",
                "-Wall", "-Wextra", "-Werror", "-O2", "-ffunction-sections", "-fdata-sections",
                f'-DINSTRUMENT_ROOT="{evidence}"', "-Iinclude", "-Ithird_party/cjson",
                "-Ithird_party/lwbtn/include", "tests/test_startup_network_prompts.c", *sources,
                "third_party/cjson/cJSON.c", "third_party/lwbtn/lwbtn.c",
                "-Wl,--gc-sections", "-o", str(evidence / "check")], cwd=root, check=True)
results = [subprocess.run([str(evidence / "check"), mode], timeout=10).returncode
           for mode in ("boot", "network")]
assets = json.loads((root / "sounds/stability-voice-manifest.json").read_text())["files"]
names = [pathlib.Path(row["file"]).stem for row in assets]
assert len(names) == len(set(names)) == 24
results.append(subprocess.run([str(evidence / "check"), "assets", *names], timeout=10).returncode)
print("Evidence:", evidence, flush=True)
assert results == [0, 0, 0], results
