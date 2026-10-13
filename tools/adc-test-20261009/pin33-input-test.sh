#!/bin/sh
set -eu
GPIO=/sys/class/gpio/gpio52
ADC=/sys/bus/iio/devices/iio:device0
if [ -e "$GPIO" ]; then echo "GPIO52 already exported; refuse to change existing owner" >&2; exit 1; fi
owned=0
cleanup() { if [ "$owned" = 1 ]; then echo 52 > /sys/class/gpio/unexport; fi; }
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
echo 52 > /sys/class/gpio/export
owned=1
# Input only: never drive the externally powered line.
echo in > "$GPIO/direction"
echo "sample,pin33_digital,adc0_raw,adc1_raw,scale_mV_per_count"
for n in 1 2 3 4 5 6 7 8 9 10; do
    printf '%s,%s,%s,%s,%s\n' "$n" "$(cat "$GPIO/value")" "$(cat "$ADC/in_voltage0_raw")" "$(cat "$ADC/in_voltage1_raw")" "$(cat "$ADC/in_voltage_scale")"
    if [ "$n" != 10 ]; then sleep 1; fi
done
