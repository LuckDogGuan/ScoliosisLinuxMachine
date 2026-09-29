#define main instrument_main
#include "../src/app/app_controller.c"
#undef main

int main(int argc, char **argv) {
    if (argc != 5) return 2;
    Config config = {0};
    snprintf(config.device_id, sizeof(config.device_id), "CAM-000123");
    snprintf(config.pc_base_url, sizeof(config.pc_base_url), "%s", argv[1]);
    config.width = 1280;
    config.height = 1080;
    config.fps = 30;
    config.duration_ms = 10000;
    if (!strcmp(argv[4], "sync")) return upload_and_remove(&config, argv[2], argv[3]) ? 1 : 0;
    if (strcmp(argv[4], "async")) return 2;
    struct timespec before, after;
    clock_gettime(CLOCK_MONOTONIC, &before);
    spawn_upload(&config, argv[2], argv[3]);
    clock_gettime(CLOCK_MONOTONIC, &after);
    long long elapsed_ns = (long long)(after.tv_sec - before.tv_sec) * 1000000000LL +
                           after.tv_nsec - before.tv_nsec;
    if (upload_pid <= 0 || elapsed_ns > 200000000LL) return 3;
    usleep(200000);
    int status;
    if (waitpid(upload_pid, &status, WNOHANG) != 0) return 4;
    if (waitpid(upload_pid, &status, 0) != upload_pid) return 5;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 6;
}
