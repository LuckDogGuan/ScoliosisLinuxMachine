#include "v3.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <syslog.h>
int legacy_run(Config config);
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--version")) {
        puts(INSTRUMENT_VERSION);
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "--save-volume")) {
        int vol = atoi(argv[3]);
        return config_save_volume(argv[2], vol) ? 1 : 0;
    }
    const char *config_path = argc > 1 && argv[1][0] != '-' ? argv[1] :
                              INSTRUMENT_ROOT "/config.json";
    if (argc == 3 && !strcmp(argv[1], "--check-config")) config_path = argv[2];
    if (argc == 3 && !strcmp(argv[1], "--hello")) config_path = argv[2];
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
    if (argc == 3 && !strcmp(argv[1], "--hello")) {
        int paired = 0;
        char pair_event[128] = {0}, unpair_event[128] = {0};
        char ip[INET_ADDRSTRLEN] = "127.0.0.1";
        (void)wifi_address(ip, sizeof(ip));
        return http_hello(&config, ip, "test-boot-event", &paired,
                          pair_event, sizeof(pair_event),
                          unpair_event, sizeof(unpair_event)) ? 1 : 0;
    }
    openlog("instrument_app", LOG_PID | LOG_CONS, LOG_DAEMON);
    if (argc == 5 && !strcmp(argv[1], "--upload-file"))
        return http_upload(&config, argv[3], argv[4]) ? 1 : 0;
    signal(SIGPIPE, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
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
    if (!getenv("INSTRUMENT_LEGACY")) return runtime_run(&config);
    return legacy_run(config);
}
