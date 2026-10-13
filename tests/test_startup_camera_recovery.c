#define camera_stop check_stop
#define camera_status check_status
#define audio_play check_audio
#include "../src/app/runtime_v3.c"
#undef camera_stop
#undef camera_status
#undef audio_play
#include <assert.h>
int check_stop(void) {return 0;}
int check_status(int *status) {*status=1;return 0;}
int check_audio(const char *sound) {(void)sound;return 0;}
int main(void) {
 assert(!mkdir(STATE_DIR,0700));assert(!mkdir(EVENTS_DIR,0700));
 assert(!mkdir(INSTRUMENT_ROOT "/capture",0700));assert(!mkdir(PENDING_DIR,0700));
 FILE *buttons=fopen(STATE_DIR "/buttons.sim","w");assert(buttons);fputs("0\n",buttons);fclose(buttons);
 setenv("INSTRUMENT_BUTTON_FILE",STATE_DIR "/buttons.sim",1);setenv("INSTRUMENT_HOST_TEST","1",1);
 cJSON *active=cJSON_CreateObject();cJSON_AddStringToObject(active,"capture_id","CAM-test-interrupted");cJSON_AddStringToObject(active,"gate_id","gate-test");assert(!v3_save_json(STATE_DIR "/active-v3.json",active));cJSON_Delete(active);
 Config config={.volume=10};COPY(config.device_id,"CAM-test");
 pid_t child=fork();assert(child>=0);if(!child) {(void)prctl(PR_SET_PDEATHSIG,SIGKILL);_exit(runtime_run(&config));}
 int socket_fd=socket(AF_INET,SOCK_DGRAM,0);assert(socket_fd>=0);
 struct timeval timeout={.tv_sec=1};setsockopt(socket_fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
 struct sockaddr_in dest={.sin_family=AF_INET,.sin_port=htons(DISCOVER_PORT),.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
 const char *q="{\"version\":3,\"op\":\"hello_query\",\"device_id\":\"CAM-test\",\"host_id\":\"observer\",\"sequence\":0,\"nonce\":\"nonce-test\"}";
 char buffer[4097];ssize_t size=-1;
 for(int i=0;i<5 && size<0;i++){sendto(socket_fd,q,strlen(q),0,(struct sockaddr*)&dest,sizeof(dest));size=recv(socket_fd,buffer,sizeof(buffer)-1,0);}
 kill(child,SIGTERM);assert(waitpid(child,NULL,0)==child);close(socket_fd);assert(size>0);buffer[size]=0;
 cJSON *hello=cJSON_Parse(buffer),*event=v3_read_json(EVENTS_DIR "/CAM-test-interrupted.json");assert(hello && event);
 printf("startup camera_fault=%d interrupted_result=%s\n",cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(hello,"camera_fault")),str(event,"result"));
 assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(hello,"camera_fault")));assert(!strcmp(str(event,"result"),"failed"));
 cJSON_Delete(hello);cJSON_Delete(event);puts("PASS: incomplete startup journal stays failed and camera-gated until stopped status is confirmed");return 0;
}
