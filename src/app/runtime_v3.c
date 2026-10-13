#include "v3.h"
#include "policy.h"
#include "platform.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <linux/rtc.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syslog.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>

#define EVENTS_DIR STATE_DIR "/events"
#define PACKET_SIZE 4096
#define ID_SIZE 128
#define MAX_ITEMS 8
#define COPY(dst, src) copy_text(dst, sizeof(dst), src)
static void copy_text(char *out, size_t size, const char *text) {
    size_t length = strlen(text); if (length >= size) length = size - 1;
    memcpy(out, text, length); out[length] = 0;
}
static volatile sig_atomic_t active = 1;
static void halt(int sig) { (void)sig; active = 0; }
static const char *str(const cJSON *obj, const char *key) {
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(value) && value->valuestring ? value->valuestring : "";
}
static double num(const cJSON *obj, const char *key) {
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(value) ? value->valuedouble : -1;
}
static int safe_id(const char *id) {
    size_t length = strlen(id);
    if (!length || length >= ID_SIZE) return 0;
    for (const unsigned char *p = (const unsigned char *)id; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')) return 0;
    return 1;
}

typedef struct { char id[ID_SIZE], hash[65], result[16]; long long size; int duration; uint64_t started_at; } Item;
typedef struct {
    char type;
    int result, duration, reason, rssi, gateway, count, wifi;
    uint64_t started_at, ended_at;
    long free_mb, total_mb, available_mb, memory_mb;
    char wifi_state[32], event[ID_SIZE];
    Item items[MAX_ITEMS];
} WorkerResult;
typedef struct { pid_t pid; int fd; uint64_t started; } Worker;
typedef struct {
    Config *config;
    int udp, paired, gate, keys, release_required, recording, stop_sent, revoking;
    int middle_fired, audio_result, audio_volume, camera_fault;
    uint64_t recording_mono, stopped_mono, recovery_retry;
    cJSON *last_completed;
    char system_boot[64];
    Worker recovery;
    uint64_t middle_hold;
    char audio_id[ID_SIZE], event_cursor[256];
    char boot[ID_SIZE], host[ID_SIZE], txn[ID_SIZE], candidate[ID_SIZE], candidate_ip[64];
    int candidate_port;
    char revoked_host[ID_SIZE], unpair[ID_SIZE], gate_id[ID_SIZE], capture_id[ID_SIZE], current_id[ID_SIZE], current_gate[ID_SIZE];
    char phase[32], wifi_state[32], peer_ip[64];
    uint64_t lease, pair_window, txn_deadline, hold_started, last_maintenance, metrics_at;
    uint64_t started_at, sequence, event_retry, metric_retry, completion_retry;
    int completion_pending;
    WorkerResult completion;
    char completion_event[ID_SIZE];
    char last_nonce[ID_SIZE], last_op[64], last_reply[PACKET_SIZE];
    long free_mb, total_mb, available_mb, memory_mb;
    int rssi, gateway_failures, wifi_seen;
    uint64_t startup_at, diagnostics_lease, diagnostics_next, last_host_contact;
    char diagnostics_id[ID_SIZE];
    int backlight_brightness, diagnostics_brightness;
    Worker diagnostics;
    Worker record, upload, maintenance, notifier, audio, discard;
    char maintenance_type;
    char upload_id[ID_SIZE];
    char sounds[8][32]; unsigned sound_in, sound_out;
    char speech_id[ID_SIZE]; uint64_t speech_lease, speech_resume;
} Runtime;
static int request_unpair(Runtime *rt);
static int begin_audio(Runtime *rt, const cJSON *request, int test);
static void audio_messages(Runtime *rt);
static void queue_sound(Runtime *rt, const char *name) {
    if (rt->sound_in - rt->sound_out >= 8) return;
    snprintf(rt->sounds[rt->sound_in++ % 8], 32, "%s", name);
}
static int save_pair(Runtime *rt) {
    cJSON *state = cJSON_CreateObject();
    if (!state) return -1;
    cJSON_AddStringToObject(state, "host_id", rt->host);
    cJSON_AddBoolToObject(state, "revoking", rt->revoking);
    cJSON_AddStringToObject(state, "revoked_host_id", rt->revoked_host);
    cJSON_AddStringToObject(state, "txn_id", rt->txn);
    cJSON_AddStringToObject(state, "unpair_event_id", rt->unpair);
    cJSON_AddStringToObject(state, "pc_base_url", rt->config->pc_base_url);
    cJSON_AddStringToObject(state, "peer_ip", rt->peer_ip);
    int result = v3_save_json(STATE_DIR "/pair-v3.json", state);
    cJSON_Delete(state);
    return result;
}
static void disable_gate(Runtime *rt) {
    int was_enabled = rt->gate;
    rt->gate = 0;
    rt->lease = 0;
    rt->capture_id[0] = 0;
    if (!rt->record.pid) COPY(rt->phase, "idle");
    if (was_enabled) queue_sound(rt, "capture_disabled");
}
static int local_unpair(Runtime *rt) {
    char previous[ID_SIZE]; COPY(previous, rt->host);
    if (storage_capture_id(rt->config, rt->unpair, sizeof(rt->unpair))) return -1;
    COPY(rt->revoked_host, rt->host);
    rt->host[0] = 0;
    int revoking = rt->revoking; rt->revoking = 0;
    if (save_pair(rt)) { rt->revoking = revoking; COPY(rt->host, previous); rt->unpair[0] = 0; return -1; }
    rt->paired = 0; rt->revoking = 0;
    rt->speech_lease=0; rt->speech_id[0]=0; rt->speech_resume=v3_now()+600;
    disable_gate(rt);
    rt->candidate[0] = 0;
    rt->pair_window = 0;
    queue_sound(rt, "pair_disconnected");
    light_update(0);
    return 0;
}
static int make_worker(Worker *worker, int *child_fd) {
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets)) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(sockets[0]); close(sockets[1]); return -1; }
    if (!pid) {
        (void)setpgid(0,0); (void)prctl(PR_SET_PDEATHSIG,SIGKILL);
        if (getppid()==1) _exit(1);
        close(sockets[0]); *child_fd = sockets[1]; return 0;
    }
    close(sockets[1]);
    worker->pid = pid;
    worker->started = v3_now();
    (void)setpgid(pid,pid);
    worker->fd = sockets[0];
    int flags = fcntl(worker->fd, F_GETFL, 0);
    (void)fcntl(worker->fd, F_SETFL, flags | O_NONBLOCK);
    return 1;
}
static void child_isolate(Runtime *rt, int own_fd) {
    close(rt->udp);
    Worker *workers[] = {&rt->record, &rt->upload, &rt->maintenance, &rt->notifier, &rt->audio, &rt->discard, &rt->diagnostics, &rt->recovery};
    for (size_t i = 0; i < sizeof(workers)/sizeof(workers[0]); i++)
        if (workers[i]->pid > 0 && workers[i]->fd >= 0 && workers[i]->fd != own_fd) close(workers[i]->fd);
}
static void finish_worker(Worker *worker) {
    int status;
    (void)waitpid(worker->pid, &status, WNOHANG);
    close(worker->fd);
    worker->fd = -1;
    worker->pid = 0;
}
static int digest(const char *path, char hash[65], long long *size) {
    struct stat info;
    if (stat(path, &info) || !S_ISREG(info.st_mode)) return -1;
    int pipes[2];
    if (pipe(pipes)) return -1;
    pid_t child = fork();
    if (child < 0) { close(pipes[0]); close(pipes[1]); return -1; }
    if (!child) {
        close(pipes[0]);
        if (dup2(pipes[1], STDOUT_FILENO) < 0) _exit(1);
        close(pipes[1]);
        execlp("sha256sum", "sha256sum", path, (char *)NULL);
        _exit(1);
    }
    close(pipes[1]);
    char data[128]; size_t at = 0;
    while (at < sizeof(data)) {
        ssize_t n = read(pipes[0], data + at, sizeof(data) - at);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        at += (size_t)n;
    }
    close(pipes[0]);
    int status;
    while (waitpid(child, &status, 0) < 0) if (errno != EINTR) return -1;
    if (at < 65 || !WIFEXITED(status) || WEXITSTATUS(status)) return -1;
    for (int i = 0; i < 64; i++) if (!((data[i] >= 'a' && data[i] <= 'f') || (data[i] >= '0' && data[i] <= '9'))) return -1;
    memcpy(hash, data, 64); hash[64] = 0; *size = (long long)info.st_size;
    return 0;
}
static int discard_intended(const char *id) {
    if (!safe_id(id)) return 1;
    char path[384]; snprintf(path,sizeof(path),"%s/discard-%s.json",STATE_DIR,id);
    return !access(path,F_OK);
}
static int pending_items(int offset, int limit, Item items[MAX_ITEMS], int *next) {
    DIR *directory = opendir(PENDING_DIR);
    if (!directory) return -1;
    int at = 0, count = 0; struct dirent *entry; *next = -1;
    while ((entry = readdir(directory))) {
        size_t len = strlen(entry->d_name);
        if (len <= 4 || strcmp(entry->d_name + len - 4, ".mp4") || len - 4 >= ID_SIZE) continue;
        if (at++ < offset) continue;
        if (count == limit) { *next = offset + count; break; }
        Item *item = &items[count]; memset(item, 0, sizeof(*item));
        memcpy(item->id, entry->d_name, len - 4);
        if (!safe_id(item->id) || discard_intended(item->id)) continue;
        char path[384]; snprintf(path, sizeof(path), "%s/%s", PENDING_DIR, entry->d_name);
        struct stat info; if (stat(path, &info) || !S_ISREG(info.st_mode)) continue;
        item->size = (long long)info.st_size;
        snprintf(path, sizeof(path), "%s/%s.json", PENDING_DIR, item->id);
        cJSON *metadata = v3_read_json(path);
        if (strlen(str(metadata, "sha256")) == 64) COPY(item->hash, str(metadata, "sha256"));
        item->duration = (int)num(metadata, "duration_ms");
        COPY(item->result, str(metadata, "result"));
        item->started_at = num(metadata, "started_at_ms") > 0 ? (uint64_t)num(metadata, "started_at_ms") : 0;
        cJSON_Delete(metadata);
        count++;
    }
    closedir(directory);
    return count;
}
static cJSON *next_event(Runtime *rt, char event_id[ID_SIZE]) {
    DIR *directory = opendir(EVENTS_DIR);
    if (!directory) return NULL;
    struct dirent *entry; char first[256] = {0}, after[256] = {0};
    while ((entry = readdir(directory))) {
        size_t len = strlen(entry->d_name);
        if (len <= 5 || strcmp(entry->d_name + len - 5, ".json")) continue;
        char path[384]; snprintf(path, sizeof(path), "%s/%s", EVENTS_DIR, entry->d_name);
        cJSON *value = v3_read_json(path);
        int eligible = value && !discard_intended(str(value,"capture_id")) && !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(value, "acknowledged")) &&
            (!str(value, "host_id")[0] || !strcmp(str(value, "host_id"), rt->host));
        cJSON_Delete(value);
        if (!eligible) continue;
        if (!first[0] || strcmp(entry->d_name, first) < 0) COPY(first, entry->d_name);
        if (strcmp(entry->d_name, rt->event_cursor) > 0 && (!after[0] || strcmp(entry->d_name, after) < 0)) COPY(after, entry->d_name);
    }
    closedir(directory);
    const char *name = after[0] ? after : first;
    if (!name[0]) return NULL;
    COPY(rt->event_cursor, name);
    char path[384]; snprintf(path, sizeof(path), "%s/%s", EVENTS_DIR, name);
    cJSON *value = v3_read_json(path);
    snprintf(event_id, ID_SIZE, "%s", str(value, "event_id"));
    return value;
}
static void acknowledge_completion(Runtime *rt, const WorkerResult *result) {
    if (result->result || !safe_id(result->event)) return;
    char path[384]; snprintf(path, sizeof(path), "%s/%s.json", EVENTS_DIR, result->event);
    cJSON *event = v3_read_json(path);
    if (event) {
        cJSON_DeleteItemFromObjectCaseSensitive(event, "acknowledged");
        cJSON_AddBoolToObject(event, "acknowledged", 1);
        (void)v3_save_json(path, event); cJSON_Delete(event);
    }
    (void)rt;
}
static void immediate_completion(Runtime *rt, const cJSON *event) {
    if (!rt->paired || rt->notifier.pid || rt->record.pid) return;
    int fd; int branch = make_worker(&rt->notifier, &fd);
    if (!branch) {
        child_isolate(rt, fd);
        WorkerResult result = {.type = 'E'};
        result.result = http_v3_status(rt->config, event);
        COPY(result.event, str(event, "capture_id"));
        (void)send(fd, &result, sizeof(result), MSG_NOSIGNAL); close(fd); _exit(0);
    }
}
static void notifier_messages(Runtime *rt) {
    if (!rt->notifier.pid) return;
    WorkerResult result; ssize_t size = recv(rt->notifier.fd, &result, sizeof(result), MSG_DONTWAIT);
    if (size == (ssize_t)sizeof(result)) {
        finish_worker(&rt->notifier); acknowledge_completion(rt, &result);
        rt->event_retry = v3_now() + (result.result ? 2000 : 0);
    } else if (!size) { finish_worker(&rt->notifier); rt->event_retry = v3_now() + 2000; }
}
static void start_record(Runtime *rt) {
    long available, total;
    long expected = ((long)rt->config->bitrate_kbps * 60000 + 7999999) / 8000000;
    long threshold = 16 + 2 * expected;
    if (threshold < 100) threshold = 100;
    if (storage_free_mb() < threshold || storage_memory_mb(&available, &total) || available < 32) {
        queue_sound(rt, "storage_low"); rt->release_required = 1; return;
    }
    COPY(rt->current_id, rt->capture_id); COPY(rt->current_gate, rt->gate_id);
    rt->capture_id[0] = 0;
    COPY(rt->phase, "starting"); rt->release_required = 1; rt->stop_sent = 0;
    cJSON *journal = cJSON_CreateObject();
    cJSON_AddStringToObject(journal, "capture_id", rt->current_id);
    cJSON_AddStringToObject(journal, "gate_id", rt->current_gate);
    cJSON_AddStringToObject(journal, "phase", "starting");
    int journal_ok = !v3_save_json(STATE_DIR "/active-v3.json", journal);
    cJSON_Delete(journal);
    if (!journal_ok) { disable_gate(rt); return; }
    int fd; int branch = make_worker(&rt->record, &fd);
    if (branch < 0) { COPY(rt->phase, "idle"); return; }
    if (!branch) {
        child_isolate(rt, fd);
        WorkerResult result = {.type = 'C', .result = -1, .reason = 2};
        int accepted = !http_v3_start(rt->config, rt->current_id, rt->current_gate, rt->boot);
        /* Audio failure must never veto a valid recording authorization. */
        if (accepted) {
            result.result = capture_controlled(rt->config, rt->current_id, fd, &result.duration, &result.reason);
        }
        result.ended_at = wall_ms();
        (void)send(fd, &result, sizeof(result), MSG_NOSIGNAL);
        close(fd); _exit(0);
    }
    rt->sound_out = rt->sound_in;
    syslog(LOG_INFO, "V3 capture preparing %s", rt->current_id);
}
static void completed(Runtime *rt, const WorkerResult *result) {
    char path[384], event_id[ID_SIZE];
    rt->completion = *result; rt->completion_pending = 1; rt->completion_retry = v3_now() + 1000;
    COPY(rt->phase, "finalizing");
    if (!rt->completion_event[0] && storage_capture_id(rt->config, rt->completion_event, sizeof(rt->completion_event))) { disable_gate(rt); return; }
    COPY(event_id, rt->completion_event);
    cJSON *event = cJSON_CreateObject();
    if (!event) { disable_gate(rt); return; }
    cJSON_AddNumberToObject(event, "version", 3);
    cJSON_AddStringToObject(event, "device_id", rt->config->device_id);
    cJSON_AddStringToObject(event, "host_id", rt->host);
    cJSON_AddStringToObject(event, "boot_id", rt->boot);
    cJSON_AddStringToObject(event, "capture_id", rt->current_id);
    cJSON_AddStringToObject(event, "gate_id", rt->current_gate);
    cJSON_AddStringToObject(event, "event_id", event_id);
    cJSON_AddStringToObject(event, "phase", "completed");
    cJSON_AddStringToObject(event, "result", result->result ? "failed" : result->duration < 3000 ? "short" : "success");
    cJSON_AddNumberToObject(event, "duration_ms", result->duration);
    cJSON_AddNumberToObject(event, "started_at_ms", (double)rt->started_at);
    cJSON_AddNumberToObject(event, "ended_at_ms", rt->started_at ? (double)(rt->started_at + (unsigned)result->duration) : 0);
    cJSON_AddStringToObject(event, "stop_reason", rt->revoking ? "unpaired" : result->reason == 0 ? "released" : result->reason == 1 ? "limit" : "error");
    if (v3_save_json(STATE_DIR "/active-v3.json", event)) {
        syslog(LOG_CRIT, "cannot persist completion journal %s; retrying", rt->current_id);
        disable_gate(rt); cJSON_Delete(event); return;
    }
    snprintf(path, sizeof(path), "%s/%s.json", PENDING_DIR, rt->current_id);
    cJSON *metadata = v3_read_json(path);
    if (!metadata) metadata = cJSON_CreateObject();
    cJSON_DeleteItemFromObjectCaseSensitive(metadata, "duration_ms");
    cJSON_AddNumberToObject(metadata, "duration_ms", result->duration);
    if (!str(metadata, "capture_id")[0]) cJSON_AddStringToObject(metadata, "capture_id", rt->current_id);
    if (!cJSON_GetObjectItemCaseSensitive(metadata, "width")) cJSON_AddNumberToObject(metadata, "width", rt->config->width);
    if (!cJSON_GetObjectItemCaseSensitive(metadata, "height")) cJSON_AddNumberToObject(metadata, "height", rt->config->height);
    if (!cJSON_GetObjectItemCaseSensitive(metadata, "fps")) cJSON_AddNumberToObject(metadata, "fps", rt->config->fps);
    cJSON_AddStringToObject(metadata, "event_id", event_id);
    cJSON_AddStringToObject(metadata, "result", str(event, "result"));
    cJSON_AddStringToObject(metadata, "host_id", rt->host);
    cJSON_AddNumberToObject(metadata, "started_at_ms", (double)rt->started_at);
    cJSON_AddNumberToObject(metadata, "ended_at_ms", rt->started_at ? (double)(rt->started_at + (unsigned)result->duration) : 0);
    int saved = !v3_save_json(path, metadata);
    cJSON_Delete(metadata);
    snprintf(path, sizeof(path), "%s/%s.json", EVENTS_DIR, rt->current_id);
    if (!saved || v3_save_json(path, event)) {
        syslog(LOG_CRIT, "cannot persist completed capture %s; retained for retry", rt->current_id);
        disable_gate(rt); cJSON_Delete(event); return;
    }
    cJSON_Delete(rt->last_completed);
    rt->last_completed=cJSON_Duplicate(event,1);
    instrument_trace("capture_completed",rt->current_id,rt->current_gate,rt->boot,str(event,"result"));
    /* Queue before publishing completion so the PC cannot acquire speech first. */
    queue_sound(rt, result->result ? "capture_failed" : result->duration < 3000 ? "capture_short" : "capture_done");
    immediate_completion(rt, event);
    cJSON_Delete(event);
    rt->completion_pending = 0; rt->completion_event[0] = 0;
    rt->recording = 0; rt->current_id[0] = 0; rt->started_at = 0;
    rt->recording_mono=rt->stopped_mono=0;
    COPY(rt->phase, rt->capture_id[0] ? "prepared" : "idle");
    rt->last_maintenance = 0;
    if (rt->revoking) (void)local_unpair(rt);
    cJSON *empty = cJSON_CreateObject();
    if (empty) { (void)v3_save_json(STATE_DIR "/active-v3.json", empty); cJSON_Delete(empty); }
}
static void record_messages(Runtime *rt) {
    if (!rt->record.pid) return;
    WorkerResult result; ssize_t size = recv(rt->record.fd, &result, sizeof(result), MSG_DONTWAIT);
    if (size == 1) {
        if (result.type == 'P') {
            char response = rt->gate && v3_now() < rt->lease && rt->keys == 5 ? 'G' : 'X';
            (void)send(rt->record.fd, &response, 1, MSG_NOSIGNAL);
        } else if (result.type == 'S') {
            rt->recording = 1; rt->started_at = wall_ms();
            rt->recording_mono=v3_now(); rt->stopped_mono=0;
            COPY(rt->phase, "recording"); queue_sound(rt,"capture_start");
            instrument_trace("actual_recording",rt->current_id,rt->current_gate,rt->boot,"");
            syslog(LOG_INFO, "V3 actual recording %s", rt->current_id);
            /* Recording notification is persisted for maintenance worker, replaced at completion. */
            cJSON *event = cJSON_CreateObject();
            cJSON_AddNumberToObject(event, "version", 3);
            cJSON_AddStringToObject(event, "device_id", rt->config->device_id);
            cJSON_AddStringToObject(event, "boot_id", rt->boot);
            cJSON_AddStringToObject(event, "capture_id", rt->current_id);
            cJSON_AddStringToObject(event, "gate_id", rt->current_gate);
            cJSON_AddStringToObject(event, "phase", "recording");
            cJSON_AddNumberToObject(event, "started_at_ms", (double)rt->started_at);
            (void)v3_save_json(STATE_DIR "/recording-v3.json", event); cJSON_Delete(event);
            cJSON *journal = v3_read_json(STATE_DIR "/recording-v3.json");
            if (journal) { (void)v3_save_json(STATE_DIR "/active-v3.json", journal); cJSON_Delete(journal); }
            rt->last_maintenance = 0;
        }
    } else if (size == (ssize_t)sizeof(result) && result.type == 'C') {
        finish_worker(&rt->record);
        if (result.result) { rt->camera_fault=1; disable_gate(rt); }
        completed(rt, &result);
    } else if (size == 0) {
        finish_worker(&rt->record); memset(&result, 0, sizeof(result));
        result.result = -1; result.reason = 2; result.ended_at = wall_ms();
        rt->camera_fault=1; disable_gate(rt); completed(rt, &result);
    }
    int released = rt->keys < 0 || (rt->recording ? (rt->keys & 5) != 5 : rt->keys != 5);
    if (rt->record.pid && released && !rt->stop_sent) {
        char stop = 'T'; (void)send(rt->record.fd, &stop, 1, MSG_NOSIGNAL);
        rt->stop_sent = 1;
        if (rt->recording) {
            rt->stopped_mono=v3_now();
            COPY(rt->phase, "finalizing");
            cJSON *event = v3_read_json(STATE_DIR "/recording-v3.json");
            if (event) {
                cJSON_DeleteItemFromObjectCaseSensitive(event, "acknowledged");
                cJSON_ReplaceItemInObjectCaseSensitive(event, "phase", cJSON_CreateString("finalizing"));
                (void)v3_save_json(STATE_DIR "/recording-v3.json", event); cJSON_Delete(event);
            }
        }
        syslog(LOG_INFO, "V3 stop requested %s", rt->current_id);
    }
}

