#ifndef INSTRUMENT_V3_H
#define INSTRUMENT_V3_H
#include "app.h"
#include "cJSON.h"
#include <stdint.h>
uint64_t v3_now(void);
uint64_t wall_ms(void);
int runtime_run(Config *config);
int buttons_snapshot(uint32_t now, int *mask);
void buttons_close(void);
int backlight_tick(uint64_t now, int keys, int allowed);
int buttons_feed(uint32_t now, int valid, int raw_mask);
int capture_controlled(const Config *config, const char *id, int control_fd, int *duration_ms, int *reason);
int http_v3_start(const Config *config, const char *id, const char *gate, const char *boot);
int http_v3_status(const Config *config, const cJSON *event);
int v3_save_json(const char *path, const cJSON *value);
cJSON *v3_read_json(const char *path);
int camera_diagnostic_frame(const Config *config, const char *token, cJSON *details);
#endif

int http_diagnostic_frame(const Config *config, const char *token, const char *path, cJSON *details);
