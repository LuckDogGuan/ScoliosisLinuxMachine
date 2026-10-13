#include "v3.h"
#include "platform.h"
#include "lwbtn/lwbtn.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdatomic.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <sys/resource.h>
/* left / middle / right: validated GPIO 3 / 4 / 0, active low.
 * GPIO export, direction and pull-up belong to the platform adapter. */
static lwbtn_t manager;
static lwbtn_btn_t buttons[3];
static int initialized, stable, released;
static uint32_t previous;
static void changed(lwbtn_t *obj, lwbtn_btn_t *button, lwbtn_evt_t event) {
    (void)obj;
    unsigned bit = 1U << (unsigned)(button - buttons);
    if (event == LWBTN_EVT_ONPRESS) stable |= (int)bit;
    if (event == LWBTN_EVT_ONRELEASE) stable &= ~(int)bit;
}
int buttons_feed(uint32_t now, int valid, int raw_mask) {
    if (!initialized) {
        lwbtn_init_ex(&manager, buttons, 3, NULL, changed);
        initialized = 1;
        previous = now;
    }
    if (!valid || (uint32_t)(now - previous) > 100) {
        for (int i = 0; i < 3; i++) lwbtn_reset(&manager, &buttons[i]);
        stable = released = 0;
        previous = now;
        return -1;
    }
    previous = now;
    for (int i = 0; i < 3; i++) lwbtn_set_btn_state(&buttons[i], (raw_mask >> i) & 1);
    lwbtn_process_ex(&manager, now);
    /* Fail closed after boot/read failure until an actual all-release sample. */
    if (!raw_mask && !stable) released = 1;
    return released ? stable : -1;
}
/* Sampling stays independent of business I/O. Invalid/stale samples still fail closed. */
typedef struct { atomic_uint sequence, at; atomic_int mask; } ButtonSample;
static ButtonSample *sample;
static pid_t sampler;
static uint32_t sample_clock(void) {
    struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
    return (uint32_t)((uint64_t)now.tv_sec*1000+now.tv_nsec/1000000);
}
void buttons_close(void) {
    if (sampler && waitpid(sampler,NULL,WNOHANG)==0) {
        (void)kill(sampler,SIGTERM);
        for (int i=0;i<50 && waitpid(sampler,NULL,WNOHANG)==0;i++) usleep(10000);
        if (waitpid(sampler,NULL,WNOHANG)==0) {
            (void)kill(-sampler,SIGKILL);(void)kill(sampler,SIGKILL);
            while(waitpid(sampler,NULL,0)<0 && errno==EINTR) {}
        }
    }
    sampler=0;
    if (sample) {munmap(sample,sizeof(*sample));sample=NULL;}
}
static int sampling_start(void) {
    sample=mmap(NULL,sizeof(*sample),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
    if (sample==MAP_FAILED) {sample=NULL;return -1;}
    atomic_init(&sample->sequence,0);atomic_init(&sample->at,0);atomic_init(&sample->mask,-1);
    sampler=fork();
    if (sampler<0) {sampler=0;buttons_close();return -1;}
    if (!sampler) {
        (void)setpgid(0,0);(void)prctl(PR_SET_PDEATHSIG,SIGKILL);
        if (getppid()==1) _exit(1);
        signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);
        (void)setpriority(PRIO_PROCESS,0,-5);
        /* Do not retain coordinator sockets or worker pipes in this long-lived child. */
        for(int fd=3;fd<1024;fd++) close(fd);
        for (;;) {
            int raw=0,valid=!platform_button_raw(&raw);
            uint32_t at=sample_clock();int mask=buttons_feed(at,valid,raw);
            atomic_fetch_add_explicit(&sample->sequence,1,memory_order_acq_rel);
            atomic_store_explicit(&sample->mask,mask,memory_order_relaxed);
            atomic_store_explicit(&sample->at,at,memory_order_relaxed);
            atomic_fetch_add_explicit(&sample->sequence,1,memory_order_release);
            usleep(10000);
        }
    }
    (void)setpgid(sampler,sampler);return 0;
}
int buttons_snapshot(uint32_t now, int *mask) {
    (void)now;*mask=-1;
    if (sampler) {
        pid_t result=waitpid(sampler,NULL,WNOHANG);
        if (result==sampler || (result<0 && errno==ECHILD)) {sampler=0;buttons_close();}
    }
    if (!sampler && sampling_start()) return -1;
    for (int attempt=0;attempt<3;attempt++) {
        unsigned sequence=atomic_load_explicit(&sample->sequence,memory_order_acquire);
        if (sequence&1) continue;
        int value=atomic_load_explicit(&sample->mask,memory_order_relaxed);
        uint32_t at=atomic_load_explicit(&sample->at,memory_order_relaxed);
        if (sequence==atomic_load_explicit(&sample->sequence,memory_order_acquire)) {
            if ((uint32_t)(sample_clock()-at)<=100) *mask=value;
            break;
        }
    }
    return *mask<0 ? -1 : 0;
}

/* Gesture timing is independent of record/upload workers. Invalid samples and
 * disabled permissions fail closed; require a release before another hold. */
int backlight_tick(uint64_t now, int keys, int allowed) {
    static uint64_t held, release_at;
    static int pressing, lit, disarmed = 1;
    int desired = lit;
    /* buttons_snapshot already rejects stale/invalid samples independently of business I/O. */
    if (!allowed || keys < 0) {
        pressing = 0; release_at = 0; disarmed = 1; desired = 0;
    } else {
        if (!keys) disarmed = 0;
        if (keys == 5 && !disarmed) {
            if (!pressing) { pressing = 1; held = now; }
            release_at = 0;
            if (now - held >= 500) desired = 1;
        } else {
            if (pressing && lit) release_at = now + 1000;
            pressing = 0;
            if (release_at && now >= release_at) { desired = 0; release_at = 0; }
        }
    }
    if (platform_backlight_set(desired)) { lit = 0; return -1; }
    if (lit != desired) {
        instrument_trace("backlight", "", "", "", desired ? "on" : "off");
        lit = desired;
    }
    return 0;
}