static void abort_worker(Worker *worker) {
    if (!worker->pid) return;
    /* Commands have isolated groups; stop them before their worker disappears. */
    char children[96]; snprintf(children,sizeof(children),"/proc/%ld/task/%ld/children",(long)worker->pid,(long)worker->pid);
    FILE *list=fopen(children,"r");
    if (list) {
        long child;
        while (fscanf(list,"%ld",&child)==1) if (child>1) (void)kill(-(pid_t)child,SIGKILL);
        fclose(list);
    }
    (void)kill(-worker->pid,SIGKILL); (void)kill(worker->pid,SIGKILL);
    close(worker->fd); worker->fd=-1; worker->pid=0;
}
static void worker_watchdog(Runtime *rt,uint64_t now) {
    struct { Worker *worker; uint64_t limit; const char *name; } workers[]={
        {&rt->record,120000,"record"},{&rt->upload,365000,"upload"},
        {&rt->maintenance,120000,"maintenance"},{&rt->notifier,15000,"notifier"},
        {&rt->audio,15000,"audio"},{&rt->discard,120000,"discard"},
        {&rt->diagnostics,15000,"diagnostics"},{&rt->recovery,15000,"camera_recovery"}
    };
    for (size_t i=0;i<sizeof(workers)/sizeof(workers[0]);i++) {
        Worker *worker=workers[i].worker;
        if (!worker->pid || now-worker->started<workers[i].limit) continue;
        instrument_trace("worker_timeout",rt->current_id,rt->gate_id,rt->boot,workers[i].name);
        abort_worker(worker);
        if (worker==&rt->record) {
            rt->camera_fault=1; disable_gate(rt);
            WorkerResult failed={.result=-1,.reason=2,.duration=rt->recording_mono ? (int)(now-rt->recording_mono) : 0};
            if (failed.duration>60000) failed.duration=60000;
            completed(rt,&failed);
        } else if (worker==&rt->upload) rt->upload_id[0]=0;
        else if (worker==&rt->audio) rt->audio_result=-1;
    }
    if (rt->recovery.pid) {
        WorkerResult result; ssize_t size=recv(rt->recovery.fd,&result,sizeof(result),MSG_DONTWAIT);
        if (size==(ssize_t)sizeof(result) || size==0) {
            finish_worker(&rt->recovery);
            if (size==(ssize_t)sizeof(result) && !result.result) {
                rt->camera_fault=0;
                instrument_trace("camera_recovered","","",rt->boot,"stopped");
            } else rt->recovery_retry=now+5000;
        }
    }
    if (rt->camera_fault && !rt->recovery.pid && now>=rt->recovery_retry) {
        rt->recovery_retry=now+5000;
        int fd; int branch=make_worker(&rt->recovery,&fd);
        if (!branch) {
            child_isolate(rt,fd);
            int status=-1; (void)camera_stop();
            WorkerResult result={.result=camera_status(&status) || status!=0 ? -1 : 0};
            (void)send(fd,&result,sizeof(result),MSG_NOSIGNAL);close(fd);_exit(0);
        }
    }
}

