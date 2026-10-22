#include "v3.h"
#include "platform.h"
#include <poll.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#define MAX_EXISTING 512
#ifndef CAPTURE_FIRST_FRAME_TIMEOUT_MS
#define CAPTURE_FIRST_FRAME_TIMEOUT_MS 2000
#endif
#ifndef CAPTURE_FINALIZE_TIMEOUT_MS
#define CAPTURE_FINALIZE_TIMEOUT_MS 2000
#endif

static int is_mp4(const char *name) {
    size_t n = strlen(name);
    return n > 4 && !strcmp(name + n - 4, ".mp4");
}

static int snapshot(char names[MAX_EXISTING][256], int *count) {
    DIR *dir = opendir(VIDEO_DIR);
    if (!dir) return -1;
    struct dirent *entry;
    *count = 0;
    while ((entry = readdir(dir))) {
        if (!is_mp4(entry->d_name)) continue;
        if (*count == MAX_EXISTING) { closedir(dir); return -1; }
        strcpy(names[(*count)++], entry->d_name);
    }
    closedir(dir);
    return 0;
}

static int collision_near_now(void) {
    for (int offset = 0; offset < 3; offset++) {
        time_t when = time(NULL) + offset;
        struct tm calendar;
        if (!localtime_r(&when, &calendar)) return -1;
        char name[64], path[128];
        if (!strftime(name, sizeof(name), "%Y%m%d%H%M%S.mp4", &calendar)) return -1;
        snprintf(path, sizeof(path), "%s/%s", VIDEO_DIR, name);
        if (!access(path, F_OK)) return -1;
    }
    return 0;
}

static int find_new(char names[MAX_EXISTING][256], int count, char *path, size_t size) {
    DIR *dir = opendir(VIDEO_DIR);
    if (!dir) return -1;
    struct dirent *entry;
    int found = 0;
    while ((entry = readdir(dir))) {
        if (!is_mp4(entry->d_name)) continue;
        int old = 0;
        for (int i = 0; i < count; i++) if (!strcmp(names[i], entry->d_name)) old = 1;
        if (!old) {
            int n = snprintf(path, size, "%s/%s", VIDEO_DIR, entry->d_name);
            if (n < 0 || (size_t)n >= size) { found = -1; break; }
            found++;
        }
    }
    closedir(dir);
    return found;
}

static int valid_mp4(const char *path) {
    struct stat info;
    if (stat(path, &info) || !S_ISREG(info.st_mode) || info.st_size < 24) return -1;
    FILE *file = fopen(path, "rb");
    if (!file) return -1;
    off_t offset = 0;
    int ftyp = 0, moov = 0, mdat = 0, ok = 1;
    while (offset < info.st_size) {
        unsigned char box[16];
        if (fread(box, 1, 8, file) != 8) { ok = 0; break; }
        uint64_t size = ((uint64_t)box[0]<<24) | ((uint64_t)box[1]<<16) | ((uint64_t)box[2]<<8) | box[3];
        unsigned header = 8;
        if (size == 1) {
            if (fread(box + 8, 1, 8, file) != 8) { ok = 0; break; }
            size = 0;
            for (int i = 8; i < 16; i++) size = (size << 8) | box[i];
            header = 16;
        } else if (!size) size = (uint64_t)(info.st_size - offset);
        if (size < header || size > (uint64_t)(info.st_size - offset)) { ok = 0; break; }
        if (!memcmp(box + 4, "ftyp", 4)) ftyp = 1;
        if (!memcmp(box + 4, "moov", 4)) moov = size > header;
        if (!memcmp(box + 4, "mdat", 4)) mdat = size > header;
        offset += (off_t)size;
        if (fseeko(file, offset, SEEK_SET)) { ok = 0; break; }
    }
    fclose(file);
    return ok && ftyp && moov && mdat && offset == info.st_size ? 0 : -1;
}

