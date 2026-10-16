#include "../src/app/runtime_v3.c"
#include <assert.h>
int main(void) {
 Config config={0}; Runtime rt={.config=&config,.gate=1,.lease=100000,.keys=5,.hold_started=1000};
 COPY(rt.capture_id,"capture-test"); COPY(rt.phase,"prepared"); COPY(rt.speech_id,"pc-name");rt.speech_lease=5000;
 assert(!capture_gesture(&rt,1799));assert(!rt.prompt_reserved);
 assert(!capture_gesture(&rt,1800));assert(rt.prompt_reserved);assert(!rt.speech_lease);
 assert(!capture_gesture(&rt,1999));assert(!rt.prompt_fired);
 assert(!capture_gesture(&rt,2000));assert(rt.prompt_fired);assert(rt.sound_in-rt.sound_out==1);
 assert(!capture_gesture(&rt,2199));rt.sound_out=rt.sound_in;assert(capture_gesture(&rt,2200));assert(rt.sound_in==rt.sound_out);
 assert(!instrument_can_start(0,1,1,0,1199));assert(instrument_can_start(0,1,1,0,1200));
 rt.keys=1;assert(!capture_gesture(&rt,2201));assert(!rt.prompt_fired);assert(rt.sound_in==rt.sound_out);
 rt.keys=5;rt.release_required=1;assert(!capture_gesture(&rt,4000));assert(!rt.prompt_reserved);
 rt.release_required=0;rt.camera_fault=1;assert(!capture_gesture(&rt,5000));assert(!rt.prompt_fired);
 unsigned before=rt.sound_in;queue_sound(&rt,"uploaded");queue_sound(&rt,"capture_enabled");queue_sound(&rt,"wifi_connected");assert(rt.sound_in==before+3);rt.sound_out=rt.sound_in;
 rt.prompt_reserved=rt.prompt_fired=1;rt.hold_started=1;rt.camera_fault=0;rt.keys=5;queue_sound(&rt,"capture_start");
 disable_gate(&rt);assert(!rt.prompt_reserved);assert(!rt.prompt_fired);assert(rt.release_required);assert(rt.sound_in-rt.sound_out==1);
 assert(!strcmp(rt.sounds[rt.sound_out++%8],"capture_disabled"));
 rt.gate=1;rt.lease=100000;COPY(rt.capture_id,"new-round");assert(!capture_gesture(&rt,6000));assert(!rt.prompt_fired);
 rt.release_required=0;rt.prompt_fired=1;rt.keys=0;COPY(rt.active_sound,"capture_done");rt.audio.pid=123;
 assert(!capture_gesture(&rt,6001));assert(rt.audio.pid==123);rt.audio.pid=0;
 rt.keys=5;rt.hold_started=1000;assert(!capture_gesture(&rt,8000));assert(rt.speech_resume==8200);
 assert(!capture_gesture(&rt,8200));rt.sound_out=rt.sound_in;assert(capture_gesture(&rt,8201));
 puts("PASS: 800ms handover, 1000ms single prompt, 1200ms capture, cancellation, gate guards and restored event prompts");
 return 0;
}
