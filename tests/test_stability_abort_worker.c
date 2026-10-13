#include "../src/app/runtime_v3.c"
#include <assert.h>
int main(void) {
    assert(!prctl(PR_SET_CHILD_SUBREAPER,1));
    Worker worker={0};int fd,branch=make_worker(&worker,&fd);assert(branch>=0);
    const char *pid_path=INSTRUMENT_ROOT "/descendant.pid";
    if(!branch) {
        char command[1024];snprintf(command,sizeof(command),"sleep 30 & echo $! > %s; wait",pid_path);
        char *const args[]={"sh","-c",command,NULL};close(fd);_exit(run_command_timeout(args,10000)?1:0);
    }
    pid_t parent=worker.pid;long descendant=0;
    for(int i=0;i<100 && !descendant;i++) {
        FILE *file=fopen(pid_path,"r");if(file){if(fscanf(file,"%ld",&descendant)!=1)descendant=0;fclose(file);}usleep(10000);
    }
    assert(descendant>1);abort_worker(&worker);assert(!worker.pid);assert(waitpid(parent,NULL,0)==parent);
    int status;pid_t child=0;
    for(int i=0;i<200;i++) {
        while((child=waitpid(-1,&status,WNOHANG))>0) {}
        if(child<0 && errno==ECHILD)break;
        usleep(1000);
    }
    int reaped=child<0 && errno==ECHILD;
    int alive=!kill((pid_t)descendant,0);
    if(alive){kill((pid_t)descendant,SIGKILL);while(waitpid(-1,NULL,0)>0 || errno==EINTR) {}}
    assert(!alive);assert(reaped);
    puts("PASS: abort_worker kills command shell and sleep grandchild; no surviving descendants");
}
