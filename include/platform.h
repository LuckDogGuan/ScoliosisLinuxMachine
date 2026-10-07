#ifndef INSTRUMENT_PLATFORM_H
#define INSTRUMENT_PLATFORM_H
#include <stddef.h>
#include "cJSON.h"
#include <stdint.h>
typedef struct { char rate[32]; int bitrate, gop; } CameraSettings;
int camera_get_settings(CameraSettings *settings);
int camera_set_settings(const CameraSettings *settings);
int camera_capture_settings(int fps, int bitrate, int gop, CameraSettings *settings);
int camera_start(void);
int camera_stop(void);
int camera_status(int *status);
int platform_button_raw(int *mask);
int platform_backlight_set(int on);
void instrument_trace_protocol(const cJSON *request, const cJSON *reply, const char *boot, const char *endpoint);
void instrument_trace(const char *event, const char *capture, const char *gate, const char *boot, const char *detail);
#endif
