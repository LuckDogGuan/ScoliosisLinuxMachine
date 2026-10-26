#include "evidence.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#define LIMIT (512*1024)
static const char *text(const cJSON *o,const char *k) {
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,k);return cJSON_IsString(v)?v->valuestring:"";
}
static void scan(cJSON *events,const char *path,const char *capture,const char *boot,double utc,size_t *used,int *partial) {
    FILE *f=fopen(path,"r");if(!f)return;
    struct stat st;if(fstat(fileno(f),&st) || !S_ISREG(st.st_mode)) {fclose(f);return;}
    /* Read a bounded tail; old oversized legacy files cannot monopolize a worker. */
    if(st.st_size>3*1024*1024) {(void)fseek(f,-3*1024*1024,SEEK_END);*partial=1;}
    char line[2048];
    while(fgets(line,sizeof(line),f)) {
        size_t n=strlen(line);
        if(!n || line[n-1]!='\n') {int ch;while((ch=fgetc(f))!=EOF && ch!='\n'){}*partial=1;continue;}
        cJSON *row=cJSON_Parse(line);if(!row){*partial=1;continue;}
        const cJSON *when=cJSON_GetObjectItemCaseSensitive(row,"utc_ms");
        int same_capture=capture[0] && !strcmp(text(row,"capture_id"),capture);
        int same_boot=!boot[0] || !strcmp(text(row,"boot_id"),boot);
        int in_window=cJSON_IsNumber(when) && when->valuedouble>=utc-60000 && when->valuedouble<=utc+15000;
        if((same_capture || (same_boot && in_window)) && *used+n<LIMIT-16384) {
            cJSON_AddItemToArray(events,row);*used+=n;
        } else {if((same_capture || in_window) && *used+n>=LIMIT-16384)*partial=1;cJSON_Delete(row);}
    }
    fclose(f);
}
static cJSON *read_body(const char *path) {
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return NULL;
    struct stat st;if(fstat(fd,&st) || st.st_size<=0 || st.st_size>LIMIT){close(fd);return NULL;}
    char *bytes=malloc((size_t)st.st_size+1);if(!bytes){close(fd);return NULL;}
    size_t at=0;while(at<(size_t)st.st_size) {
        ssize_t n=read(fd,bytes+at,(size_t)st.st_size-at);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)break;
        at+=(size_t)n;
    }
    close(fd);bytes[at]=0;cJSON *body=at==(size_t)st.st_size?cJSON_Parse(bytes):NULL;free(bytes);return body;
}
cJSON *evidence_build(const Config *config,const cJSON *snapshot,const char *fault,const char *capture,const char *boot,double utc) {
    for(int i=0;i<8;i++) {
        char path[256];snprintf(path,sizeof(path),INSTRUMENT_LOG_DIR "/fault-%d.json",i);
        struct stat st;if(lstat(path,&st) || time(NULL)-st.st_mtime>7*86400)continue;
        cJSON *saved=read_body(path);if(!saved)continue;
        const cJSON *when=cJSON_GetObjectItemCaseSensitive(saved,"utc_ms");
        int related=(capture[0] && !strcmp(text(saved,"capture_id"),capture)) ||
          (cJSON_IsNumber(when) && when->valuedouble>utc-60000 && when->valuedouble<utc+15000 && !strcmp(text(saved,"boot_id"),boot));
        if(related) {
            cJSON_ReplaceItemInObjectCaseSensitive(saved,"fault_id",cJSON_CreateString(fault));
            cJSON_AddBoolToObject(saved,"historical",1);return saved;
        }
        cJSON_Delete(saved);
    }
    cJSON *body=cJSON_CreateObject();if(!body)return NULL;
    cJSON_AddNumberToObject(body,"utc_ms",utc);
    cJSON_AddStringToObject(body,"schema","instrument-evidence-v1");
    cJSON_AddStringToObject(body,"fault_id",fault);cJSON_AddStringToObject(body,"device_id",config->device_id);
    cJSON_AddStringToObject(body,"capture_id",capture);cJSON_AddStringToObject(body,"boot_id",boot);
    cJSON_AddStringToObject(body,"version",INSTRUMENT_VERSION);cJSON_AddStringToObject(body,"build",INSTRUMENT_BUILD);
    cJSON_AddStringToObject(body,"time_basis","utc_window_and_capture_id_clock_may_change");
    cJSON_AddItemToObject(body,"snapshot",cJSON_Duplicate(snapshot,1));
    cJSON *events=cJSON_AddArrayToObject(body,"events");size_t used=0;int partial=0;
    const char *files[]={INSTRUMENT_LOG_DIR "/runtime.jsonl.2",INSTRUMENT_LOG_DIR "/runtime.jsonl.1",INSTRUMENT_LOG_DIR "/runtime.jsonl",STATE_DIR "/runtime.jsonl.2",STATE_DIR "/runtime.jsonl.1",STATE_DIR "/runtime.jsonl"};
    for(size_t i=0;i<sizeof(files)/sizeof(files[0]);i++)scan(events,files[i],capture,boot,utc,&used,&partial);
    if(!cJSON_GetArraySize(events))partial=1;
    cJSON_AddBoolToObject(body,"partial",partial);
    cJSON_AddBoolToObject(body,"window_incomplete",(double)wall_ms()<utc+15000);
    return body;
}
static int save_body(const char *path,const cJSON *body) {
    char *json=cJSON_PrintUnformatted(body);if(!json)return -1;
    size_t length=strlen(json);
    if(length>LIMIT){cJSON_free(json);return -1;}
    char temporary[300];snprintf(temporary,sizeof(temporary),"%s.tmp",path);
    int fd=open(temporary,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0){cJSON_free(json);return -1;}
    size_t at=0;while(at<length) {
        ssize_t n=write(fd,json+at,length-at);
        if(n<0 && errno==EINTR)continue;
        if(n<=0){close(fd);cJSON_free(json);unlink(temporary);return -1;}
        at+=(size_t)n;
    }
    int result=fsync(fd);if(close(fd))result=-1;cJSON_free(json);
    if(!result)result=rename(temporary,path);
    if(result)unlink(temporary);
    int dir=open(INSTRUMENT_LOG_DIR,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(dir>=0){if(fsync(dir))result=-1;close(dir);}
    return result;
}
int evidence_store(const cJSON *body) {
    (void)mkdir(INSTRUMENT_LOG_DIR,0700);
    char path[256],old[256];struct stat st;
    for(int i=7;i>=0;i--) {
        snprintf(path,sizeof(path),INSTRUMENT_LOG_DIR "/fault-%d.json",i);
        if(!lstat(path,&st) && (time(NULL)-st.st_mtime>7*86400 || i==7)) (void)unlink(path);
        if(i<7) {snprintf(old,sizeof(old),INSTRUMENT_LOG_DIR "/fault-%d.json",i+1);(void)rename(path,old);}
    }
    return save_body(INSTRUMENT_LOG_DIR "/fault-0.json",body);
}
int evidence_send(const Config *config,const cJSON *body,const char *token) {
    char path[]=INSTRUMENT_LOG_DIR "/upload-XXXXXX";int fd=mkstemp(path);if(fd<0)return -1;
    char *json=cJSON_PrintUnformatted(body);if(!json){close(fd);unlink(path);return -1;}
    size_t n=strlen(json);ssize_t done=write(fd,json,n);cJSON_free(json);close(fd);
    if(n>LIMIT || done!=(ssize_t)n){unlink(path);return -1;}
    char url[320],device[160],ticket[180],post[320];
    snprintf(url,sizeof(url),"%s/api/v3/diagnostics/evidence",config->pc_base_url);
    snprintf(device,sizeof(device),"X-Device-Id: %s",config->device_id);
    snprintf(ticket,sizeof(ticket),"X-Evidence-Token: %s",token);
    snprintf(post,sizeof(post),"--post-file=%s",path);
    char *args[]={"wget","-q","-T","15","-O","/dev/null","--header","Content-Type: application/json","--header",device,"--header",ticket,post,url,NULL};
    int result=run_command_timeout(args,20000);unlink(path);return result;
}
