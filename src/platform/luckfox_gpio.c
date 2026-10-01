#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
int platform_button_raw(int *mask) {
    const char *simulation = getenv("INSTRUMENT_BUTTON_FILE");
    int raw = 0, valid = 1;
    if (simulation) {
        FILE *file = fopen(simulation, "r");
        if (!file || fscanf(file, "%d", &raw) != 1 || raw < 0 || raw > 7) valid = 0;
        if (file) fclose(file);
    } else {
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
