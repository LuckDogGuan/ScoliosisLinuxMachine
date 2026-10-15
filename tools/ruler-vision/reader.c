#define _POSIX_C_SOURCE 200809L
#include "ruler_vision.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif
static double now_ms(void) {
#ifdef _WIN32
    LARGE_INTEGER n,f; QueryPerformanceCounter(&n); QueryPerformanceFrequency(&f); return (double)n.QuadPart*1000/(double)f.QuadPart;
#else
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1000.+t.tv_nsec/1000000.;
#endif
}
static void output(const rv_context *c,rv_result r,unsigned long long sequence,double elapsed) {
    printf("ANGLE_RESULT_JSON {\"angle_deg\":");
    if(r.valid) printf("%.8f",r.angle_deg); else printf("null");
    printf(",\"valid\":%s,\"reason\":\"%s\",\"point\":[%.4f,%.4f],\"area\":%d,\"anchor_error\":%.4f,\"processing_ms\":%.4f,\"sequence\":%llu,\"algorithm_version\":\"%s\",\"calibration_version\":\"%s\",\"label_status\":\"manual_unverified\"}\n",
       r.valid?"true":"false",r.reason,r.x,r.y,r.area,r.anchor_error,elapsed,sequence,RV_ALGORITHM_VERSION,rv_version(c));
    fflush(stdout);
}
static unsigned char *read_file(const char *path,size_t *length) {
    FILE *f=fopen(path,"rb"); if(!f)return NULL;
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return NULL;} long n=ftell(f);
    if(n<=0||n>12*1024*1024||fseek(f,0,SEEK_SET)!=0){fclose(f);return NULL;}
    unsigned char *b=(unsigned char *)malloc((size_t)n+1);
    if(!b){fclose(f);return NULL;}
    if(fread(b,1,(size_t)n,f)!=(size_t)n){free(b);fclose(f);return NULL;}
    fclose(f);b[n]=0;*length=(size_t)n;return b;
}
int main(int argc,char **argv) {
    if(argc<3){fprintf(stderr,"usage: ruler_reader calibration.json image.pgm | --stream | --benchmark image.pgm count\n");return 2;}
    size_t n=0;unsigned char *text=read_file(argv[1],&n);char error[128];
    if(!text){fprintf(stderr,"calibration unreadable\n");return 2;}
    rv_context *c=rv_load((const char *)text,n,error,sizeof(error)); free(text);
    if(!c){fprintf(stderr,"%s\n",error);return 2;}
    int w=rv_width(c),h=rv_height(c); size_t bytes=(size_t)w*h;
    unsigned char *gray=(unsigned char *)malloc(bytes); if(!gray){rv_free(c);return 2;}
    int result=0;
    if(!strcmp(argv[2],"--stream")) {
#ifdef _WIN32
        _setmode(_fileno(stdin),_O_BINARY);
#endif
        unsigned long long seq=0;
        for(;;){
            size_t got=fread(gray,1,bytes,stdin); if(!got)break;
            if(got!=bytes){fprintf(stderr,"truncated raw frame\n");result=3;break;}
            double start=now_ms();rv_result r=rv_analyze(c,gray,w,h,w);output(c,r,seq++,now_ms()-start);
        }
    }else{
        int benchmark=!strcmp(argv[2],"--benchmark"); const char *path=benchmark?(argc>=5?argv[3]:NULL):argv[2];
        FILE *f=path?fopen(path,"rb"):NULL;int iw=0,ih=0,depth=0;char magic[3]={0};
        if(!f||fscanf(f,"%2s %d %d %d",magic,&iw,&ih,&depth)!=4||strcmp(magic,"P5")||iw!=w||ih!=h||depth!=255||fgetc(f)!='\n'||fread(gray,1,bytes,f)!=bytes||fgetc(f)!=EOF){
            fprintf(stderr,"image must be exact-size binary PGM (P5) without comments\n");result=3;
        }else{
            int loops=benchmark?atoi(argv[4]):1;if(loops<1||loops>100000){result=3;}else
            for(int i=0;i<loops;i++){double start=now_ms();rv_result r=rv_analyze(c,gray,w,h,w);output(c,r,(unsigned long long)i,now_ms()-start);}
        }
        if(f)fclose(f);
    }
    free(gray);rv_free(c);return result;
}
