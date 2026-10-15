/* Read-only diagnostics run in the existing camera worker, never the key loop. */
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
            /* Match the sensor names supported by this board's startup profiles. */
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
    cJSON_AddStringToObject(health,"camera_sensor",sensor);
    cJSON_AddStringToObject(health,"camera_sensor_name",sensor_name);
    cJSON_AddStringToObject(health,"camera_service",service);
    cJSON_AddStringToObject(health,"camera_rpc",rpc);
    if (!cJSON_PrintPreallocated(health,json,(int)size,0)) snprintf(json,size,"{}");
    cJSON_Delete(health);
}
