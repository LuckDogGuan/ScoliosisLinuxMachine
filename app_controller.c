#include "app.h"
#include <dirent.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;

static void stop(int signal_number) { (void)signal_number; running = 0; }

int run_command(char *const argv[]) {
    pid_t child = fork();
    if (child < 0) return -1;
    if (!child) { execvp(argv[0], argv); _exit(127); }
    int status;
    while (waitpid(child, &status, 0) < 0) if (errno != EINTR) return -1;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

static int upload_and_remove(const Config *config, const char *capture_id, const char *path) {
    if (http_upload(config, capture_id, path)) {
        syslog(LOG_WARNING, "upload failed: %s", capture_id);
        return -1;
    }
    if (unlink(path)) {
        syslog(LOG_ERR, "uploaded but cannot remove local file: %s", path);
        return -1;
    }
    char metadata[384];
    int n = snprintf(metadata, sizeof(metadata), "%s/%s.json", PENDING_DIR, capture_id);
    if (n > 0 && (size_t)n < sizeof(metadata)) (void)unlink(metadata);
    syslog(LOG_INFO, "upload acknowledged: %s", capture_id);
    return 0;
}

static void retry_pending_once(const Config *config) {
    DIR *dir = opendir(PENDING_DIR);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) && running) {
        size_t length = strlen(entry->d_name);
        size_t prefix = strlen(config->device_id);
        if (length < 5 || strcmp(entry->d_name + length - 4, ".mp4") ||
            length <= prefix + 4 || strncmp(entry->d_name, config->device_id, prefix) ||
            entry->d_name[prefix] != '-') continue;
        char capture_id[128], path[384];
        if (length - 4 >= sizeof(capture_id)) continue;
        memcpy(capture_id, entry->d_name, length - 4);
        capture_id[length - 4] = 0;
        int n = snprintf(path, sizeof(path), "%s/%s", PENDING_DIR, entry->d_name);
        if (n > 0 && (size_t)n < sizeof(path)) (void)upload_and_remove(config, capture_id, path);
    }
    closedir(dir);
}

static void handle_capture(const Config *config, int local_trigger, int network_ready) {
    long free_mb = storage_free_mb();
    if (free_mb < 16) {
        syslog(LOG_WARNING, "capture refused: free space %ld MB", free_mb);
        (void)audio_play("storage_low");
        return;
    }
    char capture_id[128], path[384];
    if (storage_capture_id(config, capture_id, sizeof(capture_id))) {
        syslog(LOG_ERR, "capture id generation failed");
        return;
    }
    if (network_ready && http_capture_start(config, capture_id) && !local_trigger) {
        syslog(LOG_WARNING, "capture start was not accepted; recording cancelled: %s", capture_id);
        (void)audio_play("capture_failed");
        return;
    }
    if (capture_record(config, capture_id)) {
        syslog(LOG_ERR, "recording failed: %s", capture_id);
        (void)audio_play("capture_failed");
        return;
    }
    (void)audio_play("capture_done");
    int n = snprintf(path, sizeof(path), "%s/%s.mp4", PENDING_DIR, capture_id);
    if (n < 0 || (size_t)n >= sizeof(path)) return;
    if (!network_ready || upload_and_remove(config, capture_id, path)) (void)audio_play("saved_on_device");
    else (void)audio_play("uploaded");
}

int main(int argc, char **argv) {
    const char *config_path = argc > 1 && argv[1][0] != '-' ? argv[1] :
                              "/userdata/instrument/config.json";
    if (argc == 3 && !strcmp(argv[1], "--check-config")) config_path = argv[2];
    if (argc == 5 && !strcmp(argv[1], "--upload-file")) config_path = argv[2];
    Config config = {0};
    if (config_load(config_path, &config)) {
        fprintf(stderr, "invalid config: %s\n", config_path);
        return 2;
    }
    if (argc == 3 && !strcmp(argv[1], "--check-config")) {
        puts("config OK");
        return 0;
    }
    openlog("instrument_app", LOG_PID | LOG_CONS, LOG_DAEMON);
    if (argc == 5 && !strcmp(argv[1], "--upload-file"))
        return http_upload(&config, argv[3], argv[4]) ? 1 : 0;
    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    signal(SIGPIPE, SIG_IGN);
    if (storage_prepare()) {
        syslog(LOG_ERR, "cannot create pending directory");
        return 1;
    }
    audio_set_volume(config.volume);
    syslog(LOG_INFO, "battery absent; instrument started");
    int paired = 0;
    int retried_pending = 0;
    time_t last_hello = 0;
    while (running) {
        char ip[INET_ADDRSTRLEN];
        int local_trigger = button_capture_pressed();
        if (wifi_address(ip, sizeof(ip))) {
            paired = 0;
            light_update(paired);
            if (local_trigger) handle_capture(&config, 1, 0);
            sleep(1);
            continue;
        }
        struct timespec clock_time;
        if (clock_gettime(CLOCK_MONOTONIC, &clock_time)) { sleep(1); continue; }
        time_t now = clock_time.tv_sec;
        if (!last_hello || now - last_hello >= 3) {
            if (http_hello(&config, ip, &paired)) paired = 0;
            else if (!retried_pending) { retry_pending_once(&config); retried_pending = 1; }
            last_hello = now;
            light_update(paired);
        }
        int remote_capture = 0;
        if (paired && !local_trigger && http_poll(&config, &remote_capture))
            syslog(LOG_WARNING, "command poll failed");
        if (local_trigger || (remote_capture && paired)) {
            handle_capture(&config, local_trigger, 1);
            last_hello = 0;
        }
        sleep(1);
    }
    closelog();
    return 0;
}
