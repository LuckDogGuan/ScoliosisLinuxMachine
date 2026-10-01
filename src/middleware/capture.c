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
    return found == 1 ? 0 : -1;
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

static int record_impl(const Config *config, const char *capture_id, int control_fd, int *duration_ms, int *reason) {
    CameraSettings previous;
    if (camera_get_settings(&previous)) return -1;
    char names[MAX_EXISTING][256];
    int count = 0;
    if (snapshot(names, &count) || collision_near_now()) return -1;
    CameraSettings target;
    if (camera_capture_settings(config->fps,config->bitrate_kbps,config->gop,&target)) return -1;
    int started = 0, ok = 0;
    if (duration_ms) *duration_ms = 0;
    if (reason) *reason = 2;
    uint64_t started_ms = 0;
    if (!camera_set_settings(&target)) {
        int allowed = 1;
        if (control_fd >= 0) {
            char message = 'P';
            allowed = write(control_fd, &message, 1) == 1 && read(control_fd, &message, 1) == 1 && message == 'G';
            struct pollfd ready = {.fd = control_fd, .events = POLLIN};
            if (allowed && poll(&ready, 1, 0) > 0) allowed = 0;
        }
        if (!allowed) goto restore;
        instrument_trace("record_rpc_start_request", capture_id, "", "", "");
        if (camera_start()) goto restore;
        instrument_trace("record_rpc_start_return", capture_id, "", "", "success");
        started_ms = v3_now();
        started = 1;
        if (control_fd < 0) {
            struct timespec delay = {.tv_sec = config->duration_ms / 1000,
                                     .tv_nsec = (config->duration_ms % 1000) * 1000000L};
            while (nanosleep(&delay, &delay) && errno == EINTR) {}
        } else {
            char message = 'S';
            if (write(control_fd, &message, 1) != 1) goto stop_record;
            struct pollfd input = {.fd = control_fd, .events = POLLIN};
            for (;;) {
                uint64_t elapsed = v3_now() - started_ms;
                if (elapsed >= 60000) { if (reason) *reason = 1; break; }
                if (storage_free_mb() < 16) { if (reason) *reason = 2; break; }
                int wait = (int)(60000 - elapsed);
                if (wait > 500) wait = 500;
                int ready = poll(&input, 1, wait);
                if (ready < 0 && errno == EINTR) continue;
                if (ready < 0) break;
                if (ready > 0) { if (reason) *reason = 0; break; }
            }
        }
stop_record:;
        if (duration_ms) *duration_ms = (int)(v3_now() - started_ms);
        instrument_trace("record_rpc_stop_request", capture_id, "", "", "");
        ok = camera_stop() == 0;
        instrument_trace("record_rpc_stop_return", capture_id, "", "", ok ? "success" : "failed");
        if (!ok) {
            syslog(LOG_ERR, "record stop failed; retrying once");
            instrument_trace("record_rpc_stop_request", capture_id, "", "", "");
        ok = camera_stop() == 0;
        instrument_trace("record_rpc_stop_return", capture_id, "", "", ok ? "success" : "failed");
        }
        if (!ok) {
            int status = -1;
            if (!camera_status(&status) && status == 0) ok = 1;
            else {
                syslog(LOG_CRIT, "record stop unresolved (status %d); manual board check required", status);
            }
        }
    }
restore:;
    int restore_settings = camera_set_settings(&previous);
    if (restore_settings) syslog(LOG_ERR,"preview parameter restoration failed");
    if (!started || !ok || restore_settings) return -1;
    char source[384], temp[384], final[384];
    if (find_new(names, count, source, sizeof(source))) {
        syslog(LOG_ERR, "recorded MP4 count ambiguous; original files retained in video directory");
        return -1;
    }
    if (valid_mp4(source)) {
        syslog(LOG_ERR, "recorded MP4 failed basic validation: %s", source);
        return -1;
    }
    int n = snprintf(temp, sizeof(temp), "%s/%s.tmp", WRITING_DIR, capture_id);
    if (n < 0 || (size_t)n >= sizeof(temp)) return -1;
    n = snprintf(final, sizeof(final), "%s/%s.mp4", PENDING_DIR, capture_id);
    if (n < 0 || (size_t)n >= sizeof(final) || !access(temp, F_OK) || !access(final, F_OK)) return -1;
    if (rename(source, temp) || rename(temp, final)) return -1;
    int file = open(final, O_RDONLY);
    if (file < 0) return -1;
    int synced = fsync(file);
    close(file);
    if (synced) return -1;
    int directory = open(PENDING_DIR, O_RDONLY | O_DIRECTORY);
    if (directory < 0) return -1;
    synced = fsync(directory);
    close(directory);
    if (synced) return -1;
    instrument_trace("file_finalized", capture_id, "", "", final);
    return control_fd >= 0 ? 0 : storage_write_metadata(config, capture_id);
}

int capture_record(const Config *config, const char *capture_id) {
    return record_impl(config, capture_id, -1, NULL, NULL);
}
int capture_controlled(const Config *config, const char *id, int control_fd, int *duration_ms, int *reason) {
    return record_impl(config, id, control_fd, duration_ms, reason);
}
