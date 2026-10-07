#include "v3.h"
#include "platform.h"
#include "lwbtn/lwbtn.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
/* left / middle / right: validated GPIO 3 / 4 / 0, active low.
 * GPIO export, direction and pull-up belong to the platform adapter. */
static lwbtn_t manager;
static lwbtn_btn_t buttons[3];
static int initialized, stable, released;
static uint32_t previous;
static void changed(lwbtn_t *obj, lwbtn_btn_t *button, lwbtn_evt_t event) {
    (void)obj;
    unsigned bit = 1U << (unsigned)(button - buttons);
    if (event == LWBTN_EVT_ONPRESS) stable |= (int)bit;
    if (event == LWBTN_EVT_ONRELEASE) stable &= ~(int)bit;
}
int buttons_feed(uint32_t now, int valid, int raw_mask) {
    if (!initialized) {
        lwbtn_init_ex(&manager, buttons, 3, NULL, changed);
        initialized = 1;
        previous = now;
    }
    if (!valid || (uint32_t)(now - previous) > 100) {
        for (int i = 0; i < 3; i++) lwbtn_reset(&manager, &buttons[i]);
        stable = released = 0;
        previous = now;
        return -1;
    }
    previous = now;
    for (int i = 0; i < 3; i++) lwbtn_set_btn_state(&buttons[i], (raw_mask >> i) & 1);
    lwbtn_process_ex(&manager, now);
    /* Fail closed after boot/read failure until an actual all-release sample. */
    if (!raw_mask && !stable) released = 1;
    return released ? stable : -1;
}
int buttons_snapshot(uint32_t now, int *mask) {
    int raw = 0, valid = !platform_button_raw(&raw);
    *mask = buttons_feed(now, valid, raw);
    return *mask < 0 ? -1 : 0;
}

/* Gesture timing is independent of record/upload workers. Invalid samples and
 * disabled permissions fail closed; require a release before another hold. */
int backlight_tick(uint64_t now, int keys, int allowed) {
    static uint64_t held, release_at, previous;
    static int pressing, lit, disarmed = 1;
    int desired = lit;
    if (!allowed || keys < 0 || (previous && now - previous > 100)) {
        pressing = 0; release_at = 0; disarmed = 1; desired = 0;
    } else {
        if (!keys) disarmed = 0;
        if (keys == 5 && !disarmed) {
            if (!pressing) { pressing = 1; held = now; }
            release_at = 0;
            if (now - held >= 500) desired = 1;
        } else {
            if (pressing && lit) release_at = now + 1000;
            pressing = 0;
            if (release_at && now >= release_at) { desired = 0; release_at = 0; }
        }
    }
    previous = now;
    if (platform_backlight_set(desired)) { lit = 0; return -1; }
    if (lit != desired) {
        instrument_trace("backlight", "", "", "", desired ? "on" : "off");
        lit = desired;
    }
    return 0;
}
