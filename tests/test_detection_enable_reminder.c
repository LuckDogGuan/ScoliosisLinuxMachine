#include "../src/app/runtime_v3.c"
#include <assert.h>
static void held(Runtime *rt,uint64_t now) { rt->last_host_contact=now; assert(!capture_gesture(rt,now)); }
static void released(Runtime *rt,uint64_t now) { rt->keys=0; assert(!capture_gesture(rt,now)); rt->keys=5; }
int main(void) {
 Config cfg={0};Runtime rt={.config=&cfg,.keys=5,.paired=1};
 held(&rt,10000);held(&rt,11499);assert(rt.sound_in==rt.sound_out);
 held(&rt,11500);assert(rt.sound_in-rt.sound_out==1);
 assert(!strcmp(rt.sounds[rt.sound_out++%8],"detection_not_enabled"));
 held(&rt,40000);assert(rt.sound_in==rt.sound_out); /* A continuous hold never repeats. */
 released(&rt,40001);held(&rt,41000);held(&rt,42500);
 assert(rt.sound_in-rt.sound_out==1);rt.sound_out=rt.sound_in;
 released(&rt,42501);held(&rt,43000);held(&rt,44500);assert(rt.sound_in==rt.sound_out);
 held(&rt,62499);assert(rt.sound_in==rt.sound_out);
 held(&rt,62500);assert(rt.sound_in-rt.sound_out==1);rt.sound_out=rt.sound_in;
 Runtime blocked={.config=&cfg,.keys=5,.paired=1,.gate=1,.lease=100000};
 held(&blocked,10000);held(&blocked,12000);assert(blocked.sound_in==blocked.sound_out);
 blocked.gate=0;blocked.paired=0;held(&blocked,13000);held(&blocked,15000);assert(blocked.sound_in==blocked.sound_out);
 blocked.paired=1;blocked.last_host_contact=1;assert(!capture_gesture(&blocked,20000));assert(!capture_gesture(&blocked,22000));assert(blocked.sound_in==blocked.sound_out);
 blocked.diagnostics_lease=100000;held(&blocked,23000);held(&blocked,25000);assert(blocked.sound_in==blocked.sound_out);
 blocked.diagnostics_lease=0;blocked.camera_fault=1;held(&blocked,26000);held(&blocked,28000);assert(blocked.sound_in==blocked.sound_out);
 blocked.camera_fault=0;blocked.record.pid=123;held(&blocked,29000);held(&blocked,31000);assert(blocked.sound_in==blocked.sound_out);blocked.record.pid=0;
 blocked.completion_pending=1;held(&blocked,32000);held(&blocked,34000);assert(blocked.sound_in==blocked.sound_out);blocked.completion_pending=0;
 held(&blocked,35000);held(&blocked,36500);assert(blocked.sound_in-blocked.sound_out==1);
 blocked.gate=1;blocked.lease=100000;held(&blocked,36501);assert(blocked.sound_in==blocked.sound_out); /* Enabling cancels an obsolete queued reminder. */
 puts("PASS: 1500ms hold, once per hold, 20s cooldown, connected/idle guards, stale reminder cancellation");
 return 0;
}
