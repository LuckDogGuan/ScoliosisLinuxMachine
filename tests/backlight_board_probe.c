/* Real-board driver probe; stop the idle app first, keep config/pair untouched. */
#include "../src/platform/light_test.c"
#include <assert.h>
static long read_attribute(const char *path) {
    FILE *f=fopen(path,"r"); long v=-1;
    if (f) { if(fscanf(f,"%ld",&v)!=1) v=-1; fclose(f); }
    return v;
}
int main(void) {
    int values[]={0,25,50,75,100,25};
    if(light_status_tick(LIGHT_CONNECTED,0)) return 2;
    int rc=0;
    for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);++i) {
        int percent=values[i];
        if(platform_backlight_percent(percent)) {rc=3;break;}
        char path[512]; long actual;
        if(backlight_pwm) {
            snprintf(path,sizeof(path),"%s/pwm0/duty_cycle",backlight_chip);
            actual=read_attribute(path);
            if(actual!=PERIOD_NS*(100-percent)/100) rc=4;
            printf("LEVEL=%d PWM_DUTY=%ld\n",percent,actual);
        } else {
            actual=read_attribute("/sys/class/gpio/gpio55/value");
            if(actual!=(percent==100)) rc=5;
            printf("LEVEL=%d GPIO55=%ld\n",percent,actual);
        }
        fflush(stdout); if(rc) break; sleep(1);
    }
    light_status_close();
    char *args[]={"sh","-c","set -e; mux=$(devmem 0xff538014 32); pin=$(devmem 0xff530070 32); echo CLOSE_MUX=$mux CLOSE_PAD=$pin; [ $((mux & 0xf000)) -eq 0 ]; [ $((pin & 0x00800000)) -eq 0 ]",NULL};
    if(run_command(args)) rc=6;
    printf("PROBE_EXIT=%d\n",rc); return rc;
}