static void maintenance(Runtime *rt, uint64_t now) {
    /* Prompt playback never queues behind network/storage maintenance. */
    if (rt->speech_lease && now >= rt->speech_lease) {
        rt->speech_lease=0; rt->speech_id[0]=0; rt->speech_resume=now+600;
        instrument_trace("speech_expired","","",rt->boot,"");
    }
    if (!rt->audio.pid && now >= rt->speech_lease && now >= rt->speech_resume && rt->sound_in!=rt->sound_out && strcmp(rt->phase,"starting")) {
        char sound[32];COPY(sound,rt->sounds[rt->sound_out%8]);
        int fd;int branch=make_worker(&rt->audio,&fd);
        if (!branch) {
            child_isolate(rt,fd);
            WorkerResult result={.type='P',.result=audio_play(sound)};
            (void)send(fd,&result,sizeof(result),MSG_NOSIGNAL);close(fd);_exit(0);
        }
        if (branch>0) rt->sound_out++;
    }
    if (rt->maintenance.pid) {
        WorkerResult result; ssize_t size = recv(rt->maintenance.fd, &result, sizeof(result), MSG_DONTWAIT);
        if (size == (ssize_t)sizeof(result)) {
            finish_worker(&rt->maintenance);
            if (result.type == 'M') {
                rt->rssi = result.rssi; rt->free_mb = result.free_mb; rt->total_mb = result.total_mb;
                rt->available_mb = result.available_mb; rt->memory_mb = result.memory_mb;
                rt->metrics_at = result.started_at;
                rt->gateway_failures = result.gateway > 0 ? 0 : rt->gateway_failures + 1;
                int transition = strcmp(rt->wifi_state, result.wifi_state);
                int first_wifi = !rt->wifi_seen;
                if (result.gateway > 0) rt->wifi_seen = 1;
                COPY(rt->wifi_state, result.wifi_state);
                char detail[160]; snprintf(detail,sizeof(detail),"gateway=%d wifi=%d failures=%d state=%s initial=%d",result.gateway,result.wifi,rt->gateway_failures,rt->wifi_state,first_wifi);
                instrument_trace("network_snapshot","","",rt->boot,detail);
                if (transition) {
                    if (!strcmp(rt->wifi_state, "recovering")) queue_sound(rt, "wifi_recovering");
                    if (!strcmp(rt->wifi_state, "needs_attention")) queue_sound(rt, "wifi_attention");
                    if (!strcmp(rt->wifi_state, "available")) queue_sound(rt, first_wifi ? "wifi_connected" : "wifi_restored");
                }
                for (int i = 0; i < result.count; i++) {
                    if (discard_intended(result.items[i].id)) continue;
                    char path[384]; snprintf(path, sizeof(path), "%s/%s.json", PENDING_DIR, result.items[i].id);
                    cJSON *value = v3_read_json(path);
                    if (!value) value = cJSON_CreateObject();
                    cJSON_DeleteItemFromObjectCaseSensitive(value, "sha256");
                    cJSON_AddStringToObject(value, "sha256", result.items[i].hash);
                    cJSON_AddNumberToObject(value, "size_bytes", (double)result.items[i].size);
                    (void)v3_save_json(path, value); cJSON_Delete(value);
                }
            } else if (result.type == 'E') {
                rt->event_retry = now + 2000;
                if (result.event[0]) snprintf(rt->event_cursor,sizeof(rt->event_cursor),"%s.json",result.event);
                acknowledge_completion(rt, &result);
            } else if (result.type == 'R' && !result.result) {
                cJSON *event = v3_read_json(STATE_DIR "/recording-v3.json");
                if (event && !strcmp(str(event, "capture_id"), result.event) && !strcmp(str(event, "phase"), result.wifi_state)) {
                    cJSON_AddBoolToObject(event, "acknowledged", 1); (void)v3_save_json(STATE_DIR "/recording-v3.json", event);
                }
                cJSON_Delete(event);
            }
        } else if (size == 0) finish_worker(&rt->maintenance);
        return;
    }
    /* Durable event scans run in the bounded worker, never in the input loop. */
    cJSON *event = NULL;
    char type = 0;
    cJSON *recording_event = NULL;
    if (now >= rt->metric_retry && (!rt->metrics_at || now - rt->metrics_at >= 10000)) type = 'M';
    else if (rt->paired && !rt->notifier.pid && now >= rt->event_retry) type = 'E';
    else if (rt->recording && rt->paired) {
        recording_event = v3_read_json(STATE_DIR "/recording-v3.json");
        if (recording_event && !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(recording_event, "acknowledged"))) type = 'R';
    }

    if (!type) { cJSON_Delete(event); cJSON_Delete(recording_event); return; }
    rt->last_maintenance = now;
    if (type == 'M') rt->metric_retry = now + 10000;
    if (type == 'E') rt->event_retry = now + 2000;
    rt->maintenance_type = type;
    int fd; int branch = make_worker(&rt->maintenance, &fd);
    if (!branch) {
        child_isolate(rt, fd);
        WorkerResult result = {.type = type};
        if (type == 'E') {
            char event_id[ID_SIZE]={0}; event=next_event(rt,event_id);
            result.result = event ? http_v3_status(rt->config, event) : 0;
            COPY(result.event, str(event, "capture_id"));
            cJSON_Delete(event);
        } else if (type == 'R') {
            result.result = http_v3_status(rt->config, recording_event);
            COPY(result.event, str(recording_event, "capture_id"));
            COPY(result.wifi_state, str(recording_event, "phase"));
        }
        else {
            char address[64]; result.wifi = !wifi_address(address, sizeof(address));
            result.gateway = getenv("INSTRUMENT_HOST_TEST") ? 1 : result.wifi ? wifi_gateway_reachable() : 0;
            result.rssi = getenv("INSTRUMENT_HOST_TEST") ? -50 : wifi_rssi(); result.free_mb = storage_free_mb(); result.total_mb = storage_total_mb();
            result.available_mb = result.memory_mb = -1;
            (void)storage_memory_mb(&result.available_mb, &result.memory_mb);
            result.started_at = v3_now();
            int failures = result.gateway > 0 ? 0 : rt->gateway_failures + 1;
            int initializing = !rt->wifi_seen && v3_now() - rt->startup_at < 60000;
            COPY(result.wifi_state, result.gateway > 0 ? "available" : initializing ? "unknown" : failures >= 6 ? "needs_attention" : "recovering");
            if (failures && !initializing && !getenv("INSTRUMENT_HOST_TEST")) {
                instrument_trace("wifi_reconnect","","",rt->boot,failures < 3 ? "reassociate" : "restart_wifi");
                if (failures < 3) {
                    char *const args[] = {"wpa_cli", "-i", "wlan0", "reassociate", NULL};
                    (void)run_command(args);
                } else (void)wifi_reconnect();
            }
            int offset = 0, next = 0;
            while (result.count < MAX_ITEMS && next >= 0) {
                Item items[MAX_ITEMS]; int count = pending_items(offset, MAX_ITEMS, items, &next);
                if (count < 0) break;
                for (int i = 0; i < count && result.count < MAX_ITEMS; i++) if (!items[i].hash[0]) {
                    char path[384]; snprintf(path, sizeof(path), "%s/%.127s.mp4", PENDING_DIR, items[i].id);
                    if (!digest(path, items[i].hash, &items[i].size)) result.items[result.count++] = items[i];
                }
                offset = next;
            }
        }
        (void)send(fd, &result, sizeof(result), MSG_NOSIGNAL); close(fd); _exit(0);
    }
    cJSON_Delete(event); cJSON_Delete(recording_event);
}
static void begin_upload(Runtime *rt, const cJSON *request, cJSON *reply) {
    const char *id = str(request, "capture_id"), *grant = str(request, "grant_id");
    int ttl = (int)num(request, "ttl_ms");
    if (rt->upload.pid || discard_intended(id) || !safe_id(id) || !safe_id(grant) || ttl <= 0 || ttl > 30000 || strlen(str(request, "sha256")) != 64) return;
    char path[384], metadata_path[384];
    snprintf(path, sizeof(path), "%s/%s.mp4", PENDING_DIR, id);
    snprintf(metadata_path, sizeof(metadata_path), "%s/%s.json", PENDING_DIR, id);
    cJSON *metadata = v3_read_json(metadata_path); struct stat info;
    int match = metadata && !strcmp(str(metadata, "result"), "success") && num(metadata, "duration_ms") >= 3000 && !stat(path, &info) && num(request, "size_bytes") == (double)info.st_size &&
                !strcmp(str(request, "sha256"), str(metadata, "sha256"));
    cJSON_Delete(metadata); if (!match) return;
    uint64_t deadline = v3_now() + (unsigned)ttl;
    int fd; int branch = make_worker(&rt->upload, &fd);
    if (branch < 0) return;
    if (!branch) {
        child_isolate(rt, fd);
        WorkerResult result = {.type = 'U', .result = -1};
        char local_hash[65], remote_hash[65]; long long local_size = 0, remote_size = 0; int stored = 0;
        /* Unknown receipt is never permission to resend. Reconcile once per grant. */
        if (v3_now() < deadline && !digest(path, local_hash, &local_size) &&
            local_size == (long long)num(request, "size_bytes") && !strcmp(local_hash, str(request, "sha256")) &&
            !http_file_stored(rt->config, id, &stored, &remote_size, remote_hash, sizeof(remote_hash))) {
            if (!stored && v3_now() < deadline) {
                (void)setenv("INSTRUMENT_UPLOAD_GRANT", grant, 1);
                (void)http_upload(rt->config, id, path);
                stored = 0;
                (void)http_file_stored(rt->config, id, &stored, &remote_size, remote_hash, sizeof(remote_hash));
            }
            if (stored && local_size == remote_size && !strcmp(local_hash, remote_hash)) result.result = 0;
        }
        (void)send(fd, &result, sizeof(result), MSG_NOSIGNAL); close(fd); _exit(0);
    }
    COPY(rt->upload_id, id);
    cJSON_ReplaceItemInObjectCaseSensitive(reply, "ok", cJSON_CreateTrue());
    cJSON_AddStringToObject(reply, "grant_id", grant);
}
static void upload_messages(Runtime *rt) {
    if (!rt->upload.pid) return;
    WorkerResult result; ssize_t size = recv(rt->upload.fd, &result, sizeof(result), MSG_DONTWAIT);
    if (size != (ssize_t)sizeof(result) && size != 0) return;
    finish_worker(&rt->upload);
    if (size == (ssize_t)sizeof(result) && !result.result) {
        char path[384]; snprintf(path, sizeof(path), "%s/%s.mp4", PENDING_DIR, rt->upload_id);
        /* Only this explicitly receipted file may be removed; current capture is independent. */
        if (!unlink(path)) {
            snprintf(path, sizeof(path), "%s/%s.json", PENDING_DIR, rt->upload_id);
            (void)unlink(path); queue_sound(rt, "uploaded");
        }
    } else queue_sound(rt, "saved_on_device");
    rt->upload_id[0] = 0;
}
static int request_unpair(Runtime *rt) {
    if (rt->revoking) return 0;
    rt->revoking = 1;
    if (save_pair(rt)) { rt->revoking = 0; return -1; }
    disable_gate(rt); rt->pair_window = 0;
    if (rt->record.pid) {
        char stop = 'T'; (void)send(rt->record.fd, &stop, 1, MSG_NOSIGNAL); rt->stop_sent = 1;
        if (rt->recording) COPY(rt->phase, "finalizing");
        instrument_trace("unpair_stop", rt->current_id, rt->current_gate, rt->boot, "revoking");
        return 0;
    }
    if (rt->completion_pending) return 0;
    return local_unpair(rt);
}
static int begin_audio(Runtime *rt, const cJSON *request, int test) {
    int volume = (int)num(request, "volume");
    const char *operation = str(request, "operation_id");
    if (!safe_id(operation) || volume < 0 || volume > 30 || num(request, "volume") != volume ||
        rt->gate || rt->record.pid || rt->completion_pending || rt->audio.pid || v3_now()<rt->speech_lease ||
        (rt->maintenance.pid && rt->maintenance_type == 'A') || rt->sound_in != rt->sound_out) return -1;
    if (!strcmp(operation, rt->audio_id)) return 0;
    int fd; int branch = make_worker(&rt->audio, &fd);
    if (branch < 0) return -1;
    if (!branch) {
        child_isolate(rt, fd);
        WorkerResult result = {.type = 'V', .result = -1, .duration = rt->config->volume};
        int applied = audio_set_volume(volume);
        if (!applied) {
            WorkerResult current = {.type='v', .duration=volume};
            (void)send(fd,&current,sizeof(current),MSG_NOSIGNAL);
        }
        if (!applied && test) result.result = audio_play("volume_test");
        else if (!applied) result.result = config_save_volume(rt->config->config_path, volume);
        if (test || result.result) {
            if (audio_set_volume(rt->config->volume)) result.result = -1;
        } else result.duration = volume;
        if (!getenv("INSTRUMENT_HOST_TEST")) result.duration = audio_get_volume();
        (void)send(fd, &result, sizeof(result), MSG_NOSIGNAL); close(fd); _exit(0);
    }
    COPY(rt->audio_id, operation); rt->audio_result = -1;
    instrument_trace(test ? "volume_test" : "volume_set", "", "", rt->boot, operation);
    return 0;
}
static void audio_messages(Runtime *rt) {
    if (!rt->audio.pid) return;
    WorkerResult result; ssize_t size = recv(rt->audio.fd, &result, sizeof(result), MSG_DONTWAIT);
    if (size != (ssize_t)sizeof(result) && size != 0) return;
    if (size == (ssize_t)sizeof(result) && result.type == 'v') { rt->audio_volume=result.duration; return; }
    finish_worker(&rt->audio);
    if (size==(ssize_t)sizeof(result) && result.type=='P') {
        instrument_trace("prompt_complete","","",rt->boot,result.result ? "failed" : "success");
        return;
    }
    if (size == (ssize_t)sizeof(result)) rt->audio_volume = result.duration;
    rt->audio_result = size == (ssize_t)sizeof(result) ? result.result : -1;
    if (size == (ssize_t)sizeof(result) && !result.result) rt->config->volume = result.duration;
    else (void)audio_set_volume(rt->config->volume);
    instrument_trace("audio_complete", "", "", rt->boot, rt->audio_result ? "failed" : "success");
}
static cJSON *base_reply(Runtime *rt, const cJSON *request) {
    cJSON *reply = cJSON_CreateObject();
    if (!reply) return NULL;
    cJSON_AddNumberToObject(reply, "version", 3);
    char op[96]; snprintf(op, sizeof(op), "%s_reply", str(request, "op"));
    cJSON_AddStringToObject(reply, "op", !strcmp(str(request, "op"), "hello_query") ? "hello_reply" : op);
    cJSON_AddStringToObject(reply, "device_id", rt->config->device_id);
    cJSON_AddStringToObject(reply, "host_id", str(request, "host_id"));
    cJSON_AddStringToObject(reply, "boot_id", rt->boot);
    cJSON_AddNumberToObject(reply, "sequence", num(request, "sequence"));
    cJSON_AddStringToObject(reply, "nonce", str(request, "nonce"));
    cJSON_AddBoolToObject(reply, "ok", 0);
    return reply;
}
static void hello_reply(Runtime *rt, cJSON *reply, uint64_t now) {
    cJSON_ReplaceItemInObjectCaseSensitive(reply, "ok", cJSON_CreateTrue());
    cJSON *capabilities = cJSON_AddArrayToObject(reply, "capabilities");
    static const char *const names[] = {"capture_gate", "capture_prepare", "capture_events", "upload_grant", "pair_transaction", "volume_control", "paired_discover", "time_sync", "diagnostics", "capture_discard", "backlight_brightness", "speech_focus"};
    for (size_t i = 0; i < sizeof(names)/sizeof(names[0]); i++) cJSON_AddItemToArray(capabilities, cJSON_CreateString(names[i]));
    cJSON_AddStringToObject(reply, "paired_host_id", rt->revoking ? "" : rt->host);
    cJSON_AddNumberToObject(reply, "volume", rt->audio_volume);
    cJSON_AddNumberToObject(reply, "saved_volume", rt->config->volume);
    cJSON_AddBoolToObject(reply,"speech_focus_active", now < rt->speech_lease);
    cJSON_AddBoolToObject(reply, "audio_busy", now < rt->speech_lease || rt->audio.pid || (rt->maintenance.pid && rt->maintenance_type == 'A') || rt->sound_in != rt->sound_out);
    cJSON_AddStringToObject(reply, "audio_operation_id", rt->audio_id);
    cJSON_AddNumberToObject(reply, "audio_operation_result", rt->audio_result);
    cJSON_AddStringToObject(reply, "uploading_capture_id", rt->upload_id);
    cJSON_AddStringToObject(reply, "peer_ip", rt->peer_ip);
    const char *colon = strrchr(rt->config->pc_base_url, ':');
    cJSON_AddNumberToObject(reply, "http_port", colon ? atoi(colon + 1) : 0);
    cJSON_AddStringToObject(reply, "unpair_event_id", rt->unpair);
    cJSON_AddNumberToObject(reply, "pair_window_remaining_ms", rt->pair_window > now ? (double)(rt->pair_window - now) : 0);
    cJSON_AddStringToObject(reply, "phase", rt->phase);
    cJSON_AddNumberToObject(reply, "utc_ms", (double)wall_ms());
    cJSON_AddBoolToObject(reply, "diagnostics_active", rt->diagnostics_lease > now);
    cJSON_AddStringToObject(reply, "wifi_state", rt->wifi_state);
    cJSON_AddBoolToObject(reply, "gate_enabled", rt->gate && now < rt->lease);
    cJSON_AddNumberToObject(reply,"lease_remaining_ms",rt->gate && now<rt->lease ? (double)(rt->lease-now) : 0);
    cJSON_AddStringToObject(reply,"system_boot_id",rt->system_boot);
    cJSON_AddNumberToObject(reply,"application_pid",getpid());
    cJSON_AddBoolToObject(reply,"camera_fault",rt->camera_fault);
    cJSON *snapshot=cJSON_AddObjectToObject(reply,"capture_snapshot");
    cJSON_AddStringToObject(snapshot,"device_id",rt->config->device_id);
    cJSON_AddStringToObject(snapshot,"boot_id",rt->boot);
    cJSON_AddStringToObject(snapshot,"capture_id",rt->current_id[0] ? rt->current_id : rt->capture_id);
    cJSON_AddStringToObject(snapshot,"gate_id",rt->current_id[0] ? rt->current_gate : rt->gate_id);
    cJSON_AddStringToObject(snapshot,"phase",rt->phase);
    cJSON_AddNumberToObject(snapshot,"started_at_ms",(double)rt->started_at);
    uint64_t end=rt->stopped_mono ? rt->stopped_mono : now;
    cJSON_AddNumberToObject(snapshot,"elapsed_ms",rt->recording_mono && end>=rt->recording_mono ? (double)(end-rt->recording_mono) : 0);
    if (rt->last_completed && rt->paired && !strcmp(str(rt->last_completed,"host_id"),rt->host) && !strcmp(str(reply,"host_id"),rt->host))
        cJSON_AddItemToObject(reply,"last_completed_event",cJSON_Duplicate(rt->last_completed,1));
    cJSON_AddStringToObject(reply, "gate_id", rt->gate_id);
    cJSON_AddStringToObject(reply, "capture_id", rt->record.pid ? rt->current_id : rt->capture_id);
    cJSON_AddNumberToObject(reply, "control_sequence", (double)rt->sequence);
    cJSON_AddNumberToObject(reply, "snapshot_age_ms", rt->metrics_at ? (double)(now - rt->metrics_at) : -1);
    cJSON *metrics = cJSON_AddObjectToObject(reply, "metrics");
    cJSON_AddStringToObject(metrics, "ssid", rt->config->wifi_ssid);
    if (rt->rssi > -100 && rt->rssi < 0) cJSON_AddNumberToObject(metrics, "rssi_dbm", rt->rssi);
    if (rt->free_mb >= 0) cJSON_AddNumberToObject(metrics, "storage_free_mb", rt->free_mb);
    if (rt->total_mb > 0) cJSON_AddNumberToObject(metrics, "storage_total_mb", rt->total_mb);
    if (rt->available_mb >= 0) cJSON_AddNumberToObject(metrics, "memory_available_mb", rt->available_mb);
    if (rt->memory_mb > 0) cJSON_AddNumberToObject(metrics, "memory_total_mb", rt->memory_mb);
    cJSON_AddNumberToObject(metrics, "pending_count", storage_pending_count());
    cJSON_AddStringToObject(metrics, "battery", battery_present() ? "present" : "absent");
}
/* One proof-bound file at a time; durable intent survives partial cleanup. */
static int persist_discard(const char *id, const char *txn, long long expected_size, const char *expected_hash) {
    char receipt_path[384], media[384], hash[65];
    snprintf(receipt_path,sizeof(receipt_path),"%s/discard-%s.json",STATE_DIR,id);
    snprintf(media,sizeof(media),"%s/%s.mp4",PENDING_DIR,id);
    cJSON *receipt=v3_read_json(receipt_path);
    if (receipt) {
        int match=!strcmp(str(receipt,"txn_id"),txn) && !strcmp(str(receipt,"sha256"),expected_hash) && num(receipt,"size_bytes")== (double)expected_size;
        cJSON_Delete(receipt); return match ? 0 : -1;
    }
    struct stat info; long long actual_size;
    if (!lstat(media,&info)) {
        if (!S_ISREG(info.st_mode) || digest(media,hash,&actual_size) || actual_size!=expected_size || strcmp(hash,expected_hash)) return -1;
    } else if (errno!=ENOENT) return -1;
    receipt=cJSON_CreateObject();
    cJSON_AddStringToObject(receipt,"capture_id",id); cJSON_AddStringToObject(receipt,"txn_id",txn);
    cJSON_AddStringToObject(receipt,"sha256",expected_hash); cJSON_AddNumberToObject(receipt,"size_bytes",(double)expected_size);
    cJSON_AddBoolToObject(receipt,"done",0);
    int result=v3_save_json(receipt_path,receipt); cJSON_Delete(receipt); return result;
}
static int discard_file(const char *id, const char *txn, long long size, const char *hash) {
    if (persist_discard(id,txn,size,hash)) return -1;
    char path[384];
    const char *dirs[]={PENDING_DIR,PENDING_DIR,EVENTS_DIR};
    const char *exts[]={"mp4","json","json"};
    for (int i=0;i<3;i++) {
        snprintf(path,sizeof(path),"%s/%s.%s",dirs[i],id,exts[i]);
        if (unlink(path) && errno!=ENOENT) return -1;
    }
    snprintf(path,sizeof(path),"%s/discard-%s.json",STATE_DIR,id);
    cJSON *receipt=v3_read_json(path);
    if (!receipt) return -1;
    cJSON_ReplaceItemInObjectCaseSensitive(receipt,"done",cJSON_CreateTrue());
    int result=v3_save_json(path,receipt); cJSON_Delete(receipt); return result;
}
static void resume_discards(void) {
    DIR *dir=opendir(STATE_DIR); if (!dir) return;
    struct dirent *entry;
    while ((entry=readdir(dir))) {
        if (strncmp(entry->d_name,"discard-",8)) continue;
        char path[384]; snprintf(path,sizeof(path),"%s/%s",STATE_DIR,entry->d_name);
        cJSON *receipt=v3_read_json(path);
        if (receipt && safe_id(str(receipt,"capture_id")) && !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(receipt,"done"))) {
            (void)discard_file(str(receipt,"capture_id"),str(receipt,"txn_id"),(long long)num(receipt,"size_bytes"),str(receipt,"sha256"));
        }
        cJSON_Delete(receipt);
    }
    closedir(dir);
}
static int begin_discard(Runtime *rt, const cJSON *request, cJSON *reply) {
    const char *id=str(request,"capture_id"), *txn=str(request,"txn_id"), *hash=str(request,"sha256");
    double size=num(request,"size_bytes");
    if (!safe_id(id) || !safe_id(txn) || strlen(hash)!=64 || size<0 || size>9007199254740991.0 || size!=(double)(long long)size) return 0;
    for (int i=0;i<64;i++) if (!((hash[i]>='a' && hash[i]<='f') || (hash[i]>='0' && hash[i]<='9'))) return 0;
    if ((rt->record.pid && !strcmp(rt->current_id,id)) || (rt->completion_pending && !strcmp(rt->current_id,id))) { cJSON_AddStringToObject(reply,"error","capture_busy"); return 0; }
    char path[384]; snprintf(path,sizeof(path),"%s/discard-%s.json",STATE_DIR,id);
    cJSON *receipt=v3_read_json(path);
    int matched=receipt && !strcmp(str(receipt,"txn_id"),txn) && !strcmp(str(receipt,"sha256"),hash) && num(receipt,"size_bytes")==size;
    int done=matched && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(receipt,"done"));
    cJSON_Delete(receipt);
    if (done) return 1;
    if (rt->discard.pid) { cJSON_AddStringToObject(reply,"error","busy"); return 0; }
    int fd; int branch=make_worker(&rt->discard,&fd);
    if (branch<0) return 0;
    if (!branch) {
        child_isolate(rt,fd);
        WorkerResult result={.type='D',.result=persist_discard(id,txn,(long long)size,hash)};
        COPY(result.event,id);
        (void)send(fd,&result,sizeof(result),MSG_NOSIGNAL); close(fd); _exit(0);
    }
    cJSON_AddStringToObject(reply,"error","discard_pending");
    return 0;
}
static int speech_focus(Runtime *rt, const cJSON *request, uint64_t now) {
    const char *id=str(request,"speech_id"), *action=str(request,"action");
    int ttl=(int)num(request,"ttl_ms");
    if (!safe_id(id)) return 0;
    if (!strcmp(action,"release")) {
        if (strcmp(id,rt->speech_id)) return 0;
        rt->speech_lease=0; rt->speech_id[0]=0; rt->speech_resume=now+600;
        instrument_trace("speech_released","","",rt->boot,id); return 1;
    }
    if (ttl<=0 || ttl>6000 || num(request,"ttl_ms")!=ttl) return 0;
    if (!strcmp(action,"renew")) {
        if (now>=rt->speech_lease || strcmp(id,rt->speech_id)) return 0;
        rt->speech_lease=now+(unsigned)ttl; return 1;
    }
    if (strcmp(action,"acquire")) return 0;
    if (now<rt->speech_lease) return !strcmp(id,rt->speech_id);
    if (rt->audio.pid || (rt->maintenance.pid && rt->maintenance_type=='A') || rt->sound_in!=rt->sound_out) return 0;
    COPY(rt->speech_id,id); rt->speech_lease=now+(unsigned)ttl;
    instrument_trace("speech_acquired","","",rt->boot,id); return 1;
}
static void receive_udp(Runtime *rt, uint64_t now) {
    for (int packets = 0; packets < 16; packets++) {
        char buffer[PACKET_SIZE + 1]; struct sockaddr_in source = {0}; socklen_t source_length = sizeof(source);
        ssize_t count = recvfrom(rt->udp, buffer, sizeof(buffer), MSG_DONTWAIT, (struct sockaddr *)&source, &source_length);
        if (count < 0) break;
        if (count < 2 || count > PACKET_SIZE) continue;
        buffer[count] = 0;
        cJSON *request = cJSON_ParseWithLength(buffer, (size_t)count + 1);
        if (!request) continue;
        const char *op = str(request, "op");
        const char *nonce = str(request, "nonce");
        int version = (int)num(request, "version");
        char address[64];
        if (!inet_ntop(AF_INET, &source.sin_addr, address, sizeof(address))) { cJSON_Delete(request); continue; }
        if (version == 2 && !strcmp(op, "discover") && safe_id(nonce)) {
            if (!instrument_discoverable(rt->paired, rt->revoking, now, rt->pair_window)) { cJSON_Delete(request); continue; }
            cJSON *reply = cJSON_CreateObject();
            cJSON_AddNumberToObject(reply, "version", 2); cJSON_AddStringToObject(reply, "op", "here");
            cJSON_AddStringToObject(reply, "nonce", nonce); cJSON_AddStringToObject(reply, "device_id", rt->config->device_id);
            cJSON_AddStringToObject(reply, "firmware", INSTRUMENT_VERSION);
            if (cJSON_PrintPreallocated(reply, buffer, sizeof(buffer), 0))
                (void)sendto(rt->udp, buffer, strlen(buffer), 0, (struct sockaddr *)&source, source_length);
            cJSON_Delete(reply); cJSON_Delete(request); continue;
        }
        double sequence = num(request, "sequence");
        if (version != 3 || strcmp(str(request, "device_id"), rt->config->device_id) ||
            !safe_id(str(request, "host_id")) || !safe_id(nonce) || sequence < 0 || sequence > 9007199254740991.0 || sequence != (double)(uint64_t)sequence) {
            cJSON_Delete(request); continue;
        }
        cJSON *reply = base_reply(rt, request);
        if (!reply) { cJSON_Delete(request); continue; }
        int mutation = strcmp(op, "hello_query") && strcmp(op, "pending_query") && strcmp(op, "paired_discover");
        int success = 0;
        if (!strcmp(op, "hello_query")) {
            if (rt->paired && !strcmp(rt->host,str(request,"host_id")) && !strcmp(rt->peer_ip,address)) rt->last_host_contact=now;
            hello_reply(rt, reply, now);
        }
        else if (!strcmp(op, "paired_discover") && rt->paired && !rt->revoking &&
                 !strcmp(rt->host, str(request, "host_id")) && !strcmp(rt->txn, str(request, "txn_id"))) {
            success = 1; cJSON_AddStringToObject(reply, "txn_id", rt->txn);
        }
        else if (strcmp(str(request, "boot_id"), rt->boot)) cJSON_AddStringToObject(reply, "error", "boot_mismatch");
        else if (mutation && sequence == (double)rt->sequence && !strcmp(nonce, rt->last_nonce) && !strcmp(op, rt->last_op) && rt->last_reply[0]) {
            (void)sendto(rt->udp, rt->last_reply, strlen(rt->last_reply), 0, (struct sockaddr *)&source, source_length);
            cJSON_Delete(reply); cJSON_Delete(request); continue;
        } else if (mutation && sequence <= (double)rt->sequence) cJSON_AddStringToObject(reply, "error", "stale_sequence");
        else if (rt->revoking) cJSON_AddStringToObject(reply, "error", "revoking");
        else if (!strcmp(op, "pair_request")) {
            const char *txn = str(request, "txn_id"); int port = (int)num(request, "http_port");
            if (rt->paired && !strcmp(rt->host, str(request, "host_id")) && !strcmp(rt->txn, txn) && port > 0 && port <= 65535) {
                COPY(rt->candidate, rt->host); COPY(rt->candidate_ip, address);
                rt->candidate_port = port; rt->txn_deadline = now + 60000;
                cJSON_AddStringToObject(reply, "txn_id", txn); success = 1;
            } else if (!rt->paired && !rt->record.pid && now < rt->pair_window && safe_id(txn) && port > 0 && port <= 65535 &&
                (!rt->candidate[0] || (!strcmp(rt->candidate, str(request, "host_id")) && !strcmp(rt->txn, txn)))) {
                COPY(rt->candidate, str(request, "host_id")); COPY(rt->candidate_ip, address); COPY(rt->txn, txn);
                rt->candidate_port = port; rt->txn_deadline = rt->pair_window;
                cJSON_AddStringToObject(reply, "txn_id", txn); success = 1;
            } else cJSON_AddStringToObject(reply, "error", "pair_window_closed_or_busy");
        } else if (!strcmp(op, "pair_commit")) {
            const char *txn = str(request, "txn_id");
            if (rt->paired && !strcmp(rt->host, str(request, "host_id")) && !strcmp(rt->txn, txn)) {
                if (rt->candidate[0] && now < rt->txn_deadline && !strcmp(rt->candidate_ip, address)) {
                    COPY(rt->peer_ip, address);
                    snprintf(rt->config->pc_base_url, sizeof(rt->config->pc_base_url), "http://%s:%d", address, rt->candidate_port);
                    success = !save_pair(rt);
                    if (success) rt->candidate[0] = 0;
                } else success = !strcmp(rt->peer_ip, address);
            }
            else if (!rt->paired && now < rt->txn_deadline && !strcmp(rt->candidate, str(request, "host_id")) &&
                     !strcmp(rt->candidate_ip, address) && !strcmp(rt->txn, txn)) {
                COPY(rt->host, rt->candidate); COPY(rt->peer_ip, address);
                snprintf(rt->config->pc_base_url, sizeof(rt->config->pc_base_url), "http://%s:%d", address, rt->candidate_port);
                char old_unpair[ID_SIZE], old_host[ID_SIZE];
                COPY(old_unpair, rt->unpair); COPY(old_host, rt->revoked_host);
                rt->unpair[0] = 0; rt->revoked_host[0] = 0;
                if (!save_pair(rt)) {
                    rt->paired = 1; rt->pair_window = 0; rt->candidate[0] = 0; success = 1;
                    queue_sound(rt, "pair_success"); light_update(1);
                } else { rt->host[0] = 0; COPY(rt->unpair, old_unpair); COPY(rt->revoked_host, old_host); }
            }
            cJSON_AddStringToObject(reply, "txn_id", txn);
        } else if (!strcmp(op, "event_ack") && rt->unpair[0] && !strcmp(str(request, "event_id"), rt->unpair) && !strcmp(address, rt->peer_ip) && !strcmp(str(request, "host_id"), rt->revoked_host)) {
            char previous[ID_SIZE]; COPY(previous, rt->unpair); rt->unpair[0] = 0;
            if (!save_pair(rt)) success = 1; else COPY(rt->unpair, previous);
            cJSON_AddStringToObject(reply, "event_id", str(request, "event_id"));
        } else if (!rt->paired || strcmp(rt->host, str(request, "host_id")) || strcmp(rt->peer_ip, address)) {
            cJSON_AddStringToObject(reply, "error", "host_not_paired");
        } else if (!strcmp(op, "capture_discard")) {
            success = begin_discard(rt, request, reply);
        } else if (!strcmp(op, "time_sync")) {
            double utc = num(request, "utc_ms");
            if (!rt->record.pid && !rt->completion_pending && utc >= 1577836800000.0 && utc < 4102444800000.0) {
                struct timeval tv = {.tv_sec = (time_t)(utc / 1000), .tv_usec = (suseconds_t)((uint64_t)utc % 1000) * 1000};
                struct tm tm; struct rtc_time rtc;
                if (gmtime_r(&tv.tv_sec, &tm)) {
                    memset(&rtc,0,sizeof(rtc)); rtc.tm_sec=tm.tm_sec; rtc.tm_min=tm.tm_min; rtc.tm_hour=tm.tm_hour;
                    rtc.tm_mday=tm.tm_mday; rtc.tm_mon=tm.tm_mon; rtc.tm_year=tm.tm_year;
                    int fd = open("/dev/rtc0", O_RDWR|O_CLOEXEC);
                    if (!settimeofday(&tv,NULL) && fd >= 0 && !ioctl(fd,RTC_SET_TIME,&rtc)) success=1;
                    if(fd >= 0) close(fd);
                }
            } else cJSON_AddStringToObject(reply,"error","busy_or_invalid_time");
            cJSON_AddNumberToObject(reply,"utc_ms",(double)wall_ms());
        } else if (!strcmp(op, "diagnostics_backlight")) {
            double percent=num(request,"brightness");
            if (now < rt->diagnostics_lease && !strcmp(rt->diagnostics_id,str(request,"diagnostics_id")) &&
                percent >= 0 && percent <= 100 && percent == (int)percent) {
                if (!platform_backlight_percent((int)percent)) {
                    cJSON *setting=cJSON_CreateObject();
                    if (setting) {
                        cJSON_AddNumberToObject(setting,"brightness",percent);
                        if (!v3_save_json(STATE_DIR "/backlight.json",setting)) {
                            rt->backlight_brightness=rt->diagnostics_brightness=(int)percent;
                            platform_backlight_configure((int)percent); success=1;
                        } else (void)platform_backlight_percent(rt->diagnostics_brightness);
                        cJSON_Delete(setting);
                    } else (void)platform_backlight_percent(rt->diagnostics_brightness);
                }
            }
            if (!success) cJSON_AddStringToObject(reply,"error","backlight_unavailable_or_invalid");
            cJSON_AddNumberToObject(reply,"brightness",rt->diagnostics_brightness);
        } else if (!strcmp(op, "diagnostics_start")) {
            const char *id = str(request,"diagnostics_id"); int ttl=(int)num(request,"ttl_ms");
            if (!rt->gate && !rt->record.pid && !rt->completion_pending && !rt->upload.pid && !rt->diagnostics.pid && safe_id(id) && ttl>0 && ttl<=6000) {
                rt->diagnostics_brightness=0; COPY(rt->diagnostics_id,id); rt->diagnostics_lease=now+(unsigned)ttl; rt->diagnostics_next=now;
                rt->hold_started=rt->middle_hold=0; rt->release_required=1; success=1;
            }
        } else if (!strcmp(op,"diagnostics_renew") || !strcmp(op,"diagnostics_status") || !strcmp(op,"diagnostics_end")) {
            int ttl=(int)num(request,"ttl_ms");
            if (!strcmp(rt->diagnostics_id,str(request,"diagnostics_id")) && now < rt->diagnostics_lease) {
                if (!strcmp(op,"diagnostics_end")) {rt->diagnostics_lease=0; rt->release_required=1; success=1;}
                else if (!strcmp(op,"diagnostics_status")) success=1;
                else if (ttl>0 && ttl<=6000) {rt->diagnostics_lease=now+(unsigned)ttl; success=1;}
            }
            cJSON_AddNumberToObject(reply,"brightness",rt->diagnostics_brightness);
            cJSON_AddNumberToObject(reply,"key_mask",rt->keys);
            cJSON_AddNumberToObject(reply,"remaining_ms",rt->diagnostics_lease>now ? (double)(rt->diagnostics_lease-now) : 0);
        } else if (!strcmp(op, "unpair")) {
            success = !request_unpair(rt);
            cJSON_AddStringToObject(reply, "event_id", rt->unpair);
        } else if (!strcmp(op,"speech_focus")) {
            success=speech_focus(rt,request,now);
            cJSON_AddStringToObject(reply,"speech_id",str(request,"speech_id"));
            cJSON_AddNumberToObject(reply,"remaining_ms",rt->speech_lease>now ? (double)(rt->speech_lease-now) : 0);
        } else if (!strcmp(op, "capture_disable")) {
            if (!strcmp(rt->gate_id, str(request, "gate_id"))) { disable_gate(rt); success = 1; }
            cJSON_AddStringToObject(reply, "phase", rt->phase);
        } else if (!strcmp(op, "capture_enable")) {
            const char *gate = str(request, "gate_id"); int ttl = (int)num(request, "ttl_ms");
            if (!rt->camera_fault && now >= rt->diagnostics_lease && !rt->diagnostics.pid && !rt->record.pid && !rt->completion_pending && safe_id(gate) && ttl > 0 && ttl <= 6000 && strcmp(gate, rt->gate_id)) {
                COPY(rt->gate_id, gate); rt->gate = 1; rt->lease = now + (unsigned)ttl;
                rt->capture_id[0] = 0; queue_sound(rt, "capture_enabled"); success = 1;
            }
        } else if (!strcmp(op, "capture_renew")) {
            int ttl = (int)num(request, "ttl_ms");
            if (rt->gate && now < rt->lease && ttl > 0 && ttl <= 6000 && !strcmp(rt->gate_id, str(request, "gate_id"))) {
                rt->lease = now + (unsigned)ttl; success = 1;
            }
        } else if (!strcmp(op, "capture_prepare")) {
            const char *id = str(request, "capture_id");
            char path[384]; snprintf(path, sizeof(path), "%s/%s.json", EVENTS_DIR, safe_id(id) ? id : "invalid");
            if (rt->gate && now < rt->lease && !rt->record.pid && !rt->completion_pending && safe_id(id) && !discard_intended(id) &&
                !strcmp(rt->gate_id, str(request, "gate_id")) && access(path, F_OK) &&
                (!rt->capture_id[0] || !strcmp(rt->capture_id, id))) {
                COPY(rt->capture_id, id); COPY(rt->phase, "prepared"); success = 1;
            }
            cJSON_AddStringToObject(reply, "capture_id", id);
        } else if (!strcmp(op, "volume_set") || !strcmp(op, "volume_test")) {
            success = !begin_audio(rt, request, !strcmp(op, "volume_test"));
        } else if (!strcmp(op, "pending_query")) {
            int offset = (int)num(request, "offset"), limit = (int)num(request, "limit"), next;
            if (offset >= 0 && limit > 0 && limit <= MAX_ITEMS) {
                Item items[MAX_ITEMS]; int count_items = pending_items(offset, limit, items, &next);
                if (count_items >= 0) {
                    cJSON *list = cJSON_AddArrayToObject(reply, "items");
                    for (int i = 0; i < count_items; i++) {
                        cJSON *item = cJSON_CreateObject();
                        cJSON_AddStringToObject(item, "capture_id", items[i].id);
                        cJSON_AddNumberToObject(item, "size_bytes", (double)items[i].size);
                        cJSON_AddStringToObject(item, "sha256", items[i].hash);
                        cJSON_AddNumberToObject(item, "duration_ms", items[i].duration);
                        cJSON_AddStringToObject(item, "result", items[i].result);
                        cJSON_AddNumberToObject(item, "started_at_ms", (double)items[i].started_at);
                        cJSON_AddItemToArray(list, item);
                    }
                    cJSON_AddNumberToObject(reply, "next_offset", next); success = 1;
                }
            }
        } else if (!strcmp(op, "upload_grant")) begin_upload(rt, request, reply);
        else cJSON_AddStringToObject(reply, "error", "unknown_op");
        if (success) cJSON_ReplaceItemInObjectCaseSensitive(reply, "ok", cJSON_CreateTrue());
        char endpoint[96]; snprintf(endpoint,sizeof(endpoint),"%s:%u",address,(unsigned)ntohs(source.sin_port));
        instrument_trace_protocol(request,reply,rt->boot,endpoint);
        if (mutation && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(reply, "ok"))) {
            rt->sequence = (uint64_t)sequence; COPY(rt->last_nonce, nonce); COPY(rt->last_op, op);
        }
        if (cJSON_PrintPreallocated(reply, buffer, sizeof(buffer), 0)) {
            if (mutation && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(reply, "ok"))) COPY(rt->last_reply, buffer);
            (void)sendto(rt->udp, buffer, strlen(buffer), 0, (struct sockaddr *)&source, source_length);
        }
        cJSON_Delete(reply); cJSON_Delete(request);
    }
}
static void discard_messages(Runtime *rt) {
    if (!rt->discard.pid) return;
    WorkerResult result; ssize_t size=recv(rt->discard.fd,&result,sizeof(result),MSG_DONTWAIT);
    if (size==(ssize_t)sizeof(result) || size==0) finish_worker(&rt->discard);
    if (size==(ssize_t)sizeof(result) && !result.result && safe_id(result.event)) {
        if (rt->upload.pid && !strcmp(rt->upload_id,result.event)) {
            /* The worker inherits a SIGTERM handler; SIGKILL reliably stops binary IO. */
            (void)kill(rt->upload.pid,SIGKILL);
            while (waitpid(rt->upload.pid,NULL,0)<0 && errno==EINTR) {}
            finish_worker(&rt->upload); rt->upload_id[0]=0;
        }
        char path[384]; snprintf(path,sizeof(path),"%s/discard-%s.json",STATE_DIR,result.event);
        cJSON *receipt=v3_read_json(path);
        if (receipt) (void)discard_file(result.event,str(receipt,"txn_id"),(long long)num(receipt,"size_bytes"),str(receipt,"sha256"));
        cJSON_Delete(receipt);
    }
}
static void diagnostics_tick(Runtime *rt, uint64_t now) {
    if (rt->diagnostics.pid) {
        WorkerResult result; ssize_t size=recv(rt->diagnostics.fd,&result,sizeof(result),MSG_DONTWAIT);
        if (size==(ssize_t)sizeof(result) || size==0) {
            finish_worker(&rt->diagnostics);
            instrument_trace("diagnostics_frame","","",rt->boot,size==(ssize_t)sizeof(result) && !result.result ? "success" : "failed");
        }
        return;
    }
    if (now >= rt->diagnostics_lease || now < rt->diagnostics_next) return;
    rt->diagnostics_next=now+1000;
    int fd; int branch=make_worker(&rt->diagnostics,&fd);
    if (!branch) {
        child_isolate(rt,fd);
        WorkerResult result={.result=camera_diagnostic_frame(rt->config,rt->diagnostics_id)};
        (void)send(fd,&result,sizeof(result),MSG_NOSIGNAL); close(fd); _exit(0);
    }
}
static void close_pair_window(Runtime *rt, const char *reason) {
    rt->pair_window=0; rt->candidate[0]=0; rt->txn_deadline=0;
    queue_sound(rt,"pair_window_closed");
    instrument_trace("pair_window_closed","","",rt->boot,reason);
}
int runtime_run(Config *config) {
    Runtime rt = {.config = config, .audio_volume = config->volume, .keys = -1, .release_required = 1, .rssi = -100,
                  .free_mb = -1, .total_mb = -1, .available_mb = -1, .memory_mb = -1};
    cJSON *backlight=v3_read_json(STATE_DIR "/backlight.json");
    double brightness=num(backlight,"brightness");
    rt.backlight_brightness=brightness >= 0 && brightness <= 100 && brightness == (int)brightness ? (int)brightness : 100;
    platform_backlight_configure(rt.backlight_brightness); cJSON_Delete(backlight);
    rt.startup_at = v3_now();
    FILE *system_boot=fopen("/proc/sys/kernel/random/boot_id","r");
    if (system_boot) { if (!fgets(rt.system_boot,sizeof(rt.system_boot),system_boot)) rt.system_boot[0]=0; fclose(system_boot); rt.system_boot[strcspn(rt.system_boot,"\r\n")]=0; }
    COPY(rt.phase, "idle"); COPY(rt.wifi_state, "unknown");
    if (mkdir(EVENTS_DIR, 0700) && errno != EEXIST) return 1;
    resume_discards();
    if (storage_capture_id(config, rt.boot, sizeof(rt.boot))) return 1;
    cJSON *pair = v3_read_json(STATE_DIR "/pair-v3.json");
    if (pair && safe_id(str(pair, "host_id"))) { COPY(rt.host, str(pair, "host_id")); rt.paired = 1; }
    if (pair) {
        COPY(rt.revoked_host, str(pair, "revoked_host_id")); COPY(rt.txn, str(pair, "txn_id")); COPY(rt.unpair, str(pair, "unpair_event_id"));
        COPY(rt.peer_ip, str(pair, "peer_ip"));
        if (!strncmp(str(pair, "pc_base_url"), "http://", 7)) COPY(config->pc_base_url, str(pair, "pc_base_url"));
    }
    rt.revoking = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(pair, "revoking"));
    cJSON_Delete(pair);
    cJSON *interrupted = v3_read_json(STATE_DIR "/active-v3.json");
    if (interrupted && safe_id(str(interrupted, "capture_id"))) {
        rt.camera_fault=1; /* Recorder death does not prove the camera daemon stopped. */
        COPY(rt.current_id, str(interrupted, "capture_id")); COPY(rt.current_gate, str(interrupted, "gate_id"));
        rt.started_at = num(interrupted, "started_at_ms") > 0 ? (uint64_t)num(interrupted, "started_at_ms") : 0;
        WorkerResult result = {.result = -1, .reason = 2, .ended_at = wall_ms()};
        if (cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(interrupted,"duration_ms")) &&
            num(interrupted,"duration_ms")>=0 && num(interrupted,"duration_ms")<=60000 &&
            (!strcmp(str(interrupted,"result"),"success") || !strcmp(str(interrupted,"result"),"short") || !strcmp(str(interrupted,"result"),"failed"))) {
            result.duration = (int)num(interrupted, "duration_ms");
            result.result = strcmp(str(interrupted, "result"), "failed") ? 0 : -1;
            result.reason = !strcmp(str(interrupted, "stop_reason"), "released") ? 0 : !strcmp(str(interrupted, "stop_reason"), "limit") ? 1 : 2;
            if (safe_id(str(interrupted, "event_id"))) COPY(rt.completion_event, str(interrupted, "event_id"));
        }
        completed(&rt, &result);
    }
    cJSON_Delete(interrupted);
    if (rt.revoking && !rt.completion_pending) (void)local_unpair(&rt);
    rt.udp = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (rt.udp < 0) return 1;
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(DISCOVER_PORT), .sin_addr.s_addr = htonl(INADDR_ANY)};
    int yes = 1; (void)setsockopt(rt.udp, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (bind(rt.udp, (struct sockaddr *)&address, sizeof(address))) { close(rt.udp); return 1; }
    signal(SIGINT, halt); signal(SIGTERM, halt); signal(SIGPIPE, SIG_IGN);
    light_update(rt.paired);
    queue_sound(&rt,"boot_ready");
    char startup[256];
    snprintf(startup,sizeof(startup),"{\"system_boot_id\":\"%s\",\"pid\":%ld,\"version\":\"%s\"}",rt.system_boot,(long)getpid(),INSTRUMENT_VERSION);
    instrument_trace("application_started","","",rt.boot,startup);
    syslog(LOG_INFO, "V3 coordinator boot=%s, gate disabled, buttons gpio3/gpio4/gpio0", rt.boot);
    while (active) {
        uint64_t now = v3_now();
        if (rt.pair_window && now >= rt.pair_window) close_pair_window(&rt,"timeout");
        int previous_keys = rt.keys;
        (void)buttons_snapshot((uint32_t)now, &rt.keys);
        if (rt.keys != previous_keys) {
            char detail[48]; snprintf(detail,sizeof(detail),"{\"key_mask\":%d}",rt.keys);
            instrument_trace("buttons",rt.current_id,rt.gate_id,rt.boot,detail);
        }
        if (rt.gate && now >= rt.lease) {
            instrument_trace("gate_disabled",rt.current_id,rt.gate_id,rt.boot,"lease_expired");
            disable_gate(&rt);
        }
        worker_watchdog(&rt,now);
        if (rt.diagnostics_lease && now >= rt.diagnostics_lease) {
            rt.diagnostics_lease=0; rt.release_required=1; rt.middle_fired=1;
            rt.hold_started=rt.middle_hold=0;
            instrument_trace("diagnostics_expired","","",rt.boot,"");
        }
        if (now < rt.diagnostics_lease) {
            rt.hold_started=rt.middle_hold=0; rt.middle_fired=1; rt.release_required=1;
        } else if (rt.keys < 0) { rt.middle_hold = 0; rt.middle_fired = 1; rt.hold_started = 0; }
        else if (rt.keys & 2) {
            rt.hold_started = 0;
            if (!rt.middle_hold) rt.middle_hold = now;
            if (!rt.middle_fired && now - rt.middle_hold >= 3000) {
                rt.middle_fired = 1; rt.release_required = 1;
                const char *action = "ignored";
                if (rt.paired) action = request_unpair(&rt) ? "unpair_failed" : "unpair";
                else if (!rt.revoking && !rt.record.pid && !rt.completion_pending) {
                    if (rt.pair_window > now) {
                        close_pair_window(&rt,"manual"); action="pair_window_closed";
                    } else {
                        rt.pair_window = now + 45000; rt.candidate[0] = 0; queue_sound(&rt, "pair_window");
                        action = "pair_window";
                    }
                }
                instrument_trace("middle_long_press", rt.current_id, rt.gate_id, rt.boot, action);
            }
        } else {
            rt.middle_hold = 0; rt.middle_fired = 0;
            if (!rt.keys) rt.release_required = 0;
            if (rt.keys != 5) rt.hold_started = 0;
            else {
                if (!rt.hold_started) rt.hold_started = now;
                if (instrument_can_start(rt.release_required, rt.gate && now < rt.lease,
                    rt.capture_id[0] != 0, rt.camera_fault || rt.record.pid || rt.completion_pending || rt.notifier.pid, now - rt.hold_started)) start_record(&rt);
            }
        }
        diagnostics_tick(&rt,now);
        record_messages(&rt);
        if (rt.completion_pending && !rt.record.pid && now >= rt.completion_retry) {
            WorkerResult retry = rt.completion; completed(&rt, &retry);
        }
        uint64_t before_workers=v3_now();
        discard_messages(&rt); notifier_messages(&rt); upload_messages(&rt); audio_messages(&rt);
        uint64_t before_udp=v3_now(); receive_udp(&rt, now);
        uint64_t before_maintenance=v3_now(); maintenance(&rt, now);
        uint64_t before_lights=v3_now();
        int connected=rt.paired && rt.last_host_contact && now-rt.last_host_contact<15000;
        int light_mode=light_status_mode(rt.pair_window > now && !rt.paired && !rt.revoking,
            rt.paired,connected,rt.keys<0 || !strcmp(rt.wifi_state,"needs_attention"),0);
        int light_result=light_status_tick(light_mode,(long long)now);
        if (now < rt.diagnostics_lease) (void)platform_backlight_percent(rt.diagnostics_brightness);
        else (void)backlight_tick(now, rt.keys, rt.gate && now < rt.lease && !rt.revoking);
        static int previous_light_result=-2, previous_light_mode=-1;
        if (light_result!=previous_light_result || light_mode!=previous_light_mode) {
            instrument_trace("status_light","","",rt.boot,light_result ? "unavailable" : light_mode==LIGHT_PAIRING ? "breathe" : light_mode==LIGHT_DISCONNECTED ? "slow" : light_mode==LIGHT_FAULT ? "fast" : "steady");
            previous_light_result=light_result; previous_light_mode=light_mode;
        }
        uint64_t loop_end=v3_now();
        if (loop_end-now>100) {
            char detail[256]; snprintf(detail,sizeof(detail),"{\"total_ms\":%llu,\"control_ms\":%llu,\"workers_ms\":%llu,\"udp_ms\":%llu,\"maintenance_ms\":%llu,\"lights_ms\":%llu}",
                (unsigned long long)(loop_end-now),(unsigned long long)(before_workers-now),(unsigned long long)(before_udp-before_workers),(unsigned long long)(before_maintenance-before_udp),(unsigned long long)(before_lights-before_maintenance),(unsigned long long)(loop_end-before_lights));
            instrument_trace("slow_loop",rt.current_id,rt.gate_id,rt.boot,detail);
        }
        /* Every child is bounded; reap send-then-exit races without blocking input. */
        while (waitpid(-1, NULL, WNOHANG) > 0) {}
        struct pollfd input = {.fd = rt.udp, .events = POLLIN};
        (void)poll(&input, 1, 10);
    }
    disable_gate(&rt);
    if (rt.record.pid) {
        char stop = 'T'; (void)send(rt.record.fd, &stop, 1, MSG_NOSIGNAL);
        while (rt.record.pid) {
            worker_watchdog(&rt,v3_now()); record_messages(&rt);
            struct pollfd input = {.fd = rt.record.fd, .events = POLLIN}; (void)poll(&input, 1, 10);
        }
    }
    buttons_close();
    instrument_trace("application_stopping","","",rt.boot,"signal");
    cJSON_Delete(rt.last_completed);
    if (rt.recovery.pid) abort_worker(&rt.recovery);
    if (rt.diagnostics.pid) abort_worker(&rt.diagnostics);
    if (rt.notifier.pid) abort_worker(&rt.notifier);
    if (rt.upload.pid) abort_worker(&rt.upload);
    if (rt.maintenance.pid) abort_worker(&rt.maintenance);
    if (rt.discard.pid) abort_worker(&rt.discard);
    if (rt.audio.pid) abort_worker(&rt.audio);
    light_status_close();
    close(rt.udp); return 0;
}
