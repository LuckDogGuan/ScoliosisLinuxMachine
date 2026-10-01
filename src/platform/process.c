#include "app.h"
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
int run_command(char *const argv[]) {
    pid_t child = fork();
    if (child < 0) return -1;
    if (!child) { execvp(argv[0], argv); _exit(127); }
    int status;
    while (waitpid(child, &status, 0) < 0) if (errno != EINTR) return -1;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}