static void file_check_trace(const char *id, const char *stage, const char *code, int candidates, const char *path) {
    cJSON *detail = cJSON_CreateObject();
    if (!detail) return;
    cJSON_AddStringToObject(detail,"stage",stage);
    cJSON_AddStringToObject(detail,"code",code);
    cJSON_AddNumberToObject(detail,"candidates",candidates);
    if (path && path[0]) cJSON_AddStringToObject(detail,"path",path);
    char json[768];
    if (cJSON_PrintPreallocated(detail,json,sizeof(json),0)) instrument_trace("record_file_check",id,"","",json);
    cJSON_Delete(detail);
}

static int stop_camera(const char *id) {
    for (int attempt=0; attempt<2; attempt++) {
        instrument_trace("record_rpc_stop_request",id,"","","");
        int result=camera_stop();
        instrument_trace("record_rpc_stop_return",id,"","",result ? "failed" : "success");
        if (!result) return 0;
    }
    int status=-1;
    if (!camera_status(&status) && status==0) return 0;
    syslog(LOG_CRIT,"record stop unresolved (status %d); manual board check required",status);
    return -1;
}

static int wait_record_file(char names[MAX_EXISTING][256], int count, char *path, size_t size,
                            const char *id, uint64_t since, int complete, int control_fd) {
    uint64_t deadline=v3_now()+(complete ? CAPTURE_FINALIZE_TIMEOUT_MS : CAPTURE_FIRST_FRAME_TIMEOUT_MS);
    off_t previous=-1;
    const char *code="file_not_created";
    int found=0;
    for (;;) {
        path[0]=0;
        found=find_new(names,count,path,size);
        if (found<0) {code="file_scan_failed";break;}
        if (found>1) {code="multiple_new_files";break;}
        if (!complete && control_fd>=0) {
            struct pollfd input={.fd=control_fd,.events=POLLIN};
            if (poll(&input,1,0)!=0) {code="cancelled_before_video";break;}
        }
        struct stat info;
        if (complete) {
            if (found==1 && !stat(path,&info) && info.st_size>=24) {
                if (info.st_size==previous && !valid_mp4(path)) {
                    file_check_trace(id,"finalize","ok",found,path);return 0;
                }
                previous=info.st_size;code="mp4_incomplete";
            }
        } else {
            int frames=camera_record_ready(since);
            if (frames<0) {code="video_write_failed";break;}
            if (frames>0 && found==1 && !stat(path,&info) && S_ISREG(info.st_mode) && info.st_size>=24) {
                file_check_trace(id,"start","ok",found,path);return 0;
            }
            code=frames>0 ? "file_not_created" : "no_video_frames";
        }
        if (v3_now()>=deadline) break;
        struct timespec pause={.tv_nsec=50000000};nanosleep(&pause,NULL);
    }
    file_check_trace(id,complete ? "finalize" : "start",code,found,path);
    syslog(LOG_ERR,"record file check failed: capture=%s stage=%s code=%s candidates=%d path=%s",
           id,complete ? "finalize" : "start",code,found,path);
    return -1;
}

