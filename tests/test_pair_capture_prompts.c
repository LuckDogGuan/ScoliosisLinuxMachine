#include "../src/app/runtime_v3.c"
#include <assert.h>
#include <sys/time.h>
static cJSON *exchange(Runtime *rt,int client,const struct sockaddr_in *address,const char *op,int sequence,const char *host) {
    cJSON *request=cJSON_CreateObject();
    cJSON_AddNumberToObject(request,"version",3);
    cJSON_AddStringToObject(request,"device_id",rt->config->device_id);
    cJSON_AddStringToObject(request,"host_id",host);
    cJSON_AddStringToObject(request,"boot_id",rt->boot);
    cJSON_AddStringToObject(request,"op",op);
    cJSON_AddNumberToObject(request,"sequence",sequence);
    char nonce[32];snprintf(nonce,sizeof(nonce),"voice-nonce-%d",sequence);
    cJSON_AddStringToObject(request,"nonce",nonce);
    cJSON_AddStringToObject(request,"txn_id","voice-pair");
    cJSON_AddNumberToObject(request,"http_port",18080);
    cJSON_AddStringToObject(request,"gate_id","voice-gate");
    cJSON_AddNumberToObject(request,"ttl_ms",6000);
    char *text=cJSON_PrintUnformatted(request);assert(text);
    assert(sendto(client,text,strlen(text),0,(const struct sockaddr *)address,sizeof(*address))==(ssize_t)strlen(text));
    cJSON_free(text);cJSON_Delete(request);
    struct pollfd ready={.fd=rt->udp,.events=POLLIN};assert(poll(&ready,1,1000)==1);
    receive_udp(rt,v3_now());
    char buffer[PACKET_SIZE+1];ssize_t size=recv(client,buffer,PACKET_SIZE,0);assert(size>0);buffer[size]=0;
    cJSON *reply=cJSON_Parse(buffer);assert(reply);return reply;
}
static void accepted(Runtime *rt,int client,const struct sockaddr_in *address,const char *op,int sequence) {
    cJSON *reply=exchange(rt,client,address,op,sequence,"voice-test-host");
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(reply,"ok")));cJSON_Delete(reply);
}
static void expect_prompt(Runtime *rt,const char *name) {
    assert(rt->sound_in-rt->sound_out==1);
    assert(!strcmp(rt->sounds[rt->sound_out++%8],name));
}
int main(void) {
    assert(!mkdir(STATE_DIR,0700));
    Config config={0};COPY(config.device_id,"CAM-voice-test");
    Runtime rt={.config=&config,.keys=0};
    COPY(rt.boot,"CAM-voice-test-boot");COPY(rt.phase,"idle");
    rt.udp=socket(AF_INET,SOCK_DGRAM|SOCK_NONBLOCK,0);assert(rt.udp>=0);
    int client=socket(AF_INET,SOCK_DGRAM,0);assert(client>=0);
    struct timeval timeout={.tv_sec=1};assert(!setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout)));
    struct sockaddr_in address={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
    assert(!bind(rt.udp,(struct sockaddr *)&address,sizeof(address)));
    socklen_t length=sizeof(address);assert(!getsockname(rt.udp,(struct sockaddr *)&address,&length));
    rt.pair_window=v3_now()+45000;
    accepted(&rt,client,&address,"pair_request",1);
    assert(rt.sound_in==rt.sound_out);
    accepted(&rt,client,&address,"pair_commit",2);expect_prompt(&rt,"pair_success");
    accepted(&rt,client,&address,"pair_commit",2);assert(rt.sound_in==rt.sound_out);
    accepted(&rt,client,&address,"capture_enable",3);assert(rt.gate);expect_prompt(&rt,"capture_enabled");
    accepted(&rt,client,&address,"capture_enable",3);assert(rt.sound_in==rt.sound_out);
    accepted(&rt,client,&address,"capture_renew",4);assert(rt.sound_in==rt.sound_out);
    cJSON *denied=exchange(&rt,client,&address,"capture_enable",5,"another-host");
    assert(!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(denied,"ok")));cJSON_Delete(denied);
    assert(rt.sound_in==rt.sound_out);
    accepted(&rt,client,&address,"capture_disable",5);assert(!rt.gate);expect_prompt(&rt,"capture_disabled");
    accepted(&rt,client,&address,"capture_disable",5);assert(rt.sound_in==rt.sound_out);
    accepted(&rt,client,&address,"unpair",6);assert(!rt.paired);expect_prompt(&rt,"pair_disconnected");
    rt.pair_window=v3_now()+45000;close_pair_window(&rt,"manual");expect_prompt(&rt,"pair_window_closed");
    close(client);close(rt.udp);
    puts("PASS: actual UDP pair/enable/renew/disable/unpair queues correct prompts; duplicates and unauthorized requests stay silent");
    return 0;
}
