#include "../src/app/runtime_v3.c"
#include <assert.h>
int main(void) {
    assert(!mkdir(STATE_DIR,0700)); assert(!mkdir(EVENTS_DIR,0700));
    assert(!mkdir(INSTRUMENT_ROOT "/capture",0700)); assert(!mkdir(PENDING_DIR,0700));
    Config config={.width=1280,.height=1080,.fps=30}; strcpy(config.device_id,"CAM-test");
    Runtime rt={.config=&config,.started_at=1791360000000,.capture_backlight=65,.capture_backlight_verified=1,.backlight_brightness=100};
    COPY(rt.current_id,"CAM-test-backlight"); COPY(rt.current_gate,"gate"); COPY(rt.completion_event,"event-test");
    WorkerResult result={.duration=4000,.reason=0}; completed(&rt,&result);
    cJSON *metadata=v3_read_json(PENDING_DIR "/CAM-test-backlight.json"); assert(metadata);
    assert(read_backlight(metadata)==65);
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(metadata,"backlight_control_verified")));
    cJSON *event=v3_read_json(EVENTS_DIR "/CAM-test-backlight.json"); assert(event);
    assert(read_backlight(event)==65);
    char path[]=PENDING_DIR "/CAM-test-backlight.mp4";
    FILE *file=fopen(path,"wb"); assert(file); assert(fwrite("video",1,5,file)==5); fclose(file);
    Item items[MAX_ITEMS]; int next; assert(pending_items(0,8,items,&next)==1);
    assert(items[0].backlight_percent==65 && items[0].backlight_verified);
    /* Recovery reads the persisted capture, never a changed current setting. */
    Runtime restored={.config=&config,.backlight_brightness=100};
    restored.capture_backlight=read_backlight(metadata);
    restored.capture_backlight_verified=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(metadata,"backlight_control_verified"));
    assert(restored.capture_backlight==65 && restored.capture_backlight_verified);
    cJSON *legacy=cJSON_CreateObject(); assert(read_backlight(legacy)==-1);
    cJSON_AddNumberToObject(legacy,"backlight_percent",65.5); assert(read_backlight(legacy)==-1);
    cJSON_Delete(legacy); cJSON_Delete(event); cJSON_Delete(metadata); cJSON_Delete(rt.last_completed);
    puts("PASS: capture backlight persists in events, metadata, pending list and recovery; unknown is not zero");
}
