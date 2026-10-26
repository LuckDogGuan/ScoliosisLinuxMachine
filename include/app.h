#ifndef INSTRUMENT_APP_H
#define INSTRUMENT_APP_H

/* src/app: pairing, permissions and capture policy (one V3 state machine)
 * src/middleware: UDP/HTTP, debounce, recording lifecycle and durable JSON
 * src/storage, src/config: durable business data and configuration
 * src/platform: GPIO, RKiPC, ALSA, Wi-Fi, clocks, filesystem/process adapters
 */

#include <stddef.h>

#define INSTRUMENT_VERSION "3.3.36"
#ifndef INSTRUMENT_BUILD
#define INSTRUMENT_BUILD "development"
#endif
#ifndef INSTRUMENT_ROOT
#define INSTRUMENT_ROOT "/opt/instrument"
#endif
#define STATE_DIR INSTRUMENT_ROOT "/state"
#define SOUND_DIR INSTRUMENT_ROOT "/sounds"
#define LEGACY_SOUND_DIR "/userdata/sounds"
#define WRITING_DIR INSTRUMENT_ROOT "/capture/writing"
#define PENDING_DIR INSTRUMENT_ROOT "/capture/pending"
#define LEGACY_PENDING_DIR "/userdata/capture/pending"
#ifndef VIDEO_DIR
#define VIDEO_DIR "/video0"
#endif
#ifndef DISCOVER_PORT
#define DISCOVER_PORT 37020
#endif

enum {
    COMMAND_NONE = 0,
    COMMAND_SEEN = 1,
    COMMAND_ACKED = 2,
    COMMAND_STARTED = 3
};

typedef struct {
    char device_id[64];
    char wifi_ssid[64];
    char pc_base_url[192];
    int volume;
    int width, height, fps, bitrate_kbps, duration_ms, gop;
    char config_path[256];
} Config;

int config_load(const char *path, Config *config);
int config_save_volume(const char *path, int volume);
int wifi_address(char *out, size_t size);
int wifi_rssi(void);
int wifi_gateway_reachable(void);
int wifi_reconnect(void);
int storage_prepare(void);
long storage_free_mb(void);
long storage_total_mb(void);
int storage_memory_mb(long *available, long *total);
int storage_pending_count(void);
int storage_capture_id(const Config *config, char *out, size_t size);
int storage_next_pending(char *out, size_t size);
int storage_write_metadata(const Config *config, const char *capture_id);
int storage_command_load(char *command_id, size_t command_n, char *capture_id, size_t capture_n, int *phase);
int storage_command_save(const char *command_id, const char *capture_id, int phase);
int audio_play(const char *name);
int audio_set_volume(int volume);
int audio_get_volume(void);
int http_hello(Config *config, const char *ip, const char *event_id, int *paired,
               char *pair_event, size_t pair_n, char *unpair_event, size_t unpair_n);
int http_event_ack(const Config *config, const char *event_id, const char *kind);
int http_capture_status(const Config *config, const char *capture_id, const char *phase);
int http_poll(const Config *config, int *capture, char *command_id, size_t command_n);
int http_command_receipt(const Config *config, char *command_id, size_t command_n,
                         int *capture, int *acked, int *started);
int http_command_ack(const Config *config, const char *command_id);
int http_capture_start(const Config *config, const char *capture_id);
int http_upload(const Config *config, const char *capture_id, const char *path);
int http_file_stored(const Config *config, const char *capture_id, int *stored,
                     long long *size_bytes, char *sha256, size_t sha_n);
int capture_record(const Config *config, const char *capture_id);
int discover_open(void);
int discover_poll(Config *config);
int battery_present(void);
int button_capture_pressed(void);
void light_update(int paired);
enum { LIGHT_CONNECTED, LIGHT_PAIRING, LIGHT_DISCONNECTED, LIGHT_FAULT, LIGHT_OFF, LIGHT_LOW_BATTERY };
int light_status_mode(int pairing, int paired, int connected, int fault, int low_battery);
int light_status_tick(int mode, long long now);
void light_status_close(void);
int run_command(char *const argv[]);
int run_command_timeout(char *const argv[], unsigned timeout_ms);

#endif
