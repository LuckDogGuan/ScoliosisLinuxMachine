#define audio_play failed_audio
#define capture_controlled accepted_capture
#define http_v3_start accepted_start
#define storage_free_mb sufficient_disk
#define storage_memory_mb sufficient_memory
#include "../src/app/runtime_v3.c"
#undef audio_play
#undef capture_controlled
#undef http_v3_start
#undef storage_free_mb
#undef storage_memory_mb
#include <assert.h>
int failed_audio(const char *name) { (void)name; return -1; }
int accepted_start(const Config *config, const char *id, const char *gate, const char *boot) { (void)config;(void)id;(void)gate;(void)boot;return 0; }
long sufficient_disk(void) { return 1000; }
int sufficient_memory(long *available, long *total) { *available=128;*total=256;return 0; }
int accepted_capture(const Config *config,const char *id,int fd,int *duration,int *reason) { (void)config;(void)id;(void)fd;*duration=3000;*reason=0;return 0; }
int main(void) {
    assert(!mkdir(STATE_DIR,0700)); assert(!mkdir(EVENTS_DIR,0700));
    Config config={.bitrate_kbps=2000}; strcpy(config.device_id,"CAM-test");
    Runtime rt={.config=&config,.udp=-1,.gate=1,.lease=v3_now()+6000,.keys=5};
    COPY(rt.capture_id,"CAM-test-audio-failure");COPY(rt.gate_id,"gate-test");COPY(rt.boot,"boot-test");
    queue_sound(&rt,"capture_enabled");
    start_record(&rt);assert(rt.record.pid>0);
    WorkerResult result;assert(recv(rt.record.fd,&result,sizeof(result),0)==(ssize_t)sizeof(result));
    assert(result.result==0 && result.duration==3000);
    assert(waitpid(rt.record.pid,NULL,0)==rt.record.pid); finish_worker(&rt.record);
    puts("PASS: failed prompt cannot prevent an authorized capture");
}
