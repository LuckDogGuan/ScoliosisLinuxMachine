#define sample sampled_state
#include "../src/middleware/buttons.c"
#undef sample
#include "platform.h"
#include <assert.h>
#include <sys/mman.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
static volatile int *raw;
int platform_button_raw(int *mask) {*mask=*raw;return *raw>7 ? -1 : 0;}
int platform_backlight_set(int on) {(void)on;return 0;}
void instrument_trace(const char*a,const char*b,const char*c,const char*d,const char*e) {(void)a;(void)b;(void)c;(void)d;(void)e;}
static uint32_t now(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint32_t)((uint64_t)t.tv_sec*1000+t.tv_nsec/1000000);}
static int sample(void) {int mask;buttons_snapshot(now(),&mask);return mask;}
static void expect(int value) {for(int i=0;i<100;i++){if(sample()==value)return;usleep(10000);}assert(0);}
int main(void) {
 raw=mmap(NULL,sizeof(*raw),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);assert(raw!=MAP_FAILED);
 *raw=0;expect(0);*raw=5;expect(5);
 usleep(250000);assert(sample()==5);
 *raw=9;expect(-1);*raw=5;usleep(100000);assert(sample()==-1);
 *raw=0;expect(0);*raw=5;expect(5);
 pid_t first=sampler;assert(first>0);kill(first,SIGSTOP);usleep(150000);assert(sample()==-1);
 kill(first,SIGCONT);usleep(50000);assert(sample()==-1);*raw=0;expect(0);*raw=5;expect(5);
 kill(first,SIGKILL);usleep(20000);assert(sample()==-1);*raw=0;expect(0);*raw=5;expect(5);
 pid_t last=sampler;assert(last>0 && last!=first);kill(last,SIGSTOP);
 uint32_t closing=now();buttons_close();assert(now()-closing<1000);assert(waitpid(last,NULL,WNOHANG)==-1 && errno==ECHILD);
 puts("PASS: business loop pause preserves continuously sampled hold; raw failure still requires all-release");return 0;
}
