#ifndef INSTRUMENT_PLATFORM_H
#define INSTRUMENT_PLATFORM_H
#include <stddef.h>
#include "cJSON.h"
#include "app.h"
#include <stdint.h>
typedef struct { char rate[32], resolution[32]; int bitrate, gop; } CameraSettings;
int camera_profile_supported(int width, int height);
void camera_profile_reply(const Config *config, cJSON *reply);
int camera_profile_set(Config *config, const cJSON *request);
void camera_profile_load(Config *config);
int camera_get_settings(CameraSettings *settings);
int camera_set_settings(const CameraSettings *settings);
int camera_capture_settings(int fps, int bitrate, int gop, CameraSettings *settings);
int camera_start(void);
int camera_stop(void);
int camera_status(int *status);
int camera_record_ready(uint64_t since_ms);
void camera_diagnostic_health(char *json, size_t size);
int platform_backlight_health(void);
int platform_button_raw(int *mask);
int platform_backlight_set(int on);
int platform_backlight_percent(int percent);
void platform_backlight_configure(int percent);
void instrument_trace_protocol(const cJSON *request, const cJSON *reply, const char *boot, const char *endpoint);
void instrument_trace(const char *event, const char *capture, const char *gate, const char *boot, const char *detail);
#endif
