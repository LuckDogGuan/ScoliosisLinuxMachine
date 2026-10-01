/* RKiPC in-process adapter: forward the verified board ABI unchanged.
 * init must never wait for a video frame: RKiPC holds the storage mutex here.
 * Optional per-frame traces are for real-board acceptance only. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned frames;
static int64_t first_pts, last_pts;
static long long clock_ms(clockid_t clock) {
    struct timespec value; clock_gettime(clock, &value);
    return (long long)value.tv_sec * 1000 + value.tv_nsec / 1000000;
}
static void trace(const char *event, int64_t pts, int key, int result) {
    struct stat state;
    const char *path = "/opt/instrument/state/mux.jsonl";
    if (!stat(path, &state) && state.st_size > 4*1024*1024) {
        (void)rename("/opt/instrument/state/mux.jsonl.1", "/opt/instrument/state/mux.jsonl.2");
        (void)rename(path, "/opt/instrument/state/mux.jsonl.1");
    }
    FILE *out = fopen(path, "a");
    if (out) {
        fprintf(out, "{\"event\":\"%s\",\"monotonic_ms\":%lld,\"utc_ms\":%lld,\"pts_us\":%lld,\"key\":%d,\"result\":%d,\"frames\":%u,\"first_pts_us\":%lld}\n",
                event, clock_ms(CLOCK_MONOTONIC), clock_ms(CLOCK_REALTIME),
                (long long)pts, key, result, frames, (long long)first_pts);
        fclose(out);
    }
}
int rkmuxer_init(int id, const char *format, const char *path, void *video, void *audio) {
    typedef int (*Init)(int, const char *, const char *, void *, void *);
    typedef int (*Idr)(int, int);
    Init original = (Init)dlsym(RTLD_NEXT, "rkmuxer_init");
    if (!original) return -1;
    int result = original(id, format, path, video, audio);
    if (id == 0) {
        pthread_mutex_lock(&lock);
        frames = 0; first_pts = last_pts = 0;
        trace("mux_init", 0, 0, result);
        if (!result && !getenv("INSTRUMENT_NO_IDR")) {
            /* This board rejects bInstant=1 with RK_ERR_VENC_NOT_SUPPORT. */
            Idr request = (Idr)dlsym(RTLD_DEFAULT, "RK_MPI_VENC_RequestIDR");
            trace("request_idr", 0, 0, request ? request(0, 0) : -1);
        }
        pthread_mutex_unlock(&lock);
    }
    return result;
}
int rkmuxer_write_video_frame(int id, unsigned char *buffer, unsigned size, int64_t pts, int key) {
    typedef int (*Write)(int, unsigned char *, unsigned, int64_t, int);
    Write original = (Write)dlsym(RTLD_NEXT, "rkmuxer_write_video_frame");
    if (!original) return -1;
    int result = original(id, buffer, size, pts, key);
    if (id == 0) {
        pthread_mutex_lock(&lock);
        if (!frames) first_pts = pts;
        frames++; last_pts = pts;
        if (frames == 1 || key || getenv("INSTRUMENT_MUX_TRACE")) trace("video_frame", pts, key, result);
        pthread_mutex_unlock(&lock);
    }
    return result;
}
int rkmuxer_deinit(int id) {
    typedef int (*Deinit)(int);
    Deinit original = (Deinit)dlsym(RTLD_NEXT, "rkmuxer_deinit");
    if (!original) return -1;
    int result = original(id);
    if (id == 0) {
        pthread_mutex_lock(&lock); trace("mux_deinit", last_pts, 0, result); pthread_mutex_unlock(&lock);
    }
    return result;
}
