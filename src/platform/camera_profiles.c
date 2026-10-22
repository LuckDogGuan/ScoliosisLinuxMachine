#include "v3.h"
#include "platform.h"
#include <ctype.h>
#include <fcntl.h>
#include <time.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef INSTRUMENT_CAMERA_NODE
#define INSTRUMENT_CAMERA_NODE "/dev/video11"
#endif
#ifndef INSTRUMENT_CAMERA_INI
#define INSTRUMENT_CAMERA_INI "/userdata/rkipc.ini"
#endif
static const int candidates[][2]={{640,480},{800,600},{1024,768},{1280,720},{1280,960},{1280,1080},{1920,1080}};
static int ini_value(const char *section, const char *key) {
    FILE *file=fopen(INSTRUMENT_CAMERA_INI,"r");
    if (!file) return -1;
    char line[256], name[80]; int inside=0, result=-1;
    while (fgets(line,sizeof(line),file)) {
        if (sscanf(line," [%79[^]]]",name)==1) {inside=!strcmp(section,name);continue;}
        char *equal=strchr(line,'=');
        if (!inside || !equal) continue;
        char *first=line; while (isspace((unsigned char)*first)) first++;
        char *end=equal; while (end>first && isspace((unsigned char)end[-1])) end--;
        if ((size_t)(end-first)==strlen(key) && !strncmp(first,key,(size_t)(end-first))) {
            char *tail; long value=strtol(equal+1,&tail,10);
            if (tail!=equal+1 && value>=0 && value<=16384) result=(int)value;
            break;
        }
    }
    fclose(file); return result;
}
static int known_profile(int width, int height) {
    for (size_t i=0;i<sizeof(candidates)/sizeof(candidates[0]);i++)
        if (width==candidates[i][0] && height==candidates[i][1]) return 1;
    return 0;
}
int camera_profile_supported(int width, int height) {
    if (!known_profile(width,height) || width>ini_value("video.0","max_width") || height>ini_value("video.0","max_height")) return 0;
    int fd=open(INSTRUMENT_CAMERA_NODE,O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    if (fd<0) return 0;
    int supported=0;
    for (unsigned index=0;index<64;index++) {
        struct v4l2_frmsizeenum size={.index=index,.pixel_format=V4L2_PIX_FMT_NV12};
        if (ioctl(fd,VIDIOC_ENUM_FRAMESIZES,&size)) break;
        if (size.type==V4L2_FRMSIZE_TYPE_DISCRETE) {
            if ((unsigned)width==size.discrete.width && (unsigned)height==size.discrete.height) supported=1;
        } else if (size.type==V4L2_FRMSIZE_TYPE_STEPWISE || size.type==V4L2_FRMSIZE_TYPE_CONTINUOUS) {
            unsigned sw=size.type==V4L2_FRMSIZE_TYPE_CONTINUOUS ? 1 : size.stepwise.step_width;
            unsigned sh=size.type==V4L2_FRMSIZE_TYPE_CONTINUOUS ? 1 : size.stepwise.step_height;
            if (sw && sh && (unsigned)width>=size.stepwise.min_width && (unsigned)width<=size.stepwise.max_width &&
                (unsigned)height>=size.stepwise.min_height && (unsigned)height<=size.stepwise.max_height &&
                ((unsigned)width-size.stepwise.min_width)%sw==0 && ((unsigned)height-size.stepwise.min_height)%sh==0) supported=1;
        }
    }
    close(fd); return supported;
}
static void profile_json(const Config *config, cJSON *value) {
    cJSON_AddNumberToObject(value,"width",config->width);
    cJSON_AddNumberToObject(value,"height",config->height);
    cJSON_AddNumberToObject(value,"fps",config->fps);
    cJSON_AddNumberToObject(value,"bitrate_kbps",config->bitrate_kbps);
    cJSON_AddNumberToObject(value,"gop",config->gop);
    cJSON_AddStringToObject(value,"codec","hevc");
    cJSON_AddStringToObject(value,"container","mp4");
    cJSON_AddStringToObject(value,"application","next_recording_with_rpc_readback");
}
void camera_profile_reply(const Config *config, cJSON *reply) {
    cJSON *value=cJSON_AddObjectToObject(reply,"camera_settings");
    profile_json(config,value);
    int enabled=ini_value("osd.1","enabled");
    if (enabled<0) cJSON_AddNullToObject(value,"timestamp_overlay");
    else cJSON_AddBoolToObject(value,"timestamp_overlay",enabled!=0);
    cJSON_AddStringToObject(value,"support_source","v4l2_nv12_framesizes_and_encoder_limits");
    cJSON *formats=cJSON_AddArrayToObject(value,"formats");
    for (size_t i=0;i<sizeof(candidates)/sizeof(candidates[0]);i++) {
        int width=candidates[i][0],height=candidates[i][1];
        if (!camera_profile_supported(width,height)) continue;
        cJSON *mode=cJSON_CreateObject();
        cJSON_AddNumberToObject(mode,"width",width);cJSON_AddNumberToObject(mode,"height",height);
        cJSON_AddBoolToObject(mode,"analysis_supported",width==640 && height==480);
        cJSON_AddItemToArray(formats,mode);
    }
}
int camera_profile_set(Config *config, const cJSON *request) {
    const cJSON *w=cJSON_GetObjectItemCaseSensitive(request,"width"),*h=cJSON_GetObjectItemCaseSensitive(request,"height");
    if (!cJSON_IsNumber(w) || !cJSON_IsNumber(h) || w->valuedouble!=w->valueint || h->valuedouble!=h->valueint ||
        !camera_profile_supported(w->valueint,h->valueint)) return -1;
    /* This version exposes resolution only. The verified capture rate/codec stay fixed. */
    Config target=*config;target.width=w->valueint;target.height=h->valueint;
    cJSON *saved=cJSON_CreateObject();if (!saved) return -1;
    profile_json(&target,saved);
    int result=v3_save_json(STATE_DIR "/camera-profile.json",saved);cJSON_Delete(saved);
    if (!result) {*config=target;instrument_trace("camera_profile_saved","","","",target.width==640 && target.height==480 ? "640x480" : "diagnostic_resolution");}
    return result;
}
void camera_profile_load(Config *config) {
    cJSON *saved=v3_read_json(STATE_DIR "/camera-profile.json");
    if (!saved) return;
    const cJSON *w=cJSON_GetObjectItemCaseSensitive(saved,"width"),*h=cJSON_GetObjectItemCaseSensitive(saved,"height");
    if (cJSON_IsNumber(w) && cJSON_IsNumber(h) && w->valuedouble==w->valueint && h->valuedouble==h->valueint &&
        known_profile(w->valueint,h->valueint)) {config->width=w->valueint;config->height=h->valueint;}
    cJSON_Delete(saved);
}
