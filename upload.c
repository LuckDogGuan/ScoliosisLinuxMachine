#include "app.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

static cJSON *request(const Config *config, const char *route, const char *method_file,
                      const char *const headers[], int timeout) {
    char url[320], output[] = "/tmp/instrument_response_XXXXXX", seconds[12];
    int n = snprintf(url, sizeof(url), "%s%s", config->pc_base_url, route);
    if (n < 0 || (size_t)n >= sizeof(url)) return NULL;
    int fd = mkstemp(output);
    if (fd < 0) return NULL;
    close(fd);
    snprintf(seconds, sizeof(seconds), "%d", timeout);
    char *args[40];
    size_t at = 0;
    args[at++] = "wget";
    args[at++] = "-q";
    args[at++] = "-T";
    args[at++] = seconds;
    args[at++] = "-O";
    args[at++] = output;
    for (size_t i = 0; headers && headers[i]; i++) {
        if (at + 4 >= sizeof(args) / sizeof(args[0])) { unlink(output); return NULL; }
        args[at++] = "--header";
        args[at++] = (char *)headers[i];
    }
    if (method_file) {
        args[at++] = "--post-file";
        args[at++] = (char *)method_file;
    }
    args[at++] = url;
    args[at] = NULL;
    int result = run_command(args);
    cJSON *json = NULL;
    if (!result) {
        FILE *file = fopen(output, "rb");
        if (file) {
            char body[8192];
            size_t len = fread(body, 1, sizeof(body) - 1, file);
            if (feof(file)) { body[len] = 0; json = cJSON_Parse(body); }
            fclose(file);
        }
    }
    unlink(output);
    return json;
}

static cJSON *post_json(const Config *config, const char *route, const cJSON *payload) {
    char input[] = "/tmp/instrument_request_XXXXXX";
    int fd = mkstemp(input);
    if (fd < 0) return NULL;
    char *json = cJSON_PrintUnformatted(payload);
    if (!json) { close(fd); unlink(input); return NULL; }
    size_t len = strlen(json);
    ssize_t written = write(fd, json, len);
    cJSON_free(json);
    int closed = close(fd);
    if (written != (ssize_t)len || closed) { unlink(input); return NULL; }
    const char *headers[] = {"Content-Type: application/json", NULL};
    cJSON *response = request(config, route, input, headers, 5);
    unlink(input);
    return response;
}

int http_hello(const Config *config, const char *ip, int *paired) {
    cJSON *data = cJSON_CreateObject();
    if (!data) return -1;
    cJSON_AddStringToObject(data, "device_id", config->device_id);
    cJSON_AddStringToObject(data, "ip", ip);
    cJSON_AddStringToObject(data, "ssid", config->wifi_ssid);
    cJSON_AddNumberToObject(data, "rssi_dbm", wifi_rssi());
    cJSON_AddNumberToObject(data, "storage_free_mb", storage_free_mb());
    cJSON_AddStringToObject(data, "camera", "ISG1321");
    cJSON_AddStringToObject(data, "battery", battery_present() ? "present" : "absent");
    cJSON *response = post_json(config, "/api/v1/hello", data);
    cJSON_Delete(data);
    if (!response) return -1;
    int ok = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(response, "ok"));
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(response, "paired");
    if (ok && cJSON_IsBool(value)) *paired = cJSON_IsTrue(value);
    else ok = 0;
    cJSON_Delete(response);
    return ok ? 0 : -1;
}

int http_poll(const Config *config, int *capture) {
    char header[100];
    snprintf(header, sizeof(header), "Device-ID: %s", config->device_id);
    const char *headers[] = {header, NULL};
    cJSON *response = request(config, "/api/v1/pending-command", NULL, headers, 5);
    if (!response) return -1;
    const cJSON *command = cJSON_GetObjectItemCaseSensitive(response, "command");
    int ok = cJSON_IsString(command) && command->valuestring &&
             (!strcmp(command->valuestring, "capture") || !strcmp(command->valuestring, "none"));
    if (ok) *capture = !strcmp(command->valuestring, "capture");
    cJSON_Delete(response);
    return ok ? 0 : -1;
}

int http_capture_start(const Config *config, const char *capture_id) {
    cJSON *data = cJSON_CreateObject();
    if (!data) return -1;
    cJSON_AddStringToObject(data, "device_id", config->device_id);
    cJSON_AddStringToObject(data, "capture_id", capture_id);
    cJSON *response = post_json(config, "/api/v1/capture/start", data);
    cJSON_Delete(data);
    if (!response) return -1;
    int ok = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(response, "ok"));
    cJSON_Delete(response);
    return ok ? 0 : -1;
}

