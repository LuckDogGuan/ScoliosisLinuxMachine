#define camera_stop check_camera_stop
#define camera_status check_camera_status
#define http_v3_status check_http_status
#include "../src/app/runtime_v3.c"
#undef camera_stop
#undef camera_status
#undef http_v3_status
#include <assert.h>
static int check_stop_result=0, check_status_result=0, check_camera_state=0;
int check_camera_stop(void) { return check_stop_result; }
int check_camera_status(int *status) { *status=check_camera_state; return check_status_result; }
int check_http_status(const Config *config,const cJSON *event) { (void)config;(void)event;return 0; }
static void dispose(Runtime *rt) {
    Worker *workers[]={&rt->record,&rt->recovery,&rt->notifier,&rt->upload};
    for(size_t i=0;i<sizeof(workers)/sizeof(*workers);i++) if(workers[i]->pid) {
        pid_t child=workers[i]->pid; abort_worker(workers[i]);
        while(waitpid(child,NULL,0)<0 && errno==EINTR) {}
    }
    while(waitpid(-1,NULL,WNOHANG)>0) {}
    cJSON_Delete(rt->last_completed);rt->last_completed=NULL;
}
static cJSON *hello(Runtime *rt,const char *host,uint64_t now) {
    cJSON *reply=cJSON_CreateObject();assert(reply);
    cJSON_AddStringToObject(reply,"host_id",host);hello_reply(rt,reply,now);return reply;
}
static void snapshot_check(Config *config) {
    Runtime rt={.config=config,.paired=1,.gate=1,.lease=11000,.recording_mono=5000,.started_at=1791360000000};
    COPY(rt.host,"host-test");COPY(rt.boot,"boot-test");
    COPY(rt.current_id,"CAM-test-snapshot");COPY(rt.current_gate,"gate-test");COPY(rt.phase,"recording");
    cJSON *reply=hello(&rt,"host-test",10000),*snapshot=cJSON_GetObjectItemCaseSensitive(reply,"capture_snapshot");
    assert(!strcmp(str(snapshot,"device_id"),"CAM-test"));assert(!strcmp(str(snapshot,"boot_id"),"boot-test"));
    assert(!strcmp(str(snapshot,"capture_id"),"CAM-test-snapshot"));assert(!strcmp(str(snapshot,"gate_id"),"gate-test"));
    assert(num(snapshot,"elapsed_ms")==5000);assert(num(reply,"lease_remaining_ms")==1000);cJSON_Delete(reply);
    rt.stopped_mono=9000;COPY(rt.phase,"finalizing");
    reply=hello(&rt,"host-test",14000);snapshot=cJSON_GetObjectItemCaseSensitive(reply,"capture_snapshot");
    assert(num(snapshot,"elapsed_ms")==4000);assert(num(reply,"lease_remaining_ms")==0);cJSON_Delete(reply);
    rt.last_completed=cJSON_CreateObject();cJSON_AddStringToObject(rt.last_completed,"event_id","event-test");
    cJSON_AddStringToObject(rt.last_completed,"host_id","host-test");
    reply=hello(&rt,"host-test",14000);assert(cJSON_GetObjectItemCaseSensitive(reply,"last_completed_event"));cJSON_Delete(reply);
    reply=hello(&rt,"host-other",14000);assert(!cJSON_GetObjectItemCaseSensitive(reply,"last_completed_event"));cJSON_Delete(reply);
    COPY(rt.host,"host-repaired");reply=hello(&rt,"host-repaired",14000);
    assert(!cJSON_GetObjectItemCaseSensitive(reply,"last_completed_event"));cJSON_Delete(reply);
    dispose(&rt);puts("PASS: snapshot identity, monotonic elapsed, finalizing freeze, completion host filter including re-pair");
}
static void watchdog_check(Config *config) {
    Runtime rt={.config=config,.udp=-1,.gate=1,.lease=v3_now()+6000,.recording=1,.keys=5};
    COPY(rt.host,"host-test");COPY(rt.boot,"boot-test");COPY(rt.current_id,"CAM-test-watchdog");
    COPY(rt.current_gate,"gate-test");COPY(rt.gate_id,"gate-test");COPY(rt.completion_event,"event-watchdog");
    rt.started_at=1791360000000;rt.recording_mono=v3_now()-5000;
    int fd,branch=make_worker(&rt.record,&fd);assert(branch>=0);
    if(!branch) { for(;;) pause(); }
    pid_t child=rt.record.pid;rt.record.started=v3_now()-120001;
    check_stop_result=0;check_camera_state=1;
    worker_watchdog(&rt,v3_now());assert(!rt.record.pid);assert(rt.camera_fault);assert(!rt.gate);
    assert(waitpid(child,NULL,0)==child);
    assert(rt.last_completed && !strcmp(str(rt.last_completed,"result"),"failed"));
    for(int i=0;i<100 && rt.recovery.pid;i++) {usleep(1000);worker_watchdog(&rt,v3_now());}
    assert(rt.camera_fault);assert(!rt.recovery.pid);
    check_camera_state=0;rt.recovery_retry=0;worker_watchdog(&rt,v3_now());
    for(int i=0;i<100 && rt.camera_fault;i++) {usleep(1000);worker_watchdog(&rt,v3_now());}
    assert(!rt.camera_fault);dispose(&rt);
    puts("PASS: hung recording watchdog, durable failed result, recovery requires stopped camera status");
}
static void process_check(void) {
    assert(!prctl(PR_SET_CHILD_SUBREAPER,1));
    char *const success[]={"sh","-c","exit 0",NULL};
    char *const failure[]={"sh","-c","exit 7",NULL};
    char *const hang[]={"sh","-c","sleep 10 & wait",NULL};
    char *const missing[]={"nonexistent-stability-test-command",NULL};
    assert(!run_command_timeout(success,1000));assert(run_command_timeout(failure,1000)==-1);
    assert(run_command_timeout(missing,1000)==-1);assert(run_command_timeout(success,0)==-1);
    uint64_t before=v3_now();assert(run_command_timeout(hang,100)==-1);assert(v3_now()-before<1000);
    int status;pid_t child;
    for(int i=0;i<100;i++) {while((child=waitpid(-1,&status,WNOHANG))>0) {} if(child<0 && errno==ECHILD)break;usleep(1000);}
    assert(child<0 && errno==ECHILD);
    puts("PASS: command success/failure/exec failure/zero timeout; timeout kills process group and direct child is reaped");
}
int main(void) {
    assert(!mkdir(STATE_DIR,0700));assert(!mkdir(EVENTS_DIR,0700));
    assert(!mkdir(INSTRUMENT_ROOT "/capture",0700));assert(!mkdir(PENDING_DIR,0700));
    Config config={.width=1280,.height=1080,.fps=30,.volume=10};COPY(config.device_id,"CAM-test");
    snapshot_check(&config);watchdog_check(&config);process_check();return 0;
}
