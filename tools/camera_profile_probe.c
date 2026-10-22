/* Read-only capability probe; never starts/stops camera or writes config. */
#include "platform.h"
#include <stdio.h>
int main(void) {
    Config config={0};
    if(config_load("/opt/instrument/config.json",&config))return 2;
    cJSON *reply=cJSON_CreateObject();
    if(!reply)return 3;
    camera_profile_reply(&config,reply);
    char *text=cJSON_PrintUnformatted(reply);
    if(!text){cJSON_Delete(reply);return 4;}
    puts(text);cJSON_free(text);cJSON_Delete(reply);return 0;
}
