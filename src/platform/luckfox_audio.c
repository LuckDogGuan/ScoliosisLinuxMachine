#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

static const char *const sounds[] = {
    "capture_done", "uploaded", "saved_on_device", "storage_low", "capture_failed",
    "please_connect", "pair_success", "pair_failed", "wifi_connected",
    "wifi_disconnected", "pair_disconnected", "capture_start", "capture_short",
    "capture_enabled", "capture_disabled", "wifi_recovering", "wifi_attention", "wifi_restored", "pair_window", "volume_test", "pair_window_closed", "boot_ready"
};
static time_t last_played[sizeof(sounds) / sizeof(sounds[0])];

int audio_set_volume(int volume) {
    if (getenv("INSTRUMENT_HOST_TEST")) return getenv("INSTRUMENT_AUDIO_FAIL") ? -1 : 0;
    char number[16];
    snprintf(number, sizeof(number), "%d", volume);
    char *const args[] = {"amixer", "-c", "0", "cset", "name=DAC LINEOUT Volume", number, NULL};
    if (volume < 0 || volume > 30 || run_command(args)) return -1;
    int current = audio_get_volume();
    return current == volume ? 0 : -1;
}

int audio_play(const char *name) {
    if (getenv("INSTRUMENT_HOST_TEST")) return 0;
    size_t index;
    for (index = 0; index < sizeof(sounds) / sizeof(sounds[0]); index++)
        if (!strcmp(name, sounds[index])) break;
    if (index == sizeof(sounds) / sizeof(sounds[0])) return -1;
    struct timespec clock_time;
    if (clock_gettime(CLOCK_MONOTONIC, &clock_time)) return -1;
    time_t now = clock_time.tv_sec;
    if (getenv("INSTRUMENT_LEGACY") && last_played[index] && now - last_played[index] < 30) return 0;
    char path[192];
    int n = snprintf(path, sizeof(path), "%s/%s.wav", SOUND_DIR, name);
    if (n < 0 || (size_t)n >= sizeof(path)) return -1;
    if (access(path, R_OK)) {
        n = snprintf(path, sizeof(path), "%s/%s.wav", LEGACY_SOUND_DIR, name);
        if (n < 0 || (size_t)n >= sizeof(path) || access(path, R_OK)) return -1;
    }
    char *const args[] = {"aplay", "-q", "-D", "plughw:0,0", path, NULL};
    int result = run_command_timeout(args,8000);
    if (!result) last_played[index] = now;
    return result;
}

int audio_get_volume(void) {
    int pipes[2]; if (pipe(pipes)) return -1;
    pid_t child = fork();
    if (child < 0) { close(pipes[0]); close(pipes[1]); return -1; }
    if (!child) {
        close(pipes[0]); if (dup2(pipes[1], STDOUT_FILENO) < 0) _exit(1);
        close(pipes[1]); execlp("amixer", "amixer", "-c", "0", "cget", "name=DAC LINEOUT Volume", (char *)NULL); _exit(1);
    }
    close(pipes[1]); char buffer[4096]; size_t at = 0;
    while (at < sizeof(buffer)-1) { ssize_t n = read(pipes[0], buffer+at, sizeof(buffer)-1-at); if (n <= 0) break; at += (size_t)n; }
    close(pipes[0]); int status; while (waitpid(child, &status, 0) < 0) if (errno != EINTR) return -1;
    buffer[at] = 0; char *values = strstr(buffer, ": values=");
    return WIFEXITED(status) && !WEXITSTATUS(status) && values ? atoi(values+9) : -1;
}
