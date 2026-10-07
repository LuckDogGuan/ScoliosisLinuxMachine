#include "../src/app/runtime_v3.c"
#include <assert.h>
int main(void) {
    assert(!mkdir(STATE_DIR,0700)); assert(!mkdir(EVENTS_DIR,0700));
    assert(!mkdir(INSTRUMENT_ROOT "/capture",0700)); assert(!mkdir(PENDING_DIR,0700));
    Config config={.width=1280,.height=1080,.fps=30}; strcpy(config.device_id,"CAM-test");
    int durations[]={1999,2000,2999,3000};
    for(unsigned i=0;i<sizeof(durations)/sizeof(*durations);i++) {
        Runtime rt={.config=&config,.started_at=1791360000000};
        snprintf(rt.current_id,sizeof(rt.current_id),"CAM-test-%d",durations[i]);
        COPY(rt.current_gate,"gate"); COPY(rt.completion_event,"event-test");
        WorkerResult result={.duration=durations[i],.reason=0}; completed(&rt,&result);
        char path[384]; snprintf(path,sizeof(path),"%s/CAM-test-%d.json",PENDING_DIR,durations[i]);
        cJSON *metadata=v3_read_json(path); assert(metadata);
        assert(!strcmp(str(metadata,"result"),durations[i]<3000?"short":"success"));
        char video[384]; snprintf(video,sizeof(video),"%s/CAM-test-%d.mp4",PENDING_DIR,durations[i]);
        FILE *file=fopen(video,"wb"); assert(file); assert(fwrite("video",1,5,file)==5); fclose(file);
        cJSON_AddStringToObject(metadata,"sha256","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        assert(!v3_save_json(path,metadata)); cJSON_Delete(metadata);
        cJSON *request=cJSON_CreateObject(), *reply=cJSON_CreateObject();
        char id[64]; snprintf(id,sizeof(id),"CAM-test-%d",durations[i]);
        cJSON_AddStringToObject(request,"capture_id",id); cJSON_AddStringToObject(request,"grant_id","grant-test");
        cJSON_AddNumberToObject(request,"ttl_ms",30000); cJSON_AddNumberToObject(request,"size_bytes",5);
        cJSON_AddStringToObject(request,"sha256","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        begin_upload(&rt,request,reply);
        assert((rt.upload.pid>0)==(durations[i]>=3000));
        if(rt.upload.pid) { assert(waitpid(rt.upload.pid,NULL,0)==rt.upload.pid); finish_worker(&rt.upload); }
        cJSON_Delete(request); cJSON_Delete(reply);
    }
    puts("PASS: actual completion at 1999/2000/2999/3000 ms");
}
