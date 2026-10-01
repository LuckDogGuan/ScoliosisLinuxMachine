#include "v3.h"
#include "lwbtn/lwbtn.h"
#include <assert.h>
#include <stdio.h>
static uint32_t now;
static int sample(int raw, unsigned count) {
    int state = -1;
    while (count--) { now += 10; state = buttons_feed(now, 1, raw); }
    return state;
}
int main(void) {
    assert(sample(5, 200) == -1); /* boot already held cannot trigger */
    assert(sample(0, 5) == 0);
    for (int i = 0; i < 20; i++) { sample(5, 1); assert(sample(0, 1) == 0); }
    assert(sample(1, 5) == 1);
    assert(sample(5, 5) == 5); /* left then right */
    assert(sample(7, 5) == 7);
    assert(sample(5, 5) == 5);
    assert(sample(4, 5) == 4);
    assert(sample(0, 5) == 0);
    assert(buttons_feed(now + 1000, 1, 5) == -1); now += 1000;
    assert(sample(5, 200) == -1); /* sampling interruption disarms */
    assert(sample(0, 5) == 0);
    assert(sample(5, 5) == 5);
    assert(buttons_feed(now + 10, 0, 5) == -1); now += 10;
    assert(sample(5, 200) == -1); /* GPIO read error disarms */
    assert(sample(0, 5) == 0);
    now = UINT32_MAX - 50;
    assert(buttons_feed(now, 0, 0) == -1);
    assert(sample(0, 5) == 0);
    assert(sample(5, 8) == 5); /* uint32 millisecond wrap */
    printf("buttons: boot hold, bounce, staggered, three keys, error, gap, wrap PASS; lwbtn=%zu button=%zu\n", sizeof(lwbtn_t), sizeof(lwbtn_btn_t));
}
