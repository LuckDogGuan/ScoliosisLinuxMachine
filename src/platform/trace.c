#include "v3.h"
#include "platform.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/file.h>

#define LOG_DIR INSTRUMENT_ROOT "/logs"
static unsigned long trace_sequence, trace_dropped;
static pid_t trace_pid;
static char trace_run[160];
static unsigned long trace_sampled;
static struct {unsigned long key,count;uint64_t when;} repeats[16];

static uint64_t trace_detail_until;
void instrument_trace_detail(uint64_t until) {trace_detail_until=until;}


void instrument_trace(const char *event, const char *capture, const char *gate, const char *boot, const char *detail) {
    unsigned long key=5381;
    for(const unsigned char *p=(const unsigned char*)event;*p;p++)key=key*33+*p;
    if(capture)for(const unsigned char *p=(const unsigned char*)capture;*p;p++)key=key*33+*p;
    if(detail)for(const unsigned char *p=(const unsigned char*)detail;*p && p<(const unsigned char*)detail+2048;p++)key=key*33+*p;
    unsigned slot=(unsigned)(key%16);uint64_t mono=v3_now();
    unsigned long repeated=0;
    if(repeats[slot].key==key && mono-repeats[slot].when<1000) {
        repeats[slot].count++;trace_sampled++;return;
    }
    if(repeats[slot].key==key)repeated=repeats[slot].count;
    repeats[slot].key=key;repeats[slot].when=mono;repeats[slot].count=0;
    (void)mkdir(LOG_DIR,0700);
    int lock = open(LOG_DIR "/trace.lock", O_CREAT|O_WRONLY|O_CLOEXEC, 0600);
    if (lock < 0) { trace_dropped++; return; }
    struct flock guard = {.l_type=F_WRLCK, .l_whence=SEEK_SET};
    if (fcntl(lock, F_SETLK, &guard)) { trace_dropped++; close(lock); return; }
    struct stat st;
    if (!stat(LOG_DIR "/runtime.jsonl", &st) && st.st_size > 3*1024*1024) {
        (void)rename(LOG_DIR "/runtime.jsonl.1", LOG_DIR "/runtime.jsonl.2");
        (void)rename(LOG_DIR "/runtime.jsonl", LOG_DIR "/runtime.jsonl.1");
    }
    cJSON *row = cJSON_CreateObject();
    if (row) {
        if(trace_pid!=getpid()) {
            trace_pid=getpid();trace_sequence=0;
            char system_boot[64]="unknown";FILE *identity=fopen("/proc/sys/kernel/random/boot_id","r");
            if(identity){if(!fgets(system_boot,sizeof(system_boot),identity))strcpy(system_boot,"unknown");fclose(identity);}
            system_boot[strcspn(system_boot,"\r\n")]=0;
            snprintf(trace_run,sizeof(trace_run),"R-%s-%ld-%llu",system_boot,(long)getpid(),(unsigned long long)v3_now());
        }
        const char *run=trace_run;
        cJSON_AddStringToObject(row,"run_id",run);
        cJSON_AddNumberToObject(row,"sequence",(double)++trace_sequence);
        cJSON_AddNumberToObject(row,"repeat_count",(double)repeated+1);
        cJSON_AddNumberToObject(row,"sampled",(double)trace_sampled);
        cJSON_AddNumberToObject(row,"dropped",(double)trace_dropped);
        cJSON_AddStringToObject(row,"version",INSTRUMENT_VERSION);
        cJSON_AddNumberToObject(row,"monotonic_ms",(double)v3_now());
        struct timespec now; clock_gettime(CLOCK_REALTIME,&now);
        cJSON_AddNumberToObject(row,"utc_ms",(double)now.tv_sec*1000 + now.tv_nsec/1000000);
        cJSON_AddStringToObject(row,"event",event);
        cJSON_AddStringToObject(row,"capture_id",capture ? capture : "");
        cJSON_AddStringToObject(row,"gate_id",gate ? gate : "");
        cJSON_AddStringToObject(row,"boot_id",boot ? boot : "");
        cJSON_AddStringToObject(row,"detail",detail ? detail : "");
        if (detail && detail[0] == '{') {
            cJSON *fields = cJSON_Parse(detail);
            if (fields) {
                const cJSON *item = NULL;
                cJSON_ArrayForEach(item, fields) {
                    cJSON_DeleteItemFromObjectCaseSensitive(row, item->string);
                    cJSON_AddItemToObject(row, item->string, cJSON_Duplicate(item, 1));
                }
                cJSON_Delete(fields);
            }
        }
        char buffer[2048];
        if (!cJSON_PrintPreallocated(row,buffer,sizeof(buffer)-2,0)) {
            cJSON_DeleteItemFromObjectCaseSensitive(row,"detail");
            cJSON_AddBoolToObject(row,"truncated",1);
            /* Rebuild an envelope rather than emitting broken JSON. */
            if(!cJSON_PrintPreallocated(row,buffer,sizeof(buffer)-2,0)) {
                cJSON_Delete(row);row=cJSON_CreateObject();
                cJSON_AddStringToObject(row,"event",event);
                cJSON_AddStringToObject(row,"capture_id",capture?capture:"");
                cJSON_AddStringToObject(row,"boot_id",boot?boot:"");
                cJSON_AddStringToObject(row,"run_id",run);
                cJSON_AddNumberToObject(row,"sequence",(double)trace_sequence);
                cJSON_AddNumberToObject(row,"monotonic_ms",(double)v3_now());
                cJSON_AddNumberToObject(row,"utc_ms",(double)now.tv_sec*1000+now.tv_nsec/1000000);
                cJSON_AddBoolToObject(row,"truncated",1);
            }
        }
        if (cJSON_PrintPreallocated(row,buffer,sizeof(buffer)-2,0)) {
            size_t n=strlen(buffer); buffer[n++]='\n';
            int fd=open(LOG_DIR "/runtime.jsonl",O_CREAT|O_APPEND|O_WRONLY|O_CLOEXEC,0600);
            if(fd>=0) {
                ssize_t written=write(fd,buffer,n);
                if(written!=(ssize_t)n) trace_dropped++;
                close(fd);
            } else trace_dropped++;
            int failed=strstr(event,"failed") || strstr(event,"error") || strstr(event,"watchdog") ||
                (detail && (strstr(detail,"\"result\":\"failed\"") || strstr(detail,"\"ok\":false")));
            if(failed) {
                struct stat fault_st;
                if(stat(LOG_DIR "/fault-marker.json",&fault_st) || time(NULL)-fault_st.st_mtime>=60) {
                    char fault_id[128];snprintf(fault_id,sizeof(fault_id),"B-%.80s-%lu",trace_run,trace_sequence);
                    cJSON_AddStringToObject(row,"fault_id",fault_id);
                    (void)v3_save_json(LOG_DIR "/fault-marker.json",row);
                }
            }
        } else trace_dropped++;
        cJSON_Delete(row);
    }
    guard.l_type=F_UNLCK; (void)fcntl(lock,F_SETLK,&guard); close(lock);
}

