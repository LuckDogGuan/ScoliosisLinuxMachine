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

static int read_all(int fd, void *data, size_t count) {
    unsigned char *at = data;
    while (count) {
        ssize_t n = read(fd, at, count);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        at += n;
        count -= (size_t)n;
    }
    return 0;
}

static int write_all(int fd, const void *data, size_t count) {
    const unsigned char *at = data;
    while (count) {
        ssize_t n = write(fd, at, count);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        at += n;
        count -= (size_t)n;
    }
    return 0;
}

static int rpc_open(const char *name) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct timeval timeout = {.tv_sec = 4};
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    strcpy(address.sun_path, "/var/tmp/rkipc");
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) ||
        connect(fd, (struct sockaddr *)&address, sizeof(address))) { close(fd); return -1; }
    int32_t hello = -1, length = (int32_t)strlen(name) + 1;
    if (read_all(fd, &hello, sizeof(hello)) || hello ||
        write_all(fd, &length, sizeof(length)) || write_all(fd, name, (size_t)length)) {
        close(fd); return -1;
    }
    return fd;
}

static int rpc_simple(const char *name) {
    int fd = rpc_open(name);
    if (fd < 0) return -1;
    int32_t error = -1;
    int result = read_all(fd, &error, sizeof(error));
    close(fd);
    return result || error ? -1 : 0;
}

static int rpc_status(int *status) {
    int fd = rpc_open("rk_storage_record_statue_get");
    if (fd < 0) return -1;
    int32_t value = -1, error = -1;
    int result = read_all(fd, &value, sizeof(value));
    if (!result) result = read_all(fd, &error, sizeof(error));
    close(fd);
    if (result || error) return -1;
    *status = value;
    return 0;
}

static int rpc_int(const char *name, int *value, int set) {
    int fd = rpc_open(name);
    if (fd < 0) return -1;
    int32_t channel = 0, number = *value, error = -1;
    int result = write_all(fd, &channel, sizeof(channel));
    if (!result && set) result = write_all(fd, &number, sizeof(number));
    if (!result && !set) result = read_all(fd, &number, sizeof(number));
    if (!result) result = read_all(fd, &error, sizeof(error));
    close(fd);
    if (result || error) return -1;
    if (!set) *value = number;
    return 0;
}

static int rpc_rate(const char *name, char *rate, size_t size, int set) {
    int fd = rpc_open(name);
    if (fd < 0) return -1;
    int32_t channel = 0, length = (int32_t)strlen(rate) + 1, error = -1;
    int result = write_all(fd, &channel, sizeof(channel));
    if (!result && set) {
        result = write_all(fd, &length, sizeof(length));
        if (!result) result = write_all(fd, rate, (size_t)length);
    }
    if (!result && !set) {
        result = read_all(fd, &length, sizeof(length));
        if (!result && (length <= 0 || (size_t)length >= size)) result = -1;
        if (!result) result = read_all(fd, rate, (size_t)length);
        if (!result) rate[length] = 0;
    }
    if (!result) result = read_all(fd, &error, sizeof(error));
    close(fd);
    return result || error ? -1 : 0;
}

int camera_get_settings(CameraSettings *settings) {
    return rpc_rate("rk_video_get_frame_rate", settings->rate, sizeof(settings->rate), 0) ||
        rpc_int("rk_video_get_max_rate", &settings->bitrate, 0) || rpc_int("rk_video_get_gop", &settings->gop, 0) ? -1 : 0;
}
int camera_set_settings(const CameraSettings *settings) {
    CameraSettings current;
    if (camera_get_settings(&current)) return -1;
    CameraSettings target = *settings;
    if (strcmp(current.rate,target.rate) && rpc_rate("rk_video_set_frame_rate",target.rate,sizeof(target.rate),1)) return -1;
    if (current.bitrate != target.bitrate && rpc_int("rk_video_set_max_rate",&target.bitrate,1)) return -1;
    if (current.gop != target.gop && rpc_int("rk_video_set_gop",&target.gop,1)) return -1;
    return 0;
}
int camera_start(void) { return rpc_simple("rk_storage_record_start"); }
int camera_stop(void) { return rpc_simple("rk_storage_record_stop"); }
int camera_status(int *status) { return rpc_status(status); }

int camera_capture_settings(int fps, int bitrate, int gop, CameraSettings *settings) {
    /* Current Luckfox encoder has a fixed 60-Hz source. Preserve its verified 60/2 profile. */
    if (fps <= 0 || 60 % fps) return -1;
    snprintf(settings->rate,sizeof(settings->rate),"60/%d",60/fps);
    settings->bitrate=bitrate; settings->gop=gop; return 0;
}

/* Read only the new JPEG requested by this diagnostic operation. */
int camera_diagnostic_frame(const Config *config, const char *token) {
    char existing[MAX_EXISTING][128]; int count=0;
    DIR *dir=opendir(VIDEO_DIR); struct dirent *entry;
    if (!dir) { instrument_trace("diagnostics_camera", "", "", "", "video_dir_unavailable"); return -1; }
    while ((entry=readdir(dir))) {
        if (!strstr(entry->d_name,".jpeg")) continue;
        if(count>=MAX_EXISTING) {closedir(dir); return -1;}
        snprintf(existing[count++],128,"%.127s",entry->d_name);
    }
    closedir(dir);
    if (rpc_simple("rk_take_photo")) { instrument_trace("diagnostics_camera", "", "", "", "snapshot_rpc_failed"); return -1; }
    uint64_t deadline=v3_now()+2000;
    char path[384]={0}; long previous=-1;
    while(v3_now()<deadline) {
        dir=opendir(VIDEO_DIR); if(!dir) return -1;
        while((entry=readdir(dir))) {
            size_t length=strlen(entry->d_name);
            if(length<5 || strcmp(entry->d_name+length-5,".jpeg")) continue;
            int old=0; for(int i=0;i<count;i++) if(!strcmp(existing[i],entry->d_name)) {old=1; break;}
            if(!old) {snprintf(path,sizeof(path),"%s/%.127s",VIDEO_DIR,entry->d_name); break;}
        }
        closedir(dir);
        if(path[0]) {
            struct stat st; unsigned char end[2]; int fd=open(path,O_RDONLY|O_CLOEXEC);
            int complete=fd>=0 && !fstat(fd,&st) && st.st_size>=4 && st.st_size<=1048576 &&
                st.st_size==previous && lseek(fd,-2,SEEK_END)>=0 && read(fd,end,2)==2 && end[0]==255 && end[1]==217;
            if(fd>=0) close(fd);
            if(complete) {
                int result=http_diagnostic_frame(config,token,path); instrument_trace("diagnostics_camera", "", "", "", result ? "http_failed" : "http_sent"); (void)unlink(path); return result;
            }
            if(!stat(path,&st)) previous=st.st_size;
        }
        struct timespec pause={.tv_nsec=50000000}; nanosleep(&pause,NULL);
    }
    instrument_trace("diagnostics_camera", "", "", "", path[0] ? "jpeg_incomplete" : "jpeg_timeout");
    if(path[0]) (void)unlink(path);
    return -1;
}
