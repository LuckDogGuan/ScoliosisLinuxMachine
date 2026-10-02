#include "../src/app/runtime_v3.c"
#include <assert.h>
int main(void) {
    assert(!mkdir(INSTRUMENT_ROOT,0700) || errno==EEXIST);
    assert(!mkdir(STATE_DIR,0700) || errno==EEXIST);
    assert(!mkdir(EVENTS_DIR,0700) || errno==EEXIST);
    assert(!mkdir(INSTRUMENT_ROOT "/capture",0700) || errno==EEXIST);
    assert(!mkdir(PENDING_DIR,0700) || errno==EEXIST);
    const char *file=PENDING_DIR "/CAM-test.mp4";
    FILE *f=fopen(file,"wb"); assert(f); assert(fwrite("proof-video",1,11,f)==11); fclose(f);
    char hash[65]; long long size; assert(!digest(file,hash,&size)); assert(size==11);
    assert(discard_file("CAM-test","discard-CAM-test",size,"0000000000000000000000000000000000000000000000000000000000000000"));
    assert(!access(file,F_OK));
    assert(discard_file("CAM-test","discard-CAM-test",size+1,hash)); assert(!access(file,F_OK));
    Runtime rt={.record={.pid=123},.udp=-1}; COPY(rt.current_id,"CAM-test");
    cJSON *request=cJSON_CreateObject(), *reply=cJSON_CreateObject();
    cJSON_AddStringToObject(request,"capture_id","CAM-test"); cJSON_AddStringToObject(request,"txn_id","discard-CAM-test");
    cJSON_AddStringToObject(request,"sha256",hash); cJSON_AddNumberToObject(request,"size_bytes",size);
    assert(!begin_discard(&rt,request,reply)); assert(!strcmp(str(reply,"error"),"capture_busy"));
    assert(!access(file,F_OK)); cJSON_Delete(request); cJSON_Delete(reply);
    assert(!discard_file("CAM-test","discard-CAM-test",size,hash)); assert(access(file,F_OK));
    assert(!discard_file("CAM-test","discard-CAM-test",size,hash));
    assert(discard_file("CAM-test","other-txn",size,hash));
    f=fopen(file,"wb"); assert(f); assert(fwrite("proof-video",1,11,f)==11); fclose(f);
    assert(discard_intended("CAM-test"));
    Item items[MAX_ITEMS]; int next; assert(pending_items(0,MAX_ITEMS,items,&next)==0);
    assert(!discard_file("CAM-test","discard-CAM-test",size,hash)); assert(access(file,F_OK));
    const char *other=PENDING_DIR "/CAM-live.mp4";
    f=fopen(other,"wb"); assert(f); assert(fwrite("proof-video",1,11,f)==11); fclose(f);
    int pipes[2]; assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pipes));
    pid_t upload=fork(); assert(upload>=0);
    if (!upload) { signal(SIGTERM,SIG_IGN); close(pipes[0]); for (;;) pause(); }
    close(pipes[1]);
    rt=(Runtime){.upload={.pid=upload,.fd=pipes[0]},.udp=-1,.gate=1}; COPY(rt.upload_id,"CAM-live");
    request=cJSON_CreateObject(); reply=cJSON_CreateObject();
    cJSON_AddStringToObject(request,"capture_id","CAM-live"); cJSON_AddStringToObject(request,"txn_id","discard-CAM-live");
    cJSON_AddStringToObject(request,"sha256",hash); cJSON_AddNumberToObject(request,"size_bytes",size);
    assert(!begin_discard(&rt,request,reply)); assert(!strcmp(str(reply,"error"),"discard_pending"));
    for (int i=0;i<200 && rt.discard.pid;i++) { usleep(10000); discard_messages(&rt); }
    assert(!rt.upload.pid && !rt.discard.pid && rt.gate==1); assert(access(other,F_OK));
    assert(begin_discard(&rt,request,reply)); cJSON_Delete(request); cJSON_Delete(reply);
    const char *recover=PENDING_DIR "/CAM-recover.mp4";
    f=fopen(recover,"wb"); assert(f); assert(fwrite("proof-video",1,11,f)==11); fclose(f);
    assert(!persist_discard("CAM-recover","discard-CAM-recover",size,hash)); assert(!access(recover,F_OK));
    resume_discards(); assert(access(recover,F_OK));
    puts("capture_discard: mismatch, busy, exact proof, duplicate receipt passed");
    return 0;
}
