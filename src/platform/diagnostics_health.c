/* Read-only diagnostics run in the camera worker, never the key loop. */
#include "v3.h"
#include "platform.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifndef DIAGNOSTICS_ROOT
#define DIAGNOSTICS_ROOT ""
#endif
static int read_name(const char *path, char *out, size_t size) {
    FILE *file=fopen(path,"r");
    if (!file) return -1;
    int ok=fgets(out,(int)size,file)!=NULL; fclose(file);
    if (ok) out[strcspn(out,"\r\n")]=0;
    return ok ? 0 : -1;
}
static int storage_path(char *out, size_t size) {
    FILE *file=fopen(DIAGNOSTICS_ROOT "/userdata/rkipc.ini","r");
    if (!file) return -1;
    char line[384], section[64]="", mount[192]="", folder[64]="";
    while (fgets(line,sizeof(line),file)) {
        char key[64], value[192];
        if (sscanf(line," [%63[^]]",section)==1 && strchr(line,'[')) continue;
        if (sscanf(line," %63[^= \t] = %191s",key,value)!=2) continue;
        if (!strcmp(section,"storage") && !strcmp(key,"mount_path")) snprintf(mount,sizeof(mount),"%s",value);
        if (!strcmp(section,"storage.0") && !strcmp(key,"folder_name")) {
            if (strlen(value)>=sizeof(folder)) {fclose(file); return -1;}
            strcpy(folder,value);
        }
    }
    fclose(file);
    if (mount[0]!='/' || !folder[0]) return -1;
    size_t length=strlen(mount);
    while (length>0 && mount[length-1]=='/') mount[--length]=0;
    int n=snprintf(out,size,"%s/%s",mount,folder);
    return n>0 && (size_t)n<size ? 0 : -1;
}
void camera_diagnostic_health(char *json, size_t size) {
    cJSON *health=cJSON_CreateObject();
    if (!health) { snprintf(json,size,"{}"); return; }
    const char *sensor="unavailable", *service="unavailable";
    char sensor_name[96]="", path[512], name[128];
    DIR *dir=opendir(DIAGNOSTICS_ROOT "/sys/class/video4linux");
    struct dirent *entry;
    if (dir) {
        sensor="not_detected";
        while ((entry=readdir(dir))) {
            if (strncmp(entry->d_name,"v4l-subdev",10)) continue;
            snprintf(path,sizeof(path),DIAGNOSTICS_ROOT "/sys/class/video4linux/%.255s/name",entry->d_name);
            if (read_name(path,name,sizeof(name))) continue;
            static const char *const sensors[]={"sc3336","sc4336","sc530ai","sc450ai","sc850sl","imx415","mis5001","mia1321"};
            for (size_t i=0;i<sizeof(sensors)/sizeof(sensors[0]);i++) {
                if (strstr(name,sensors[i])) {
                    sensor="detected"; snprintf(sensor_name,sizeof(sensor_name),"%.95s",name); break;
                }
            }
            if (!strcmp(sensor,"detected")) break;
        }
        closedir(dir);
    }
    dir=opendir(DIAGNOSTICS_ROOT "/proc");
    if (dir) {
        service="stopped";
        while ((entry=readdir(dir))) {
            if (entry->d_name[0]<'0' || entry->d_name[0]>'9') continue;
            snprintf(path,sizeof(path),DIAGNOSTICS_ROOT "/proc/%.255s/comm",entry->d_name);
            if (!read_name(path,name,sizeof(name)) && !strcmp(name,"rkipc")) { service="running"; break; }
        }
        closedir(dir);
    }
    int status=0;
    const char *rpc=!strcmp(service,"running") && !camera_status(&status) ? "ready" : "unavailable";
    char actual[256]="";
    int storage_ok=!storage_path(actual,sizeof(actual));
    const char *code="pending", *stage="snapshot";
    if (!strcmp(sensor,"not_detected")) { code="sensor_not_detected"; stage="sensor"; }
    else if (!strcmp(service,"stopped")) { code="service_stopped"; stage="service"; }
    else if (strcmp(service,"running")) { code="service_unavailable"; stage="service"; }
    else if (strcmp(rpc,"ready")) { code="rpc_unavailable"; stage="rpc"; }
    else if (!storage_ok) {code="camera_config_unavailable";stage="storage";}
    else if (strcmp(actual,VIDEO_DIR)) {code="video_path_mismatch";stage="storage";}
    cJSON_AddStringToObject(health,"camera_sensor",sensor);
    cJSON_AddStringToObject(health,"camera_sensor_name",sensor_name);
    cJSON_AddStringToObject(health,"camera_service",service);
    cJSON_AddStringToObject(health,"camera_rpc",rpc);
    cJSON_AddStringToObject(health,"video_expected_dir",VIDEO_DIR);
    cJSON_AddStringToObject(health,"video_actual_dir",actual);
    cJSON_AddStringToObject(health,"code",code);
    cJSON_AddStringToObject(health,"stage",stage);
    if (!cJSON_PrintPreallocated(health,json,(int)size,0)) snprintf(json,size,"{}");
    cJSON_Delete(health);
}
