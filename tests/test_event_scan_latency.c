#include "../src/app/runtime_v3.c"
#include <assert.h>
int main(void) {
    assert(!mkdir(STATE_DIR,0700));assert(!mkdir(EVENTS_DIR,0700));
    assert(!mkdir(INSTRUMENT_ROOT "/capture",0700));assert(!mkdir(PENDING_DIR,0700));
    assert(!mkfifo(EVENTS_DIR "/blocked.json",0600));
    Config config={0};COPY(config.device_id,"CAM-test");
    Runtime rt={.config=&config,.paired=1,.udp=-1};COPY(rt.host,"host-test");
    uint64_t before=v3_now();rt.metrics_at=before;
    alarm(2);maintenance(&rt,before);alarm(0);
    assert(v3_now()-before<100);assert(rt.maintenance.pid>0);
    pid_t child=rt.maintenance.pid;abort_worker(&rt.maintenance);
    assert(waitpid(child,NULL,0)==child);
    puts("PASS: blocked durable event scan cannot stall input/control loop; bounded worker cleanup");
    return 0;
}
