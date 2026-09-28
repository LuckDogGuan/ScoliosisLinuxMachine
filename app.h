#ifndef INSTRUMENT_APP_H
#define INSTRUMENT_APP_H

#include <stddef.h>

#define PENDING_DIR "/userdata/capture/pending"
#define VIDEO_DIR "/userdata/video0"
#define SOUND_DIR "/userdata/sounds"

typedef struct {
    char device_id[64];
    char wifi_ssid[64];
    char pc_base_url[192];
    int volume;
    int width, height, fps, bitrate_kbps, duration_ms, gop;
} Config;

int config_load(const char *path, Config *config);
int wifi_address(char *out, size_t size);
int wifi_rssi(void);
int storage_prepare(void);
long storage_free_mb(void);
int storage_capture_id(const Config *config, char *out, size_t size);
int storage_next_pending(char *out, size_t size);
int storage_write_metadata(const Config *config, const char *capture_id);
int audio_play(const char *name);
void audio_set_volume(int volume);
int http_hello(const Config *config, const char *ip, int *paired);
int http_poll(const Config *config, int *capture);
int http_capture_start(const Config *config, const char *capture_id);
int http_upload(const Config *config, const char *capture_id, const char *path);
int capture_record(const Config *config, const char *capture_id);
int battery_present(void);
int button_capture_pressed(void);
void light_update(int paired);
int run_command(char *const argv[]);

#endif
