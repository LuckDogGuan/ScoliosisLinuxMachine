#include <stdio.h>
#include <unistd.h>

/* Same IIO voltage conversion as the supplied Luckfox example. */
int main(void) {
    const char *paths[] = {
        "/sys/bus/iio/devices/iio:device0/in_voltage_scale",
        "/sys/bus/iio/devices/iio:device0/in_voltage0_raw",
        "/sys/bus/iio/devices/iio:device0/in_voltage1_raw"
    };
    puts("sample,scale_mV_per_count,raw0,voltage0_V,raw1,voltage1_V");
    for (int sample = 1; sample <= 10; ++sample) {
        double values[3];
        for (int i = 0; i < 3; ++i) {
            FILE *file = fopen(paths[i], "r");
            if (!file) { perror(paths[i]); return 1; }
            int ok = fscanf(file, "%lf", &values[i]);
            fclose(file);
            if (ok != 1) { fprintf(stderr, "Invalid ADC value: %s\n", paths[i]); return 1; }
        }
        if (values[0] <= 0 || values[1] < 0 || values[1] > 1023 ||
            values[2] < 0 || values[2] > 1023) {
            fputs("Invalid 10-bit SARADC scale/raw range\n", stderr); return 1;
        }
        printf("%d,%.9f,%.0f,%.6f,%.0f,%.6f\n", sample, values[0],
               values[1], values[1]*values[0]/1000.0,
               values[2], values[2]*values[0]/1000.0);
        fflush(stdout);
        if (sample < 10) sleep(1);
    }
    return 0;
}
