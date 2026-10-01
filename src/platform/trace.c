#include "v3.h"
#include "platform.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
void instrument_trace(const char *event, const char *capture, const char *gate, const char *boot, const char *detail) {
    int lock = open(STATE_DIR "/trace.lock", O_CREAT|O_WRONLY|O_CLOEXEC, 0600);
    if (lock < 0) return;
    struct flock guard = {.l_type=F_WRLCK, .l_whence=SEEK_SET};
    if (fcntl(lock, F_SETLKW, &guard)) { close(lock); return; }
    struct stat st;
    if (!stat(STATE_DIR "/runtime.jsonl", &st) && st.st_size > 4*1024*1024) {
        (void)rename(STATE_DIR "/runtime.jsonl.1", STATE_DIR "/runtime.jsonl.2");
        (void)rename(STATE_DIR "/runtime.jsonl", STATE_DIR "/runtime.jsonl.1");
    }
    cJSON *row = cJSON_CreateObject();
    if (row) {
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
        char buffer[4096];
        if (cJSON_PrintPreallocated(row,buffer,sizeof(buffer)-2,0)) {
            size_t n=strlen(buffer); buffer[n++]='\n';
            int fd=open(STATE_DIR "/runtime.jsonl",O_CREAT|O_APPEND|O_WRONLY|O_CLOEXEC,0600);
            if(fd>=0) { ssize_t written = write(fd,buffer,n); (void)written; close(fd); }
        }
        cJSON_Delete(row);
    }
    guard.l_type=F_UNLCK; (void)fcntl(lock,F_SETLK,&guard); close(lock);
}

void instrument_trace_protocol(const cJSON *request, const cJSON *reply, const char *boot, const char *endpoint) {
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
