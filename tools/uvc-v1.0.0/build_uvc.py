#!/usr/bin/env python3
# Rebuild official rk_mpi_uvc in WSL; never call the upstream recursive-clean Makefile.
from pathlib import Path
import subprocess, shutil, argparse, hashlib, json
p=argparse.ArgumentParser()
p.add_argument("--sdk",default="/home/guanjx/luckfox/uvc-official-sdk")
p.add_argument("--libs",default="/home/guanjx/luckfox/uvc-build/lib")
p.add_argument("--output",required=True)
a=p.parse_args()
sdk,libs,out=Path(a.sdk),Path(a.libs),Path(a.output)
expected="824b817f889c2cbff1d48fcdb18ab494a68f69d1"
actual=subprocess.check_output(["git","-C",str(sdk),"rev-parse","HEAD"],text=True).strip()
assert actual==expected, "Unexpected SDK revision"
out.mkdir(parents=True,exist_ok=True)
source=out/"source"
shutil.copytree(sdk/"project/app/uvc_app_tiny/uvc_app",source,dirs_exist_ok=True)
config=source/"uvc/uvc_mpi_config.c"
config.write_text(config.read_text().replace('"/oem/usr/share/uvc_mpi_cfg.conf"','"/tmp/instrument-uvc-test/uvc_mpi_cfg.conf"').replace('"/oem/usr/share//uvc_mpi_cfg.conf"','"/tmp/instrument-uvc-test/uvc_mpi_cfg.conf"'))
inc=sdk/"media/isp/release_camera_engine_rkaiq_rv1106_arm-rockchip830-linux-uclibcgnueabihf/include/rkaiq"
mpi=sdk/"media/rockit/rockit/mpi/sdk/include"
flags=" ".join("-I"+str(x) for x in [mpi,inc,*[inc/s for s in ["uAPI2","common","xcore","algos","iq_parser","iq_parser_v2"]]])+" -L"+str(libs)+" -Wl,-rpath-link,"+str(libs)
cross="/home/guanjx/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf"
subprocess.run(["cmake","-S",str(source),"-B",str(out/"build"),"-DCMAKE_SYSTEM_NAME=Linux","-DCMAKE_C_COMPILER="+cross+"-gcc","-DCMAKE_CXX_COMPILER="+cross+"-g++","-DCMAKE_C_FLAGS="+flags,"-DCMAKE_CXX_FLAGS="+flags,"-DCMAKE_BUILD_TYPE=Release"],check=True)
subprocess.run(["cmake","--build",str(out/"build"),"-j4"],check=True)
shutil.copy(out/"build/rk_mpi_uvc",out/"rk_mpi_uvc")
subprocess.run([cross+"-gcc","-Os","-Wall","-Wextra","-Werror",str(Path(__file__).with_name("udp_query.c")),"-o",str(out/"udp_query")],check=True)
print("Built in",out)
