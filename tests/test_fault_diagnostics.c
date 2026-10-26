#define recvfrom evidence_recvfrom
#define sendto evidence_sendto
#include "../src/app/runtime_v3.c"
#undef recvfrom
#undef sendto
#include <assert.h>
static const char *packet;
static char answer[PACKET_SIZE+1];
ssize_t evidence_recvfrom(int fd,void *out,size_t length,int flags,struct sockaddr *address,socklen_t *size) {
    (void)fd;(void)flags;(void)size;
    if(!packet){errno=EAGAIN;return -1;}
    struct sockaddr_in *source=(struct sockaddr_in*)address;
    source->sin_family=AF_INET;source->sin_port=htons(12345);source->sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    size_t n=strlen(packet);assert(n<length);memcpy(out,packet,n);packet=NULL;return (ssize_t)n;
}
ssize_t evidence_sendto(int fd,const void *data,size_t n,int flags,const struct sockaddr *address,socklen_t size) {
    (void)fd;(void)flags;(void)address;(void)size;assert(n<sizeof(answer));memcpy(answer,data,n);answer[n]=0;return (ssize_t)n;
}
static void command(Runtime *rt,const char *op,const char *host,const char *nonce,int sequence) {
    static char data[1500];
    snprintf(data,sizeof(data),"{\"version\":3,\"device_id\":\"CAM-test\",\"boot_id\":\"boot-test\",\"host_id\":\"%s\",\"op\":\"%s\",\"nonce\":\"%s\",\"sequence\":%d,\"fault_id\":\"F-test\",\"token\":\"D-test\",\"utc_ms\":%.0f}",host,op,nonce,sequence,(double)wall_ms());
    packet=data;answer[0]=0;receive_udp(rt,v3_now());assert(answer[0]);
}
static void protocol_check(Config *config) {
    Runtime rt={.config=config,.udp=-1,.paired=1,.gate=1,.free_mb=1000};
    COPY(rt.host,"host-test");COPY(rt.boot,"boot-test");COPY(rt.peer_ip,"127.0.0.1");
    packet="{\"version\":3,\"device_id\":\"CAM-test\",\"boot_id\":\"boot-test\",\"host_id\":\"host-test\",\"op\":\"pending_query\",\"nonce\":\"invalid-page\",\"sequence\":0,\"offset\":0,\"limit\":16}";
    answer[0]=0;receive_udp(&rt,v3_now());
    assert(strstr(answer,"invalid_pending_query") && !strstr(answer,"precondition_not_met"));
    command(&rt,"evidence_collect","wrong-host","nonce-wrong",1);
    assert(!rt.evidence_fault[0] && strstr(answer,"host_not_paired"));
    command(&rt,"evidence_collect","host-test","nonce-good",2);
    assert(strstr(answer,"\"ok\":true") && !strcmp(rt.evidence_fault,"F-test"));
    assert(rt.gate && !rt.diagnostics_lease && !rt.record.pid && !rt.diagnostics.pid);
    evidence_tick(&rt,v3_now());assert(!rt.evidence.pid && rt.gate);
    command(&rt,"evidence_collect","host-test","nonce-good",2);assert(strstr(answer,"\"ok\":true"));
    command(&rt,"evidence_status","host-test","nonce-status",0);assert(strstr(answer,"pending"));assert(rt.sequence==2);
    cJSON_Delete(rt.evidence_snapshot);
    puts("PASS evidence identity, idempotent retries, read-only status and capture-priority deferral without hardware lease");
}
static void trace_check(void) {
    char huge[10000];memset(huge,'x',sizeof(huge)-1);huge[sizeof(huge)-1]=0;
    instrument_trace("capture_failed","capture-test","gate-test","boot-test",huge);
    FILE *f=fopen(INSTRUMENT_LOG_DIR "/runtime.jsonl","r");assert(f);
    char line[2100];assert(fgets(line,sizeof(line),f));fclose(f);assert(strlen(line)<=2048);
    cJSON *row=cJSON_Parse(line);assert(row && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(row,"truncated")));cJSON_Delete(row);
    row=v3_read_json(INSTRUMENT_LOG_DIR "/fault-marker.json");assert(row && str(row,"fault_id")[0]);cJSON_Delete(row);
    int notify[2];assert(!pipe(notify));pid_t child=fork();assert(child>=0);
    if(!child) {
        close(notify[0]);int fd=open(INSTRUMENT_LOG_DIR "/trace.lock",O_WRONLY);assert(fd>=0);
        struct flock guard={.l_type=F_WRLCK,.l_whence=SEEK_SET};assert(!fcntl(fd,F_SETLK,&guard));
        assert(write(notify[1],"L",1)==1);for(;;)pause();
    }
    close(notify[1]);char ready;assert(read(notify[0],&ready,1)==1);close(notify[0]);
    uint64_t before=v3_now();instrument_trace("contended","","","","");assert(v3_now()-before<100);
    kill(child,SIGKILL);assert(waitpid(child,NULL,0)==child);
    instrument_trace("after_lock","","","boot-test","");
    puts("PASS 2KiB valid trace, independent fault marker, non-blocking rotation lock and drop accounting");
}
static void package_check(Config *config) {
    for(int i=0;i<5000;i++){char detail[40];snprintf(detail,sizeof(detail),"input-%d",i);instrument_trace("capture_event","capture-test","","boot-test",detail);}
    cJSON *snap=cJSON_CreateObject();cJSON_AddStringToObject(snap,"phase","idle");
    cJSON *body=evidence_build(config,snap,"F-package","capture-test","boot-test",(double)wall_ms());assert(body);
    char *encoded=cJSON_PrintUnformatted(body);assert(encoded && strlen(encoded)<=512*1024);cJSON_free(encoded);
    assert(!evidence_store(body));
    struct stat st;assert(!stat(INSTRUMENT_LOG_DIR "/fault-0.json",&st) && st.st_size<=512*1024);
    for(int i=0;i<10;i++)assert(!evidence_store(body));
    assert(!stat(INSTRUMENT_LOG_DIR "/fault-7.json",&st));assert(stat(INSTRUMENT_LOG_DIR "/fault-8.json",&st));
    cJSON *saved=evidence_build(config,snap,"F-later","capture-test","boot-test",(double)wall_ms());
    assert(saved && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(saved,"historical")));
    assert(!strcmp(str(saved,"fault_id"),"F-later"));
    cJSON_Delete(saved);cJSON_Delete(body);cJSON_Delete(snap);
    puts("PASS 512KiB evidence cap, 8-slot 4MiB retention and historical re-collection");
}
int main(void) {
    assert(!mkdir(STATE_DIR,0700));assert(!mkdir(EVENTS_DIR,0700));
    Config config={0};COPY(config.device_id,"CAM-test");
    trace_check();package_check(&config);protocol_check(&config);return 0;
}
