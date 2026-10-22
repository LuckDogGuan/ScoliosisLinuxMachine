#include "platform.h"
#include "v3.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
static int mode, starts, stops, restores, finalized;
static pid_t writer;
static void create_video(const char *name,int complete) {
    char path[512];snprintf(path,sizeof(path),"%s/%s.mp4",VIDEO_DIR,name);
    const unsigned char prefix[]={0,0,0,8,'f','t','y','p',0,0,0,9,'m','d','a','t',1,0,0,0,8,'f','r','e','e'};
    const unsigned char tail[]={0,0,0,9,'m','o','o','v',1};
    FILE *f=fopen(path,"wb");assert(f);assert(fwrite(prefix,1,sizeof(prefix),f)==sizeof(prefix));
    if(complete)assert(fwrite(tail,1,sizeof(tail),f)==sizeof(tail));
    assert(!fclose(f));
}
int camera_get_settings(CameraSettings *s) {memset(s,0,sizeof(*s));strcpy(s->resolution,"1280*1080");return 0;}
int camera_set_settings(const CameraSettings *s) {if(!strcmp(s->resolution,"1280*1080"))restores++;return 0;}
int camera_capture_settings(int fps,int bitrate,int gop,CameraSettings *s) {
    (void)fps;memset(s,0,sizeof(*s));strcpy(s->resolution,"640*480");s->bitrate=bitrate;s->gop=gop;return 0;
}
int camera_start(void) {
    starts++;
    if(mode==0 || (mode==1 && starts==2))create_video("one",1);
    if(mode==3 || mode==5)create_video("one",0);
    if(mode==4){create_video("one",1);create_video("two",1);}
    return 0;
}
int camera_stop(void) {
    stops++;
    if(mode==3){writer=fork();assert(writer>=0);if(!writer){usleep(120000);create_video("one",1);_exit(0);}}
    return 0;
}
int camera_record_ready(uint64_t since) {(void)since;return (mode==2 || mode==6 || (mode==1 && starts==1)) ? 0 : 1;}
int camera_status(int *s) {*s=0;return 0;}
long storage_free_mb(void) {return 5471;}
int storage_write_metadata(const Config *c,const char *id) {(void)c;(void)id;return 0;}
uint64_t v3_now(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
void instrument_trace(const char *e,const char *c,const char *g,const char *b,const char *d) {
    (void)c;(void)g;(void)b;
    if(!strcmp(e,"file_finalized"))finalized++;
    if(!strcmp(e,"record_file_check"))printf("CHECK %s\n",d);
}
#include "../src/middleware/capture.c"
int main(int argc,char **argv) {
    assert(argc==2);mode=atoi(argv[1]);
    Config c={.width=640,.height=480,.fps=30,.duration_ms=1,.bitrate_kbps=4000,.gop=30};
    int result;
    if(mode==6){
        int fd[2];assert(!socketpair(AF_UNIX,SOCK_SEQPACKET,0,fd));pid_t parent=fork();assert(parent>=0);
        if(!parent){close(fd[0]);char msg;assert(read(fd[1],&msg,1)==1 && msg=='P');assert(write(fd[1],"G",1)==1);
            usleep(30000);assert(write(fd[1],"T",1)==1);while(read(fd[1],&msg,1)>0)assert(msg!='S');close(fd[1]);_exit(0);}
        close(fd[1]);int duration=0,reason=2;result=capture_controlled(&c,"replay",fd[0],&duration,&reason);
        shutdown(fd[0],SHUT_RDWR);close(fd[0]);int status;assert(waitpid(parent,&status,0)==parent && status==0);
        assert(duration==0);assert(starts==1);
    } else result=capture_record(&c,"replay");
    if(writer>0)assert(waitpid(writer,NULL,0)==writer);
    int expected=(mode==0 || mode==1 || mode==3)?0:-1;
    printf("mode=%d result=%d expected=%d starts=%d stops=%d restores=%d finalized=%d\n",mode,result,expected,starts,stops,restores,finalized);
    fflush(stdout);assert(result==expected);assert(restores>=1);
    if(mode==1 || mode==2)assert(starts==2);
    assert(finalized==(expected==0));
    if(mode==4 || mode==5)assert(!access("video/one.mp4",F_OK));
    return 0;
}
