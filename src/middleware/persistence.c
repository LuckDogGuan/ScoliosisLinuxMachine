#include "v3.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#define PACKET_SIZE 4096
int v3_save_json(const char *path, const cJSON *value) {
    char temporary[512], output[PACKET_SIZE];
    if (!cJSON_PrintPreallocated((cJSON *)value, output, sizeof(output), 0)) return -1;
    int n = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof(temporary)) return -1;
    int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return -1;
    size_t length = strlen(output), at = 0;
    while (at < length) {
        ssize_t count = write(fd, output + at, length - at);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { close(fd); return -1; }
        at += (size_t)count;
    }
    int ok = !fsync(fd);
    if (close(fd)) ok = 0;
    if (!ok || rename(temporary, path)) return -1;
    char directory[512];
    snprintf(directory, sizeof(directory), "%s", path);
    char *slash = strrchr(directory, '/');
    if (!slash) return -1;
    *slash = 0;
    fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return -1;
    ok = !fsync(fd);
    close(fd);
    return ok ? 0 : -1;
}
cJSON *v3_read_json(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    char text[PACKET_SIZE];
    size_t count = fread(text, 1, sizeof(text) - 1, file);
    int ok = feof(file) && !ferror(file);
    fclose(file);
    if (!ok) return NULL;
    text[count] = 0;
    return cJSON_Parse(text);
}
