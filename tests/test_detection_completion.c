#include "../src/app/runtime_v3.c"
#include <assert.h>
#include <sys/time.h>
static cJSON *rpc(Runtime *rt,int client,const struct sockaddr_in *address,const char *op,int sequence,const char *reason,const char *id,const char *host) {
 cJSON *q=cJSON_CreateObject();cJSON_AddNumberToObject(q,"version",3);cJSON_AddStringToObject(q,"device_id",rt->config->device_id);
 cJSON_AddStringToObject(q,"boot_id",rt->boot);cJSON_AddStringToObject(q,"host_id",host);cJSON_AddStringToObject(q,"op",op);
 cJSON_AddNumberToObject(q,"sequence",sequence);char nonce[40];snprintf(nonce,sizeof(nonce),"detection-%d",sequence);cJSON_AddStringToObject(q,"nonce",nonce);
 cJSON_AddStringToObject(q,"gate_id","gate-test");cJSON_AddStringToObject(q,"reason",reason);cJSON_AddStringToObject(q,"operation_id",id);
 char *text=cJSON_PrintUnformatted(q);assert(text);assert(sendto(client,text,strlen(text),0,(const struct sockaddr *)address,sizeof(*address))==(ssize_t)strlen(text));cJSON_free(text);cJSON_Delete(q);
 struct pollfd ready={.fd=rt->udp,.events=POLLIN};assert(poll(&ready,1,1000)==1);receive_udp(rt,v3_now());
 char buffer[PACKET_SIZE+1];ssize_t size=recv(client,buffer,PACKET_SIZE,0);assert(size>0);buffer[size]=0;cJSON *reply=cJSON_Parse(buffer);assert(reply);return reply;
}
static int call(Runtime *rt,int client,const struct sockaddr_in *address,const char *op,int seq,const char *reason,const char *id,const char *host) {
 cJSON *reply=rpc(rt,client,address,op,seq,reason,id,host);int ok=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(reply,"ok"));cJSON_Delete(reply);return ok;
}
int main(void) {
 Config cfg={0};COPY(cfg.device_id,"CAM-detection");Runtime rt={.config=&cfg,.paired=1,.gate=1,.lease=UINT64_MAX,.keys=0};
 COPY(rt.host,"test-host");COPY(rt.peer_ip,"127.0.0.1");COPY(rt.boot,"boot-test");COPY(rt.gate_id,"gate-test");
 rt.udp=socket(AF_INET,SOCK_DGRAM|SOCK_NONBLOCK,0);int client=socket(AF_INET,SOCK_DGRAM,0);assert(rt.udp>=0 && client>=0);
 struct timeval timeout={.tv_sec=1};assert(!setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout)));
 struct sockaddr_in address={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};assert(!bind(rt.udp,(struct sockaddr *)&address,sizeof(address)));socklen_t len=sizeof(address);assert(!getsockname(rt.udp,(struct sockaddr *)&address,&len));
 assert(call(&rt,client,&address,"capture_disable",1,"manual_stop","","test-host"));assert(!rt.gate && rt.sound_in-rt.sound_out==1);assert(!strcmp(rt.sounds[rt.sound_out++%8],"capture_disabled"));
 assert(call(&rt,client,&address,"capture_disable",1,"manual_stop","","test-host"));assert(rt.sound_in==rt.sound_out);
 rt.gate=1;rt.lease=UINT64_MAX;assert(call(&rt,client,&address,"capture_disable",2,"round_completed","","test-host"));assert(!rt.gate && rt.sound_in==rt.sound_out);
 assert(call(&rt,client,&address,"detection_class_complete",3,"","class-one","test-host"));assert(rt.sound_in-rt.sound_out==1);assert(!strcmp(rt.sounds[rt.sound_out++%8],"class_completed"));
 assert(call(&rt,client,&address,"detection_class_complete",4,"","class-one","test-host"));assert(rt.sound_in==rt.sound_out);
 rt.record.pid=123;assert(!call(&rt,client,&address,"detection_class_complete",5,"","class-two","test-host"));assert(rt.record.pid==123 && rt.sound_in==rt.sound_out);rt.record.pid=0;
 assert(!call(&rt,client,&address,"detection_class_complete",6,"","class-two","other-host"));assert(rt.sound_in==rt.sound_out);
 rt.gate=1;assert(!call(&rt,client,&address,"detection_class_complete",7,"","class-two","test-host"));
 rt.record.pid=123;assert(call(&rt,client,&address,"capture_disable",8,"class_completed","","test-host"));assert(rt.record.pid==123 && rt.sound_in==rt.sound_out);rt.record.pid=0;
 close(client);close(rt.udp);puts("PASS: pause/round/class separation, duplicate class operation, paired host and recording guards");return 0;
}
