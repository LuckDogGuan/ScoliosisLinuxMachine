#include "../src/app/runtime_v3.c"
#include <assert.h>

/* Feed the real maintenance completion path; defer playback so the queue is observable. */
static void snapshot(Runtime *rt, const char *state, int gateway) {
    int sockets[2];
    assert(!socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets));
    WorkerResult result = {.type='M', .gateway=gateway, .wifi=gateway > 0};
    COPY(result.wifi_state, state);
    rt->maintenance.fd=sockets[0];
    rt->maintenance.pid=fork();
    assert(rt->maintenance.pid>=0);
    if (!rt->maintenance.pid) _exit(0);
    assert(waitpid(rt->maintenance.pid,NULL,0)==rt->maintenance.pid);
    assert(send(sockets[1],&result,sizeof(result),0)==(ssize_t)sizeof(result));
    close(sockets[1]);
    rt->speech_resume=UINT64_MAX;
    maintenance(rt,v3_now());
    assert(!rt->maintenance.pid);
}
static void expect_sound(Runtime *rt, const char *name) {
    assert(rt->sound_in-rt->sound_out==1);
    assert(!strcmp(rt->sounds[rt->sound_out++%8],name));
}
int main(int argc,char **argv) {
    Config config={.volume=20};
    Runtime rt={.config=&config,.udp=-1};
    COPY(rt.wifi_state,"unknown");
    if (argc>1 && !strcmp(argv[1],"assets")) {
        assert(argc>2);
        for(int i=2;i<argc;i++) { queue_sound(&rt,argv[i]); expect_sound(&rt,argv[i]); }
        for(int i=0;i<8;i++) queue_sound(&rt,"pair_success");
        queue_sound(&rt,"capture_enabled");
        assert(rt.sound_in-rt.sound_out==8); /* Capacity protection remains. */
        puts("PASS: every existing voice asset is queueable; capacity stays bounded");
        return 0;
    }
    if (argc>1 && !strcmp(argv[1],"boot")) {
        queue_sound(&rt,"boot_ready"); expect_sound(&rt,"boot_ready");
        const char *capture[]={"capture_start","capture_done","capture_failed","capture_short","storage_low"};
        for(size_t i=0;i<sizeof(capture)/sizeof(capture[0]);i++) {
            queue_sound(&rt,capture[i]); expect_sound(&rt,capture[i]);
        }
        queue_sound(&rt,"uploaded"); expect_sound(&rt,"uploaded");
        queue_sound(&rt,"saved_on_device"); expect_sound(&rt,"saved_on_device");
        puts("PASS: startup, capture and existing upload/storage event prompts accepted");
        return 0;
    }
    snapshot(&rt,"unknown",0);
    snapshot(&rt,"recovering",0);
    snapshot(&rt,"needs_attention",0);
    assert(rt.sound_in==rt.sound_out); /* Never connected: do not falsely announce a disconnect. */
    snapshot(&rt,"available",1); expect_sound(&rt,"wifi_connected");
    for(int i=0;i<3;i++) snapshot(&rt,"available",1);
    assert(rt.sound_in==rt.sound_out);
    snapshot(&rt,"recovering",0); expect_sound(&rt,"wifi_disconnected");
    snapshot(&rt,"recovering",0);
    snapshot(&rt,"needs_attention",0);
    snapshot(&rt,"recovering",0);
    assert(rt.sound_in==rt.sound_out);
    snapshot(&rt,"available",1); expect_sound(&rt,"wifi_restored");
    /* PC unpair is independent of Wi-Fi. */
    rt.paired=1; snapshot(&rt,"available",1);
    rt.paired=0; snapshot(&rt,"available",1);
    assert(rt.sound_in==rt.sound_out);
    snapshot(&rt,"needs_attention",0); expect_sound(&rt,"wifi_disconnected");
    snapshot(&rt,"available",1); expect_sound(&rt,"wifi_restored");
    assert(config.volume==20);
    puts("PASS: first connection, one disconnect per outage, recovery, offline startup and PC unpair");
    return 0;
}
