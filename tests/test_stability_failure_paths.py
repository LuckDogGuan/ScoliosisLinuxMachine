"""Failed camera and EOF must never permit overlapping recordings."""
import pathlib,subprocess,tempfile
root=pathlib.Path(__file__).resolve().parents[1]
evidence=tempfile.mkdtemp(prefix="stability-failure-paths-")
sources=[str(p) for p in (root/"src").rglob("*.c") if p.name not in {"runtime_v3.c","app_controller.c","luckfox_mux_adapter.c"}]
subprocess.run(["gcc","-std=c11","-D_POSIX_C_SOURCE=200809L","-D_DEFAULT_SOURCE","-Wall","-Wextra","-Werror","-O2","-ffunction-sections","-fdata-sections",f'-DINSTRUMENT_ROOT="{evidence}"',"-Iinclude","-Ithird_party/cjson","-Ithird_party/lwbtn/include","tests/test_stability_failure_paths.c",*sources,"third_party/cjson/cJSON.c","third_party/lwbtn/lwbtn.c","-Wl,--gc-sections","-o",evidence+"/check"],cwd=root,check=True)
result=subprocess.run([evidence+"/check"],check=False,timeout=15)
print("Evidence:",evidence)
raise SystemExit(result.returncode)
