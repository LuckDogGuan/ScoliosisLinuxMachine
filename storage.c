#include "app.h"
#include "cJSON.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

int storage_prepare(void) {
    if (mkdir("/userdata/capture", 0755) && errno != EEXIST) return -1;
    if (mkdir(PENDING_DIR, 0755) && errno != EEXIST) return -1;
    return 0;
}

long storage_free_mb(void) {
    struct statvfs info;
    if (statvfs("/userdata", &info)) return -1;
    return (long)((unsigned long long)info.f_bavail * info.f_frsize / (1024 * 1024));
}

int storage_capture_id(const Config *config, char *out, size_t size) {
    unsigned char random[4];
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -1;
    ssize_t count = read(fd, random, sizeof(random));
    close(fd);
    if (count != (ssize_t)sizeof(random)) return -1;
    int written = snprintf(out, size, "%s-%02x%02x%02x%02x", config->device_id,
                           random[0], random[1], random[2], random[3]);
    return written > 0 && (size_t)written < size ? 0 : -1;
}

int storage_next_pending(char *out, size_t size) {
    DIR *dir = opendir(PENDING_DIR);
    if (!dir) return -1;
    struct dirent *entry;
    int result = 0;
    while ((entry = readdir(dir))) {
        size_t len = strlen(entry->d_name);
        if (len < 5 || strcmp(entry->d_name + len - 4, ".mp4")) continue;
        int written = snprintf(out, size, "%s/%s", PENDING_DIR, entry->d_name);
        if (written < 0 || (size_t)written >= size) { result = -1; break; }
        result = 1;
        break;
    }
    closedir(dir);
    return result;
}

int storage_write_metadata(const Config *config, const char *capture_id) {
    char path[320], temp[324];
    int n = snprintf(path, sizeof(path), "%s/%s.json", PENDING_DIR, capture_id);
    if (n < 0 || (size_t)n >= sizeof(path)) return -1;
    n = snprintf(temp, sizeof(temp), "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof(temp)) return -1;
    cJSON *root = cJSON_CreateObject();
    if (!root) return -1;
    cJSON_AddStringToObject(root, "capture_id", capture_id);
    cJSON_AddNumberToObject(root, "width", config->width);
    cJSON_AddNumberToObject(root, "height", config->height);
    cJSON_AddNumberToObject(root, "fps", config->fps);
    cJSON_AddNumberToObject(root, "duration_ms", config->duration_ms);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return -1;
    FILE *file = fopen(temp, "wb");
    if (!file) { cJSON_free(json); return -1; }
    size_t len = strlen(json);
    int ok = fwrite(json, 1, len, file) == len && fflush(file) == 0 &&
             fsync(fileno(file)) == 0;
    if (fclose(file)) ok = 0;
    cJSON_free(json);
    if (!ok) return -1;
    return rename(temp, path);
}
