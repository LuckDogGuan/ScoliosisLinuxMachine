#ifndef RULER_SIGNED_H
#define RULER_SIGNED_H
#include <stdint.h>
#include <wchar.h>
#ifdef _WIN32
#define RS_API __declspec(dllexport)
#else
#define RS_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct rs_context rs_context;
typedef struct {
    int32_t status, reason, branch, reserved;
    double angle_deg, point_x, point_y, elapsed_ms;
} rs_result;
RS_API uint32_t rs_abi_version(void);
RS_API int rs_load_w(const wchar_t *path, rs_context **out);
RS_API int rs_analyze_gray8(rs_context *, const uint8_t *, int width, int height, int stride, rs_result *);
RS_API int rs_analyze_png_w(rs_context *, const wchar_t *, rs_result *);
RS_API void rs_free(rs_context *);
#ifdef __cplusplus
}
#endif
#endif