static int record_impl(const Config *config, const char *capture_id, int control_fd, int *duration_ms, int *reason) {
    CameraSettings previous;
    if (camera_get_settings(&previous)) return -1;
    char names[MAX_EXISTING][256];
    int count=0;
    if (snapshot(names,&count) || collision_near_now()) return -1;
    CameraSettings target;
    if (camera_capture_settings(config->fps,config->bitrate_kbps,config->gop,&target)) return -1;
    if (config->width>0 && config->height>0)
        snprintf(target.resolution,sizeof(target.resolution),"%d*%d",config->width,config->height);
    int started=0, ok=0, ready=0;
    char source[384]={0}, temp[384], final[384];
    if (duration_ms) *duration_ms=0;
    if (reason) *reason=2;
    if (camera_set_settings(&target)) goto restore;
    if (control_fd>=0) {
        char message='P';
        int allowed=write(control_fd,&message,1)==1 && read(control_fd,&message,1)==1 && message=='G';
        struct pollfd input={.fd=control_fd,.events=POLLIN};
        if (!allowed || poll(&input,1,0)!=0) goto restore;
    }
    for (int attempt=0;attempt<2;attempt++) {
        uint64_t request=v3_now();
        instrument_trace("record_rpc_start_request",capture_id,"","","");
        int result=camera_start();
        instrument_trace("record_rpc_start_return",capture_id,"","",result ? "failed" : "success");
        if (result) goto restore;
        started=1;
        if (!wait_record_file(names,count,source,sizeof(source),capture_id,request,0,control_fd)) {ready=1;break;}
        ok=!stop_camera(capture_id);
        if (!ok || attempt==1) goto restore;
        struct pollfd input={.fd=control_fd,.events=POLLIN};
        /* A retry is safe only before recording was announced, with no candidate
         * to overwrite or misassign, and while the original button hold is live. */
        if ((control_fd>=0 && poll(&input,1,0)!=0) || find_new(names,count,source,sizeof(source))!=0) goto restore;
        int status=-1;
        if (camera_status(&status) || status!=0) goto restore;
        instrument_trace("record_start_retry",capture_id,"","","no_video_frames");
        if (camera_set_settings(&previous) || camera_set_settings(&target) || collision_near_now()) goto restore;
    }
    if (ready) {
        uint64_t started_ms=v3_now();
        if (control_fd<0) {
            struct timespec delay={.tv_sec=config->duration_ms/1000,.tv_nsec=(config->duration_ms%1000)*1000000L};
            while (nanosleep(&delay,&delay) && errno==EINTR) {}
        } else {
            char message='S';
            if (write(control_fd,&message,1)!=1) goto stop_record;
            struct pollfd input={.fd=control_fd,.events=POLLIN};
            for (;;) {
                uint64_t elapsed=v3_now()-started_ms;
                if (elapsed>=60000) {if(reason)*reason=1;break;}
                if (storage_free_mb()<16) {if(reason)*reason=2;break;}
                int wait=(int)(60000-elapsed);if(wait>500)wait=500;
                int arrived=poll(&input,1,wait);
                if (arrived<0 && errno==EINTR)continue;
                if (arrived<0)break;
                if (arrived>0) {if(reason)*reason=0;break;}
            }
        }
stop_record:;
        if (duration_ms)*duration_ms=(int)(v3_now()-started_ms);
        ok=!stop_camera(capture_id);
        /* Finalize before changing preview/encoder parameters. */
        if (ok && wait_record_file(names,count,source,sizeof(source),capture_id,0,1,-1))ok=0;
    }
restore:;
    int restored=camera_set_settings(&previous);
    if (restored)syslog(LOG_ERR,"preview parameter restoration failed");
    if (!started || !ready || !ok || restored)return -1;
    if (valid_mp4(source))return -1;
    int n=snprintf(temp,sizeof(temp),"%s/%s.tmp",WRITING_DIR,capture_id);
    if (n<0 || (size_t)n>=sizeof(temp))return -1;
    n=snprintf(final,sizeof(final),"%s/%s.mp4",PENDING_DIR,capture_id);
    if (n<0 || (size_t)n>=sizeof(final) || !access(temp,F_OK) || !access(final,F_OK))return -1;
    if (rename(source,temp) || rename(temp,final))return -1;
    int file=open(final,O_RDONLY);if(file<0)return -1;
    int synced=fsync(file);close(file);if(synced)return -1;
    int directory=open(PENDING_DIR,O_RDONLY|O_DIRECTORY);if(directory<0)return -1;
    synced=fsync(directory);close(directory);if(synced)return -1;
    instrument_trace("file_finalized",capture_id,"","",final);
    return control_fd>=0 ? 0 : storage_write_metadata(config,capture_id);
}

int capture_record(const Config *config, const char *capture_id) {
    return record_impl(config, capture_id, -1, NULL, NULL);
}
int capture_controlled(const Config *config, const char *id, int control_fd, int *duration_ms, int *reason) {
    return record_impl(config, id, control_fd, duration_ms, reason);
}
