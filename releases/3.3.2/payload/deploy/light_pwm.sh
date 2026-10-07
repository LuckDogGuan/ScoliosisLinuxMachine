#!/bin/sh
# Existing luckfox-config configfs mechanism; transient PWM2_M0 only.
set -eu
overlay=/sys/kernel/config/device-tree/overlays/instrument-light-test
dt=/sys/firmware/devicetree/base
case "$1" in
--enable)
    [ "$(tr -d '\000' < "$dt/pwm@ff350020/status")" = disabled ] || {
        echo 'PWM2 already enabled; refusing takeover' >&2; exit 1;
    }
    [ ! -e "$overlay" ] || { echo 'Light overlay already exists' >&2; exit 1; }
    command -v dtc >/dev/null
    handle=$(od -An -tx1 "$dt/pinctrl/pwm2/pwm2m0-pins/phandle" | tr -d ' \n')
    case "$handle" in ????????) ;; *) echo 'Invalid PWM2_M0 phandle' >&2; exit 1;; esac
    work=$(mktemp -d /tmp/instrument-light-dt.XXXXXX)
    cleanup() {
        rm -f "$work/pwm.dts"
        rm -f "$work/pwm.dtbo"
        rmdir "$work"
    }
    trap cleanup EXIT
    cat > "$work/pwm.dts" <<EOF
/dts-v1/;
/plugin/;
/ {
    fragment@0 {
        target-path = "/pwm@ff350020";
        __overlay__ {
            pinctrl-0 = <0x$handle>;
            status = "okay";
        };
    };
};
EOF
    dtc -I dts -O dtb -o "$work/pwm.dtbo" "$work/pwm.dts"
    mkdir "$overlay"
    if ! cat "$work/pwm.dtbo" > "$overlay/dtbo" || ! echo 1 > "$overlay/status"; then
        echo 0 > "$overlay/status" || true
        rmdir "$overlay" || true
        exit 1
    fi
    [ "$(cat "$overlay/status")" = 1 ] || {
        echo 'Overlay did not activate' >&2
        echo 0 > "$overlay/status"
        rmdir "$overlay"
        exit 1
    }
    ;;
--disable)
    [ -d "$overlay" ] || exit 0
    echo 0 > "$overlay/status"
    rmdir "$overlay"
    ;;
*) echo 'Usage: light_pwm.sh --enable|--disable' >&2; exit 2;;
esac
