#include "platform.h"
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
static int button_inputs_init(void) {
    static const char setup[] =
        "#!/bin/sh\n"
        "# RV1106 GPIO0_A3/A4/A0 -> GND buttons. Only A0/A4 bias bits are changed.\n"
        "# Official pinctrl-rockchip.c: GPIO0 pull offset 0x38, two bits per pin,\n"
        "# 0=disabled, 1=pull-up, 2=pull-down; PMUIOC base 0xff388000.\n"
        "set -eu\n"
        "register=0xff388038\n"
        "mask=0x0303\n"
        "backup=/tmp/button-input-pull-before.txt\n"
        "before=$(devmem \"$register\" 32)\n"
        "if [ \"${1:-}\" = \"--restore\" ]; then\n"
        "    saved=$(cat \"$backup\")\n"
        "    value=$(( (mask << 16) | (saved & mask) ))\n"
        "    devmem \"$register\" 32 \"$value\"\n"
        "    echo \"Restored A0/A4 bias: $(devmem \"$register\" 32)\"\n"
        "    exit 0\n"
        "fi\n"
        "[ -f \"$backup\" ] || printf '%s\\n' \"$before\" > \"$backup\"\n"
        "devmem \"$register\" 32 0x03030101\n"
        "after=$(devmem \"$register\" 32)\n"
        "expected=$(( (before & ~mask) | 0x0101 ))\n"
        "[ \"$((after))\" -eq \"$expected\" ] || { echo 'Bias readback failed' >&2; exit 1; }\n"
        "for pin in 3 4 0; do\n"
        "    path=/sys/class/gpio/gpio$pin\n"
        "    if [ ! -d \"$path\" ]; then echo \"$pin\" > /sys/class/gpio/export; fi\n"
        "    echo in > \"$path/direction\"\n"
        "    [ \"$(cat \"$path/direction\")\" = in ] || exit 1\n"
        "done\n"
        ;
    char *args[] = {"sh", "-c", (char *)setup, "instrument-buttons", NULL};
    return run_command(args);
}
int platform_button_raw(int *mask) {
    const char *simulation = getenv("INSTRUMENT_BUTTON_FILE");
    int raw = 0, valid = 1;
    if (simulation) {
        FILE *file = fopen(simulation, "r");
        if (!file || fscanf(file, "%d", &raw) != 1 || raw < 0 || raw > 7) valid = 0;
        if (file) fclose(file);
    } else {
        static int ready;
        if (!ready) {
            if (button_inputs_init()) { *mask = 0; return -1; }
            ready = 1;
        }
        static const int pins[] = {3, 4, 0};
        for (int i = 0; i < 3; i++) {
            char path[80], level;
            snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pins[i]);
            int fd = open(path, O_RDONLY | O_CLOEXEC);
            if (fd < 0) { valid = 0; continue; }
            if (read(fd, &level, 1) != 1 || (level != '0' && level != '1')) valid = 0;
            else if (level == '0') raw |= 1 << i;
            close(fd);
        }
    }
    *mask = raw; return valid ? 0 : -1;
}
