#ifndef RULER_TEMPLATE_H
#define RULER_TEMPLATE_H
#include "../ruler-signed/ruler_signed.h"
/* Query dimensions with buffer=NULL, then supply width*height bytes. */
RS_API int rt_gray_w(const wchar_t *,uint8_t *,int,int *,int *);
RS_API int rt_roi_w(const wchar_t *,int,int,int,int,uint8_t *,int);
RS_API int rt_pixel_w(const wchar_t *,int,int,int *);
#endif