void instrument_trace_protocol(const cJSON *request, const cJSON *reply, const char *boot, const char *endpoint) {
    const cJSON *operation=cJSON_GetObjectItemCaseSensitive(request,"op");
    const cJSON *ok=cJSON_GetObjectItemCaseSensitive(reply,"ok");
    if(cJSON_IsString(operation) && !strcmp(operation->valuestring,"hello_query") && cJSON_IsTrue(ok) && v3_now()>=trace_detail_until) return;
    cJSON *fields = cJSON_CreateObject(); if (!fields) return;
    const char *names[] = {"op", "device_id", "host_id", "gate_id", "capture_id", "event_id", "grant_id", "txn_id", "operation_id", "sequence", "nonce", "size_bytes", "sha256"};
    for (size_t i = 0; i < sizeof(names)/sizeof(names[0]); i++) {
        const cJSON *item = cJSON_GetObjectItemCaseSensitive(request,names[i]);
        if (item) cJSON_AddItemToObject(fields,names[i],cJSON_Duplicate(item,1));
    }
    cJSON_AddStringToObject(fields,"direction","pc_to_board");
    cJSON_AddStringToObject(fields,"source_endpoint",endpoint);
    cJSON_AddBoolToObject(fields,"ok",cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(reply,"ok")));
    const cJSON *error=cJSON_GetObjectItemCaseSensitive(reply,"error");
    if(error) cJSON_AddItemToObject(fields,"error",cJSON_Duplicate(error,1));
    char detail[3072];
    if(cJSON_PrintPreallocated(fields,detail,sizeof(detail),0)) instrument_trace("udp_exchange","","",boot,detail);
    cJSON_Delete(fields);
}
