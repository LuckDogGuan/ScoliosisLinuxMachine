#define audio_play check_audio_play
#include "../src/app/runtime_v3.c"
#undef audio_play
#include <assert.h>
int check_audio_play(const char *sound) {assert(!strcmp(sound,"boot_ready"));return 0;}
int main(void) {
 Config config={.volume=10};Runtime rt={.config=&config,.udp=-1,.audio_result=123};COPY(rt.phase,"idle");COPY(rt.sounds[0],"boot_ready");rt.sound_in=1;
 int fd,branch=make_worker(&rt.maintenance,&fd);assert(branch>=0);if(!branch){for(;;)pause();}
 pid_t network=rt.maintenance.pid;uint64_t start=v3_now();maintenance(&rt,start);
 assert(v3_now()-start<100);assert(rt.audio.pid>0);assert(rt.maintenance.pid==network);
 for(int i=0;i<100 && rt.audio.pid;i++){usleep(1000);audio_messages(&rt);}
 assert(!rt.audio.pid);assert(rt.audio_result==123 && config.volume==10);
 abort_worker(&rt.maintenance);assert(waitpid(network,NULL,0)==network);
 puts("PASS: startup prompt runs while Wi-Fi maintenance is blocked; business loop and volume result stay independent");return 0;
}