static int send_all(int fd, const void *buffer, size_t length) {
    const unsigned char *at = buffer;
    while (length) {
        ssize_t sent = send(fd, at, length, MSG_NOSIGNAL);
        if (sent < 0 && errno == EINTR) continue;
        if (sent <= 0) return -1;
        at += sent;
        length -= (size_t)sent;
    }
    return 0;
}

static int connect_pc(const Config *config, char *host, size_t host_size) {
    const char *start = config->pc_base_url + strlen("http://");
    const char *colon = strchr(start, ':');
    if (!colon || colon == start || (size_t)(colon - start) >= host_size) return -1;
    memcpy(host, start, (size_t)(colon - start));
    host[colon - start] = 0;
    char *end;
    long port = strtol(colon + 1, &end, 10);
    if (*end || port < 1 || port > 65535) return -1;
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &address.sin_addr) != 1) return -1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK)) { close(fd); return -1; }
    int result = connect(fd, (struct sockaddr *)&address, sizeof(address));
    if (result && errno == EINPROGRESS) {
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(fd, &writable);
        struct timeval timeout = {.tv_sec = 5};
        result = select(fd + 1, NULL, &writable, NULL, &timeout);
        if (result > 0) {
            int error = 0;
            socklen_t size = sizeof(error);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size)) result = -1;
            else if (error) { errno = error; result = -1; }
            else result = 0;
        } else result = -1;
    }
    if (result || fcntl(fd, F_SETFL, flags)) { close(fd); return -1; }
    struct timeval timeout = {.tv_sec = 60};
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) ||
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout))) {
        close(fd); return -1;
    }
    return fd;
}

int http_upload(const Config *config, const char *capture_id, const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) { perror("upload open"); return -1; }
    struct stat info;
    if (fstat(fileno(file), &info) || !S_ISREG(info.st_mode) || info.st_size <= 0) {
        fclose(file); return -1;
    }
    char host[64];
    int fd = connect_pc(config, host, sizeof(host));
    if (fd < 0) { fprintf(stderr, "upload connect: %s\n", strerror(errno)); fclose(file); return -1; }
    char header[1024];
    int n = snprintf(header, sizeof(header),
        "POST /api/v1/capture/file HTTP/1.1\r\n"
        "Host: %s\r\nConnection: close\r\nContent-Type: video/mp4\r\n"
        "Content-Length: %lld\r\nDevice-ID: %s\r\nCapture-ID: %s\r\n"
        "Width: %d\r\nHeight: %d\r\nFps: %d\r\nDuration-Ms: %d\r\n"
        "Codec: h265\r\nContainer: mp4\r\n\r\n",
        host, (long long)info.st_size, config->device_id, capture_id,
        config->width, config->height, config->fps, config->duration_ms);
    int ok = n > 0 && (size_t)n < sizeof(header) && send_all(fd, header, (size_t)n) == 0;
    char chunk[8192];
    off_t remaining = info.st_size;
    while (ok && remaining > 0) {
        size_t wanted = remaining < (off_t)sizeof(chunk) ? (size_t)remaining : sizeof(chunk);
        size_t count = fread(chunk, 1, wanted, file);
        if (count != wanted || send_all(fd, chunk, count)) { ok = 0; break; }
        remaining -= (off_t)count;
    }
    fclose(file);
    if (ok) shutdown(fd, SHUT_WR);
    char response[8192];
    size_t length = 0;
    while (ok && length < sizeof(response) - 1) {
        ssize_t count = recv(fd, response + length, sizeof(response) - 1 - length, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) { ok = 0; break; }
        if (!count) break;
        length += (size_t)count;
    }
    close(fd);
    response[length] = 0;
    if (!ok) { fprintf(stderr, "upload send/receive: %s\n", strerror(errno)); return -1; }
    if (strncmp(response, "HTTP/1.1 200 ", 13) &&
        strncmp(response, "HTTP/1.0 200 ", 13)) {
        response[strcspn(response, "\r\n")] = 0;
        fprintf(stderr, "upload response: %s\n", response);
        return -1;
    }
    char *body = strstr(response, "\r\n\r\n");
    if (!body) { fputs("upload response: missing body\n", stderr); return -1; }
    body += 4;
    char *start = strchr(body, '{');
    char *end = start ? strrchr(start, '}') : NULL;
    if (!end) { fputs("upload response: missing JSON\n", stderr); return -1; }
    cJSON *json = cJSON_ParseWithLength(start, (size_t)(end - start + 1));
    if (!json) { fputs("upload response: invalid JSON\n", stderr); return -1; }
    ok = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "ok"));
    cJSON_Delete(json);
    return ok ? 0 : -1;
}
