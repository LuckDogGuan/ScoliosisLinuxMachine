"""One runnable regression for mode timing, argument guards, cleanup, signals and failures."""
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="instrument-light-check-") as tmp:
    tmp = Path(tmp)
    harness = tmp / "check.c"
    harness.write_text("""
#define LIGHT_TEST_HOST
#include "light_test.c"
#include <assert.h>
int main(int argc, char **argv) {
    assert(duty("off",0)==0 && duty("on",0)==1000000);
    assert(duty("slow",999)==1000000 && duty("slow",1000)==0);
    assert(duty("fast",124)==1000000 && duty("fast",125)==0);
    assert(duty("breathe",0)==0 && duty("breathe",1500)==1000000 && duty("breathe",3000)==0);
    return light_test_main(argc-1,argv+1);
}
""")
    exe = tmp / "check"
    subprocess.run(["gcc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-D_DEFAULT_SOURCE",
                    "-Wall", "-Wextra", "-Werror", "-I"+str(root/"include"),
                    "-I"+str(root/"src/platform"), str(harness), "-o", str(exe)],check=True)
    files = ["class/gpio/export", "class/gpio/unexport"]
    files += [f"class/gpio/gpio{pin}/{name}" for pin in (32,55)
              for name in ("direction","value")]
    files += ["class/pwm/pwmchip0/export", "class/pwm/pwmchip0/unexport"]
    files += ["class/pwm/pwmchip0/pwm0/"+name for name in ("enable","duty_cycle","period","polarity")]
    for name in files:
        path = tmp / name
        path.parent.mkdir(parents=True,exist_ok=True)
        path.write_text("")
    env = dict(os.environ,LIGHT_TEST_SYSFS=str(tmp))
    def run(*args,expected=0):
        result = subprocess.run([str(exe),*args],env=env,capture_output=True,text=True,timeout=5)
        assert result.returncode==expected, (args,result.stdout,result.stderr)
        return result
    run("center","breathe",expected=2)
    for value in ("0","301","2x","nan","999999999999999999999",""):
        run("backlight","on","--seconds",value,expected=2)
    run("backlight","slow",expected=2)
    run("center","nonsense","--ground-verified",expected=2)
    for mode in ("off","on","slow","fast","breathe"):
        run("center",mode,"--ground-verified","--seconds","1")
        assert (tmp/"class/pwm/pwmchip0/pwm0/enable").read_text().strip()=="0"
        assert (tmp/"class/pwm/pwmchip0/pwm0/duty_cycle").read_text().strip()=="0"
        assert (tmp/"class/gpio/gpio32/value").read_text().strip()=="0"
    for polarity in ([],["--active-low"]):
        run("backlight","on","--seconds","1",*polarity)
        assert (tmp/"class/gpio/gpio55/value").read_text().strip()==("1" if polarity else "0")
    for sig in (signal.SIGINT,signal.SIGTERM,signal.SIGHUP):
        child = subprocess.Popen([str(exe),"center","breathe","--ground-verified"],
                                 env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        assert "START" in child.stdout.readline()
        conflict = run("backlight","on","--seconds","1",expected=1)
        assert "Another light test" in conflict.stderr
        child.send_signal(sig)
        out,err = child.communicate(timeout=3)
        assert child.returncode==128+sig,(out,err,child.returncode)
        assert (tmp/"class/pwm/pwmchip0/pwm0/duty_cycle").read_text().strip()=="0"
    # Inject output write failure after successful GPIO export: cleanup must still unexport.
    (tmp/"class/gpio/gpio55/direction").unlink()
    run("backlight","on","--seconds","1",expected=1)
    assert (tmp/"class/gpio/unexport").read_text().strip()=="55"
print("PASS: five modes/timing, polarity, input guards, exclusive lock, SIGINT/TERM/HUP, fault cleanup")
