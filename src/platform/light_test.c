/* Standalone diagnostic; business light_update and button paths stay unchanged. */
#include "app.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>
enum { PERIOD_NS=1000000, SLOW_MS=1000, FAST_MS=125, BREATH_MS=3000, STEP_MS=15 };
static volatile sig_atomic_t stopped;
static void stop_test(int sig) { stopped = sig; }
static long long now_ms(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) return -1;
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static const char *sysroot(void) {
#ifdef LIGHT_TEST_HOST
    const char *root = getenv("LIGHT_TEST_SYSFS");
    if (root) return root;
#endif
    return "/sys";
}
static int put(const char *path, const char *value) {
    int fd = open(path, O_WRONLY | O_CLOEXEC | O_TRUNC);
    if (fd < 0) { perror(path); return -1; }
    ssize_t count = write(fd, value, strlen(value));
    int saved = errno, result = close(fd);
    if (count != (ssize_t)strlen(value) || result) {
        errno = saved; perror(path); return -1;
    }
    return 0;
}
static int gpio_write(int pin, const char *attribute, const char *value) {
    char path[256];
    snprintf(path, sizeof(path), "%s/class/gpio/gpio%d/%s", sysroot(), pin, attribute);
    return put(path, value);
}
static int gpio_control(int pin, const char *operation) {
    char path[256], number[16];
    snprintf(path, sizeof(path), "%s/class/gpio/%s", sysroot(), operation);
    snprintf(number, sizeof(number), "%d", pin);
    return put(path, number);
}
static int gpio_free(int pin) {
    char path[256];
    snprintf(path, sizeof(path), "%s/class/gpio/gpio%d", sysroot(), pin);
#ifdef LIGHT_TEST_HOST
    return 1;
#else
    return access(path, F_OK) != 0;
#endif
}
static int gpio_claim(int pin, int *owned, int high) {
    if (!gpio_free(pin)) { fprintf(stderr, "GPIO%d occupied; refusing takeover\n", pin); return -1; }
    if (gpio_control(pin, "export")) return -1;
    *owned = 1;
    return gpio_write(pin, "direction", high ? "high" : "low");
}
static int overlay(const char *action) {
#ifdef LIGHT_TEST_HOST
    (void)action;
    return 0;
#else
    /* Hardware-specific setup is compiled into the adapter; no sibling .sh. */
    static const char setup[] =
        "#!/bin/sh\n"
        "# Existing luckfox-config configfs mechanism; transient PWM2_M0 only.\n"
        "set -eu\n"
        "overlay=/sys/kernel/config/device-tree/overlays/instrument-light-test\n"
        "dt=/sys/firmware/devicetree/base\n"
        "case \"$1\" in\n"
        "--enable)\n"
        "    [ \"$(tr -d '\\000' < \"$dt/pwm@ff350020/status\")\" = disabled ] || {\n"
        "        echo 'PWM2 already enabled; refusing takeover' >&2; exit 1;\n"
        "    }\n"
        "    [ ! -e \"$overlay\" ] || { echo 'Light overlay already exists' >&2; exit 1; }\n"
        "    command -v dtc >/dev/null\n"
        "    handle=$(od -An -tx1 \"$dt/pinctrl/pwm2/pwm2m0-pins/phandle\" | tr -d ' \\n')\n"
        "    case \"$handle\" in ??????\?\?) ;; *) echo 'Invalid PWM2_M0 phandle' >&2; exit 1;; esac\n"
        "    work=$(mktemp -d /tmp/instrument-light-dt.XXXXXX)\n"
        "    cleanup() {\n"
        "        rm -f \"$work/pwm.dts\"\n"
        "        rm -f \"$work/pwm.dtbo\"\n"
        "        rmdir \"$work\"\n"
        "    }\n"
        "    trap cleanup EXIT\n"
        "    cat > \"$work/pwm.dts\" <<EOF\n"
        "/dts-v1/;\n"
        "/plugin/;\n"
        "/ {\n"
        "    fragment@0 {\n"
        "        target-path = \"/pwm@ff350020\";\n"
        "        __overlay__ {\n"
        "            pinctrl-0 = <0x$handle>;\n"
        "            status = \"okay\";\n"
        "        };\n"
        "    };\n"
        "};\n"
        "EOF\n"
        "    dtc -I dts -O dtb -o \"$work/pwm.dtbo\" \"$work/pwm.dts\"\n"
        "    mkdir \"$overlay\"\n"
        "    if ! cat \"$work/pwm.dtbo\" > \"$overlay/dtbo\" || ! echo 1 > \"$overlay/status\"; then\n"
        "        echo 0 > \"$overlay/status\" || true\n"
        "        rmdir \"$overlay\" || true\n"
        "        exit 1\n"
        "    fi\n"
        "    [ \"$(cat \"$overlay/status\")\" = 1 ] || {\n"
        "        echo 'Overlay did not activate' >&2\n"
        "        echo 0 > \"$overlay/status\"\n"
        "        rmdir \"$overlay\"\n"
        "        exit 1\n"
        "    }\n"
        "    ;;\n"
        "--disable)\n"
        "    [ -d \"$overlay\" ] || exit 0\n"
        "    echo 0 > \"$overlay/status\"\n"
        "    rmdir \"$overlay\"\n"
        "    ;;\n"
        "*) echo 'Usage: light_pwm.sh --enable|--disable' >&2; exit 2;;\n"
        "esac\n"
        ;
    char *args[] = {"sh", "-c", (char *)setup, "instrument-pwm", (char *)action, NULL};
    return run_command(args);
#endif
}
static int pwm_write(const char *chip, const char *name, long value) {
    char path[512], number[32];
    snprintf(path, sizeof(path), "%s/pwm0/%s", chip, name);
    snprintf(number, sizeof(number), "%ld", value);
    return put(path, number);
}
static int pwm_find(char *chip, size_t size) {
#ifdef LIGHT_TEST_HOST
    snprintf(chip, size, "%s/class/pwm/pwmchip0", sysroot());
    return access(chip, F_OK);
#else
    for (int i=0; i<32; i++) {
        char link[256], target[512];
        snprintf(link,sizeof(link),"/sys/class/pwm/pwmchip%d/device",i);
        ssize_t count=readlink(link,target,sizeof(target)-1);
        if (count<0) continue;
        target[count]='\0';
        if (strstr(target,"/ff350020.pwm")) {
            snprintf(chip,size,"/sys/class/pwm/pwmchip%d",i); return 0;
        }
    }
    fputs("PWM2 unavailable; no software breathing fallback\n",stderr);
    return -1;
#endif
}
static long duty(const char *mode, long elapsed) {
    if (!strcmp(mode,"off")) return 0;
    if (!strcmp(mode,"on")) return PERIOD_NS;
    if (!strcmp(mode,"slow")) return elapsed/SLOW_MS%2 ? 0 : PERIOD_NS;
    if (!strcmp(mode,"fast")) return elapsed/FAST_MS%2 ? 0 : PERIOD_NS;
    long phase=elapsed%BREATH_MS;
    return PERIOD_NS*(phase<=BREATH_MS/2 ? phase : BREATH_MS-phase)/(BREATH_MS/2);
}
static int usage(void) {
    fputs("Usage: --light-test center off|on|slow|fast|breathe --ground-verified [--seconds 1..300] [--active-low]\n"
          "       --light-test backlight off|on [--seconds 1..300] [--active-low]\n",stderr);
    return 2;
}
int light_test_main(int argc,char **argv) {
    if (argc<2) return usage();
    int center=!strcmp(argv[0],"center"), seconds=30, ground_verified=0, active_low=0;
    if (!center && strcmp(argv[0],"backlight")) return usage();
    const char *mode=argv[1];
    if (strcmp(mode,"off") && strcmp(mode,"on") &&
        (!center || (strcmp(mode,"slow") && strcmp(mode,"fast") && strcmp(mode,"breathe")))) return usage();
    for (int i=2; i<argc; i++) {
        if (!strcmp(argv[i],"--ground-verified")) ground_verified=1;
        else if (!strcmp(argv[i],"--active-low")) active_low=1;
        else if (!strcmp(argv[i],"--seconds") && i+1<argc) {
            char *end; errno=0; long value=strtol(argv[++i],&end,10);
            if (errno || !*argv[i] || *end || value<1 || value>300) return usage();
            seconds=(int)value;
        } else return usage();
    }
    if (center && !ground_verified) {
        fputs("GPIO32 return path not verified; center test refused\n",stderr); return 2;
    }
#ifndef LIGHT_TEST_HOST
    FILE *model=fopen("/proc/device-tree/model","r");
    char name[128]={0};
    if (model) { size_t count=fread(name,1,sizeof(name)-1,model); name[count]=0; fclose(model); }
    if (geteuid() || !strstr(name,"Luckfox Pico Zero")) {
        fputs("Requires root on Luckfox Pico Zero\n",stderr); return 2;
    }
#endif
    int lock=open("/tmp/instrument-light-test.lock",O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
    if (lock<0 || flock(lock,LOCK_EX|LOCK_NB)) {
        fputs("Another light test is running or lock unavailable\n",stderr);
        if (lock>=0) close(lock);
        return 1;
    }
    struct sigaction handler={0};
    handler.sa_handler=stop_test; sigemptyset(&handler.sa_mask);
    sigaction(SIGINT,&handler,NULL); sigaction(SIGTERM,&handler,NULL); sigaction(SIGHUP,&handler,NULL);
    int ground_owned=0, output_owned=0, pwm_owned=0, overlay_owned=0, result=1;
    char chip[256]={0}, path[512];
    if (center) {
        if (!gpio_free(1) || !gpio_free(32)) { fputs("GPIO1/GPIO32 occupied\n",stderr); goto cleanup; }
        if (overlay("--enable")) goto cleanup;
        overlay_owned=1;
        if (pwm_find(chip,sizeof(chip))) goto cleanup;
        snprintf(path,sizeof(path),"%s/pwm0",chip);
#ifndef LIGHT_TEST_HOST
        if (!access(path,F_OK)) { fputs("PWM2 channel already owned\n",stderr); goto cleanup; }
#endif
        snprintf(path,sizeof(path),"%s/export",chip);
        if (put(path,"0")) goto cleanup;
        pwm_owned=1;
        if (pwm_write(chip,"period",PERIOD_NS) || pwm_write(chip,"duty_cycle",0) || pwm_write(chip,"enable",0)) goto cleanup;
        snprintf(path,sizeof(path),"%s/pwm0/polarity",chip);
        if (put(path,active_low ? "inversed" : "normal") || pwm_write(chip,"enable",1) ||
            gpio_claim(32,&ground_owned,0)) goto cleanup;
    } else if (gpio_claim(55,&output_owned,active_low)) goto cleanup;
    long long start=now_ms();
    if (start<0) goto cleanup;
    printf("LIGHT_TEST_START target=%s mode=%s seconds=%d active_low=%d ground_verified=%d\n",
           argv[0],mode,seconds,active_low,ground_verified); fflush(stdout);
    result=0;
    while (!stopped) {
        long long now=now_ms();
        if (now<0) { result=1; break; }
        if (now-start>=seconds*1000) break;
        long level=duty(mode,(long)(now-start));
        if ((center && pwm_write(chip,"duty_cycle",level)) ||
            (!center && gpio_write(55,"value",(level!=0)!=active_low ? "1" : "0"))) { result=1; break; }
        struct timespec wait={0,STEP_MS*1000000};
        while (nanosleep(&wait,&wait) && errno==EINTR && !stopped) {}
    }
cleanup:
    /* Keep return LOW until outputs are shut down. */
    if (output_owned) {
        if (gpio_write(55,"value",active_low ? "1" : "0")) result=1;
        if (gpio_control(55,"unexport")) result=1;
    }
    if (pwm_owned) {
        if (pwm_write(chip,"duty_cycle",0)) result=1;
        if (pwm_write(chip,"enable",0)) result=1;
        snprintf(path,sizeof(path),"%s/unexport",chip);
        if (put(path,"0")) result=1;
    }
    if (overlay_owned && overlay("--disable")) result=1;
    if (ground_owned) {
        if (gpio_write(32,"value","0")) result=1;
        if (gpio_control(32,"unexport")) result=1;
    }
    printf("LIGHT_TEST_END result=%d signal=%d outputs=%s\n",result,(int)stopped,result ? "check-errors" : "off");
    fflush(stdout); close(lock);
    return result ? result : stopped ? 128+stopped : 0;
}

/* Business indication shares the diagnostic PWM/GPIO helpers and exclusive lock.
 * Normal=steady; the actual open pairing window=breathing. No board work LED. */
static struct {
    int lock, ground, pwm, overlay, mode;
    long last_duty;
    long long changed, next, retry;
    char chip[256];
} status_light = {.lock=-1, .mode=-1, .last_duty=-1};
static int backlight_owned, backlight_level = -1;
int platform_backlight_set(int on) {
    if (getenv("INSTRUMENT_HOST_TEST")) return 0;
    if (status_light.lock < 0) return -1;
    if (!backlight_owned) {
        if (gpio_claim(55, &backlight_owned, 0)) return -1;
        backlight_level = -1;
    }
    if (backlight_level == on) return 0;
    if (gpio_write(55, "value", on ? "1" : "0")) return -1;
    backlight_level = on;
    return 0;
}
void light_status_close(void) {
    char path[512];
    if (backlight_owned) {
        (void)gpio_write(55, "value", "0");
        (void)gpio_control(55, "unexport");
        backlight_owned = 0; backlight_level = -1;
    }
    if (status_light.pwm) {
        (void)pwm_write(status_light.chip,"duty_cycle",0);
        (void)pwm_write(status_light.chip,"enable",0);
        snprintf(path,sizeof(path),"%s/unexport",status_light.chip);
        (void)put(path,"0");
    }
    if (status_light.overlay) (void)overlay("--disable");
    if (status_light.ground) {
        (void)gpio_write(32,"value","0");
        (void)gpio_control(32,"unexport");
    }
    if (status_light.lock>=0) close(status_light.lock);
    status_light.lock=-1; status_light.pwm=status_light.overlay=status_light.ground=0;
    status_light.mode=-1; status_light.last_duty=-1;
}
int light_status_tick(int pairing, long long now) {
    if (getenv("INSTRUMENT_HOST_TEST")) return 0;
    if (status_light.lock<0) {
        if (now<status_light.retry) return -1;
        status_light.retry=now+5000;
        status_light.lock=open("/tmp/instrument-light-test.lock",O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
        if (status_light.lock<0 || flock(status_light.lock,LOCK_EX|LOCK_NB)) goto failed;
        if (!gpio_free(1) || !gpio_free(32) || overlay("--enable")) goto failed;
        status_light.overlay=1;
        if (pwm_find(status_light.chip,sizeof(status_light.chip))) goto failed;
        char path[512];
        snprintf(path,sizeof(path),"%s/pwm0",status_light.chip);
#ifndef LIGHT_TEST_HOST
        if (!access(path,F_OK)) goto failed;
#endif
        snprintf(path,sizeof(path),"%s/export",status_light.chip);
        if (put(path,"0")) goto failed;
        status_light.pwm=1;
        snprintf(path,sizeof(path),"%s/pwm0/polarity",status_light.chip);
        if (pwm_write(status_light.chip,"period",PERIOD_NS) ||
            pwm_write(status_light.chip,"duty_cycle",0) || pwm_write(status_light.chip,"enable",0) ||
            put(path,"normal") || pwm_write(status_light.chip,"enable",1) ||
            gpio_claim(32,&status_light.ground,0)) goto failed;
    }
    if (status_light.mode!=pairing) {
        status_light.mode=pairing; status_light.changed=now; status_light.next=now;
    }
    if (now<status_light.next) return 0;
    status_light.next=now+STEP_MS;
    long level=duty(pairing ? "breathe" : "on",(long)((now-status_light.changed)%BREATH_MS));
    if (level!=status_light.last_duty && pwm_write(status_light.chip,"duty_cycle",level)) goto failed;
    status_light.last_duty=level;
    return 0;
failed:
    light_status_close();
    return -1;
}
