#ifndef RULER_VISION_H
#define RULER_VISION_H
#include <stddef.h>
#include <stdint.h>
#define RV_ALGORITHM_VERSION "shadow-position-v1"
typedef struct rv_context rv_context;
typedef struct {
    int valid, area;
    double angle_deg, x, y, mass, anchor_error, processing_ms;
    const char *reason;
} rv_result;
rv_context *rv_load(const char *json, size_t length, char *error, size_t error_size);
void rv_free(rv_context *ctx);
int rv_width(const rv_context *ctx);
int rv_height(const rv_context *ctx);
const char *rv_version(const rv_context *ctx);
rv_result rv_analyze(rv_context *ctx, const uint8_t *gray, int width, int height, int stride);
#endif
