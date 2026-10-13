#include "../src/app/runtime_v3.c"
#include <assert.h>
static cJSON *request(const char *action,const char *id,int ttl) {
 cJSON *r=cJSON_CreateObject();cJSON_AddStringToObject(r,"action",action);
 cJSON_AddStringToObject(r,"speech_id",id);cJSON_AddNumberToObject(r,"ttl_ms",ttl);return r;
}
static int focus(Runtime *rt,const char *action,const char *id,int ttl,uint64_t now) {
 cJSON *r=request(action,id,ttl);int result=speech_focus(rt,r,now);cJSON_Delete(r);return result;
}
int main(void) {
 Config config={.volume=10};COPY(config.device_id,"CAM-speech-test");
 uint64_t now=v3_now();
 Runtime rt={.config=&config,.udp=-1,.metrics_at=now,.last_maintenance=now,.event_retry=now+100000};
 COPY(rt.boot,"boot-test");COPY(rt.phase,"prepared");
 queue_sound(&rt,"capture_done");assert(!focus(&rt,"acquire","speech-test",6000,now));
 rt.sound_out=rt.sound_in;rt.audio.pid=123;
 assert(!focus(&rt,"acquire","speech-test",6000,now));rt.audio.pid=0;
 assert(!focus(&rt,"acquire","bad/id",6000,now));
 assert(!focus(&rt,"acquire","speech-test",6001,now));
 assert(focus(&rt,"acquire","speech-test",6000,now));
 assert(focus(&rt,"acquire","speech-test",6000,now+1));
 assert(!focus(&rt,"acquire","other",6000,now+1));
 queue_sound(&rt,"uploaded");maintenance(&rt,now+10);assert(!rt.audio.pid);
 assert(!focus(&rt,"renew","other",6000,now+20));
 assert(!focus(&rt,"release","other",0,now+20));
 assert(focus(&rt,"renew","speech-test",6000,now+2000));
 assert(rt.speech_lease==now+8000);
 assert(focus(&rt,"release","speech-test",0,now+3000));
 rt.metrics_at=rt.last_maintenance=now+3001;
 maintenance(&rt,now+3001);assert(!rt.audio.pid);assert(rt.speech_resume==now+3600);
 assert(!focus(&rt,"acquire","speech-expiring",6000,now+3002)); rt.sound_out=rt.sound_in; assert(focus(&rt,"acquire","speech-expiring",6000,now+3002));
 rt.metrics_at=rt.last_maintenance=now+9002;
 maintenance(&rt,now+9002);assert(!rt.audio.pid);assert(!rt.speech_lease);
 assert(rt.speech_resume==now+9602);
 assert(!focus(&rt,"renew","speech-expiring",6000,now+9003));
 assert(focus(&rt,"acquire","speech-new",6000,now+9004));
 assert(!focus(&rt,"release","speech-expiring",0,now+9005));
 assert(rt.speech_lease>now+9005);
 puts("PASS: prompt-first acquisition, queued upload deferral, token matching, renewal, release gap, expiry and stale release");
 return 0;
}
