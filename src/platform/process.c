#include "app.h"
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/prctl.h>
static unsigned long long monotonic_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC,&now)) return 0;
    return (unsigned long long)now.tv_sec*1000 + now.tv_nsec/1000000;
}
int run_command_timeout(char *const argv[], unsigned timeout_ms) {
    if (!timeout_ms) return -1;
    pid_t child=fork();
    if (child<0) return -1;
    if (!child) {
        (void)setpgid(0,0); (void)prctl(PR_SET_PDEATHSIG,SIGKILL);
        if (getppid()==1) _exit(127);
        execvp(argv[0],argv); _exit(127);
    }
    (void)setpgid(child,child);
    unsigned long long deadline=monotonic_ms()+timeout_ms;
    int status;
    for (;;) {
        pid_t result=waitpid(child,&status,WNOHANG);
        if (result==child) return WIFEXITED(status) && !WEXITSTATUS(status) ? 0 : -1;
        if (result<0 && errno!=EINTR) return -1;
        if (monotonic_ms()>=deadline) break;
        struct timespec pause={.tv_nsec=10000000}; (void)nanosleep(&pause,NULL);
    }
    (void)kill(-child,SIGKILL); (void)kill(child,SIGKILL);
    while (waitpid(child,&status,0)<0 && errno==EINTR) {}
    return -1;
}
int run_command(char *const argv[]) { return run_command_timeout(argv,30000); }
