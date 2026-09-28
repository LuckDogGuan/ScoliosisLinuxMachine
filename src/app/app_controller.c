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

static int persist(const char *command_id, const char *capture_id, int phase) {
    if (storage_command_save(command_id ? command_id : "",
                             capture_id ? capture_id : "", phase)) {
        syslog(LOG_ERR, "cannot persist command state");
        return -1;
    }
    return 0;
}

static void clear_command(char *command_id, char *capture_id, int *phase) {
    (void)persist("", "", COMMAND_NONE);
    command_id[0] = 0;
    capture_id[0] = 0;
    *phase = COMMAND_NONE;
}

static int has_pending_mp4(void) {
    char path[384];
    return storage_next_pending(path, sizeof(path)) == 1;
}

static int has_writing_file(void) {
    DIR *dir = opendir(WRITING_DIR);
    if (!dir) return 0;
    struct dirent *entry;
    int found = 0;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        found = 1;
        break;
    }
    closedir(dir);
    return found;
}

static int remove_capture_files(const char *capture_id, const char *path) {
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

static int upload_and_remove(const Config *config, const char *capture_id, const char *path) {
    if (!http_upload(config, capture_id, path))
        return remove_capture_files(capture_id, path);
    int stored = 0;
    if (!http_file_stored(config, capture_id, &stored) && stored == 1)
        return remove_capture_files(capture_id, path);
    syslog(LOG_WARNING, "upload failed: %s", capture_id);
    return -1;
}

static void retry_one_pending(const Config *config) {
    char path[384], capture_id[128];
    if (storage_next_pending(path, sizeof(path)) != 1) return;
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    size_t length = strlen(name);
    if (length < 5 || strcmp(name + length - 4, ".mp4") || length - 4 >= sizeof(capture_id))
        return;
    memcpy(capture_id, name, length - 4);
    capture_id[length - 4] = 0;
    (void)upload_and_remove(config, capture_id, path);
}

static int refuse_if_low_space(void) {
    long free_mb = storage_free_mb();
    if (free_mb < 16) {
        syslog(LOG_WARNING, "capture refused: free space %ld MB", free_mb);
        (void)audio_play("storage_low");
        return -1;
    }
    return 0;
}

static void after_record(const Config *config, const char *capture_id, int network_ready) {
    (void)audio_play("capture_done");
    char path[384];
    int n = snprintf(path, sizeof(path), "%s/%s.mp4", PENDING_DIR, capture_id);
    if (n < 0 || (size_t)n >= sizeof(path)) return;
    if (!network_ready || upload_and_remove(config, capture_id, path))
        (void)audio_play("saved_on_device");
    else
        (void)audio_play("uploaded");
}

static void ack_current(const Config *config, char *command_id, char *capture_id, int *phase) {
    if (!http_command_ack(config, command_id) &&
        !persist(command_id, capture_id, COMMAND_ACKED))
        *phase = COMMAND_ACKED;
}

static int accept_seen(const Config *config, char *command_id, char *capture_id, int *phase) {
    if (persist(command_id, capture_id, COMMAND_SEEN)) return -1;
    *phase = COMMAND_SEEN;
    ack_current(config, command_id, capture_id, phase);
    return 0;
}

static void apply_receipt(const Config *config, char *command_id, size_t command_n,
                          char *capture_id, int *phase) {
    char receipt_id[128];
    int capture = 0, acked = 0, started = 0;
    if (http_command_receipt(config, receipt_id, sizeof(receipt_id), &capture, &acked, &started))
        return;
    if (started && command_id[0] && receipt_id[0] && !strcmp(command_id, receipt_id)) {
        if (!persist(command_id, capture_id, COMMAND_STARTED)) *phase = COMMAND_STARTED;
        return;
    }
    if (acked && command_id[0] && receipt_id[0] && !strcmp(command_id, receipt_id)) {
        if (!persist(command_id, capture_id, COMMAND_ACKED)) *phase = COMMAND_ACKED;
        return;
    }
    if (capture && receipt_id[0] && (!command_id[0] || !strcmp(command_id, receipt_id))) {
        if (!command_id[0]) {
            if (strlen(receipt_id) >= command_n) return;
            strcpy(command_id, receipt_id);
        }
        (void)accept_seen(config, command_id, capture_id, phase);
    }
}

static void start_and_record(const Config *config, char *command_id, char *capture_id,
                             size_t capture_n, int *phase, time_t *last_hello) {
    if (refuse_if_low_space()) return;
    if (!capture_id[0]) {
        if (storage_capture_id(config, capture_id, capture_n)) {
            syslog(LOG_ERR, "capture id generation failed");
            return;
        }
        if (persist(command_id, capture_id, COMMAND_ACKED)) return;
    }
    int start = http_capture_start(config, capture_id);
    if (start == -1) {
        char receipt_id[128];
        int capture = 0, acked = 0, started = 0;
        if (!http_command_receipt(config, receipt_id, sizeof(receipt_id),
                                  &capture, &acked, &started) &&
            started && (!receipt_id[0] || !strcmp(receipt_id, command_id)))
            start = 0;
        else
            return;
    }
    if (start == -2) {
        syslog(LOG_WARNING, "capture start was not accepted: %s", capture_id);
        (void)audio_play("capture_failed");
        clear_command(command_id, capture_id, phase);
        return;
    }
    if (persist(command_id, capture_id, COMMAND_STARTED)) return;
    *phase = COMMAND_STARTED;
    if (capture_record(config, capture_id)) {
        syslog(LOG_ERR, "recording failed: %s", capture_id);
        (void)audio_play("capture_failed");
        return;
    }
    after_record(config, capture_id, 1);
    *last_hello = 0;
}

static void handle_local_capture(const Config *config, int network_ready, time_t *last_hello) {
    if (refuse_if_low_space()) return;
    char capture_id[128];
    if (storage_capture_id(config, capture_id, sizeof(capture_id))) {
        syslog(LOG_ERR, "capture id generation failed");
        return;
    }
    if (network_ready) {
        int start = http_capture_start(config, capture_id);
        if (start == -2) {
            syslog(LOG_WARNING, "capture start was not accepted; recording cancelled: %s",
                   capture_id);
            (void)audio_play("capture_failed");
            return;
        }
    }
    if (capture_record(config, capture_id)) {
        syslog(LOG_ERR, "recording failed: %s", capture_id);
        (void)audio_play("capture_failed");
        return;
    }
    after_record(config, capture_id, network_ready);
    *last_hello = 0;
}

static void handle_legacy_capture(const Config *config, time_t *last_hello) {
    if (refuse_if_low_space()) return;
    char capture_id[128];
    if (storage_capture_id(config, capture_id, sizeof(capture_id))) {
        syslog(LOG_ERR, "capture id generation failed");
        return;
    }
    int start = http_capture_start(config, capture_id);
    if (start == -2) {
        syslog(LOG_WARNING, "capture start was not accepted; recording cancelled: %s", capture_id);
        (void)audio_play("capture_failed");
        return;
    }
    if (start == -1) return;
    if (capture_record(config, capture_id)) {
        syslog(LOG_ERR, "recording failed: %s", capture_id);
        (void)audio_play("capture_failed");
        return;
    }
    after_record(config, capture_id, 1);
    *last_hello = 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--version")) {
        puts(INSTRUMENT_VERSION);
        return 0;
    }
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
    FILE *version_file = fopen(STATE_DIR "/version", "wb");
    if (version_file) {
        fputs(INSTRUMENT_VERSION "\n", version_file);
        fclose(version_file);
    }
    if (discover_open()) syslog(LOG_ERR, "UDP discover socket failed");
    syslog(LOG_INFO, "instrument %s started; volume %d", INSTRUMENT_VERSION, config.volume);
    int paired = 0;
    time_t last_hello = 0;
    while (running) {
        char command_id[128], capture_id[128];
        int phase = COMMAND_NONE;
        if (storage_command_load(command_id, sizeof(command_id), capture_id, sizeof(capture_id),
                                 &phase)) {
            syslog(LOG_WARNING, "command state unreadable; ignoring");
            command_id[0] = 0;
            capture_id[0] = 0;
            phase = COMMAND_NONE;
        }
        if (phase == COMMAND_STARTED && !has_pending_mp4() && !has_writing_file()) {
            syslog(LOG_WARNING, "command started but no capture file remains; clearing command");
            clear_command(command_id, capture_id, &phase);
        }

        if (discover_poll(&config)) last_hello = 0;
        char ip[INET_ADDRSTRLEN];
        int local_trigger = button_capture_pressed();
        if (wifi_address(ip, sizeof(ip))) {
            paired = 0;
            light_update(paired);
            if (local_trigger && phase == COMMAND_NONE) handle_local_capture(&config, 0, &last_hello);
            sleep(1);
            continue;
        }
        struct timespec clock_time;
        if (clock_gettime(CLOCK_MONOTONIC, &clock_time)) { sleep(1); continue; }
        time_t now = clock_time.tv_sec;
        if (!last_hello || now - last_hello >= 3) {
            if (http_hello(&config, ip, &paired)) paired = 0;
            else retry_one_pending(&config);
            last_hello = now;
            light_update(paired);
        }
        if (phase == COMMAND_STARTED && !has_pending_mp4() && !has_writing_file()) {
            syslog(LOG_WARNING, "command started but no capture file remains; clearing command");
            clear_command(command_id, capture_id, &phase);
        }

        if (local_trigger && phase == COMMAND_NONE)
            handle_local_capture(&config, 1, &last_hello);

        int pending = has_pending_mp4();
        if (paired && !local_trigger) {
            if (phase == COMMAND_NONE && !pending) {
                int remote_capture = 0;
                char poll_id[128];
                if (http_poll(&config, &remote_capture, poll_id, sizeof(poll_id)))
                    syslog(LOG_WARNING, "command poll failed");
                else if (remote_capture && poll_id[0]) {
                    strcpy(command_id, poll_id);
                    (void)accept_seen(&config, command_id, capture_id, &phase);
                } else if (remote_capture)
                    handle_legacy_capture(&config, &last_hello);
                else
                    apply_receipt(&config, command_id, sizeof(command_id), capture_id, &phase);
            } else if (phase == COMMAND_SEEN)
                apply_receipt(&config, command_id, sizeof(command_id), capture_id, &phase);
            if (phase == COMMAND_ACKED && !has_pending_mp4())
                start_and_record(&config, command_id, capture_id, sizeof(capture_id),
                                 &phase, &last_hello);
        }
        sleep(1);
    }
    closelog();
    return 0;
}
