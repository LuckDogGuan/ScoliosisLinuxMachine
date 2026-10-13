#define camera_stop injected_stop
#define camera_status injected_status
#define http_v3_status injected_http_status
#include "../src/app/runtime_v3.c"
#undef camera_stop
#undef camera_status
#undef http_v3_status
#include <assert.h>
int injected_stop(void) { return -1; }
int injected_status(int *status) { *status=1; return 0; }
int injected_http_status(const Config *config,const cJSON *event) { (void)config;(void)event;return 0; }
static void close_workers(Runtime *rt) {
    Worker *workers[]={&rt->record,&rt->recovery,&rt->notifier};
    for(size_t i=0;i<sizeof(workers)/sizeof(*workers);i++) if(workers[i]->pid) {
        pid_t pid=workers[i]->pid;abort_worker(workers[i]);while(waitpid(pid,NULL,0)<0 && errno==EINTR) {}
    }
    cJSON_Delete(rt->last_completed);
}
static int fail_result_check(Config *config,int eof) {
    Runtime rt={.config=config,.udp=-1,.gate=1,.lease=v3_now()+6000,.recording=1,.keys=5,.started_at=1791360000000};
    COPY(rt.current_id,eof ? "CAM-test-eof" : "CAM-test-failed");COPY(rt.current_gate,"gate-test");
    COPY(rt.completion_event,eof ? "event-eof" : "event-failed");COPY(rt.host,"host-test");COPY(rt.boot,"boot-test");
    int fd,branch=make_worker(&rt.record,&fd);assert(branch>=0);
    if(!branch) {
        if(!eof) {WorkerResult result={.type='C',.result=-1,.reason=2,.duration=3000};(void)send(fd,&result,sizeof(result),MSG_NOSIGNAL);}
        close(fd);_exit(0);
    }
    for(int i=0;i<100 && rt.record.pid;i++) {record_messages(&rt);usleep(1000);}
    assert(!rt.record.pid);assert(rt.last_completed);
    int safe=rt.camera_fault && !rt.gate;
    printf("%s: %s must block new capture until stopped camera is confirmed\n",safe ? "PASS" : "FAIL",eof ? "record-worker EOF" : "failed record result");
    close_workers(&rt);return safe ? 0 : 1;
}
static int audio_gate_check(Config *config) {
    Runtime rt={.config=config,.paired=1,.audio={.pid=999999}};
    COPY(rt.host,"host-test");COPY(rt.boot,"boot-test");COPY(rt.peer_ip,"127.0.0.1");
    rt.udp=socket(AF_INET,SOCK_DGRAM|SOCK_NONBLOCK,0);assert(rt.udp>=0);
    struct sockaddr_in address={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
    assert(!bind(rt.udp,(struct sockaddr*)&address,sizeof(address)));
    socklen_t size=sizeof(address);assert(!getsockname(rt.udp,(struct sockaddr*)&address,&size));
    int client=socket(AF_INET,SOCK_DGRAM,0);assert(client>=0);
    const char *request="{\"version\":3,\"op\":\"capture_enable\",\"nonce\":\"nonce-audio\",\"sequence\":1,\"device_id\":\"CAM-test\",\"host_id\":\"host-test\",\"boot_id\":\"boot-test\",\"gate_id\":\"gate-audio\",\"ttl_ms\":6000}";
    assert(sendto(client,request,strlen(request),0,(struct sockaddr*)&address,sizeof(address))==(ssize_t)strlen(request));
    struct pollfd arrived={.fd=rt.udp,.events=POLLIN};assert(poll(&arrived,1,1000)>0);
    struct timeval timeout={.tv_sec=1};assert(!setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout)));
    receive_udp(&rt,v3_now());char reply[PACKET_SIZE+1];int length=recv(client,reply,PACKET_SIZE,0);assert(length>0);reply[length]=0;
    cJSON *json=cJSON_Parse(reply);assert(json);int safe=rt.gate && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json,"ok"));
    printf("%s: active audio operation must not block capture_enable\n",safe ? "PASS" : "FAIL");
    cJSON_Delete(json);close(client);close(rt.udp);return safe ? 0 : 1;
}
static int trace_lock_check(void) {
    int notify[2];assert(!pipe(notify));pid_t child=fork();assert(child>=0);
    if(!child) {
        close(notify[0]);int lock=open(STATE_DIR "/trace.lock",O_CREAT|O_WRONLY,0600);assert(lock>=0);
        struct flock guard={.l_type=F_WRLCK,.l_whence=SEEK_SET};assert(!fcntl(lock,F_SETLK,&guard));
        assert(write(notify[1],"L",1)==1);for(;;)pause();
    }
    close(notify[1]);char ready;assert(read(notify[0],&ready,1)==1);close(notify[0]);
    uint64_t before=v3_now();instrument_trace("lock_contention","","","","");uint64_t elapsed=v3_now()-before;
    kill(child,SIGKILL);assert(waitpid(child,NULL,0)==child);assert(elapsed<100);
    puts("PASS: trace lock contention never stalls the coordinator");return 0;
}
int main(void) {
    assert(!mkdir(STATE_DIR,0700));assert(!mkdir(EVENTS_DIR,0700));
    assert(!mkdir(INSTRUMENT_ROOT "/capture",0700));assert(!mkdir(PENDING_DIR,0700));
    Config config={.width=1280,.height=1080,.fps=30};COPY(config.device_id,"CAM-test");
    int failures=0;failures+=fail_result_check(&config,0);failures+=fail_result_check(&config,1);
    failures+=audio_gate_check(&config);failures+=trace_lock_check();return failures ? 1 : 0;
}
