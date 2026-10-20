/* Frozen round-four reader. No training, inference runtime, or image library. */
#define _CRT_SECURE_NO_WARNINGS
#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <wincodec.h>
#endif
#include "ruler_signed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#define RW 500
#define RH 300
#define RN (RW*RH)
#define LW 96
#define LH 112
#define LN (LW*LH)
#define IW 1280
#define IH 1080
#define MAX_REFS 256
#define MAX_ANCHORS 1024
struct rs_context {
    uint32_t na,nr;
    double brightness, limit, span;
    uint8_t baseline[RN], white[RN], gray[RN], mask[RN], temp[RN], opened[RN], seen[RN];
    double *ax,*ay,*ux,*uy,*labels;
    float *features, feature[LN], scratch[LN], distances[MAX_REFS];
    int queue[RN];
    int xstart[LW],xcount[LW],ystart[LH],ycount[LH];
    float xweight[LW][4],yweight[LH][4],horizontal[270*LW];
    uint8_t *image;
};
static double now_ms(void) {
#ifdef _WIN32
    LARGE_INTEGER count,freq;
    QueryPerformanceCounter(&count);QueryPerformanceFrequency(&freq);
    return 1000.0*(double)count.QuadPart/(double)freq.QuadPart;
#else
    return (double)clock()*1000.0/CLOCKS_PER_SEC;
#endif
}
static int finish(rs_result *r,int status,int reason,int branch) {
    r->status=status;r->reason=reason;r->branch=branch;return status;
}
static void reset(rs_result *r) {
    memset(r,0,sizeof(*r));r->angle_deg=NAN;r->point_x=NAN;r->point_y=NAN;
}
static int rd(FILE *f,void *out,size_t size) { return fread(out,1,size,f)==size; }
static int u32(FILE *f,uint32_t *out) {
    uint8_t b[4];if(!rd(f,b,4))return 0;
    *out=(uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);return 1;
}
static int little_endian(void) { const uint16_t x=1;return *(const uint8_t*)&x==1; }
RS_API uint32_t rs_abi_version(void) { return 1; }
RS_API void rs_free(rs_context *c) {
    if(!c)return;
    free(c->ax);free(c->ay);free(c->ux);free(c->uy);free(c->labels);free(c->features);free(c->image);free(c);
}
RS_API int rs_load_w(const wchar_t *path,rs_context **out) {
    FILE *f=NULL;rs_context *c=NULL;char magic[8];uint32_t h[10];int result=-2;
    if(!out)return -1;*out=NULL;if(!path || !little_endian())return -1;
#ifdef _WIN32
    f=_wfopen(path,L"rb");
#else
    { char p[4096];size_t n=wcstombs(p,path,sizeof(p));if(n==(size_t)-1||n>=sizeof(p))return -1;f=fopen(p,"rb"); }
#endif
    if(!f)return -2;
    if(!rd(f,magic,8)||memcmp(magic,"RSR4BIN1",8))goto done;
    for(int i=0;i<10;i++)if(!u32(f,&h[i]))goto done;
    if(h[0]!=1||h[1]!=IW||h[2]!=IH||h[3]!=RW||h[4]!=RH||h[5]!=LW||h[6]!=LH||
       h[7]<2||h[7]>MAX_ANCHORS||h[8]<2||h[8]>MAX_REFS||h[9]!=0)goto done;
    c=(rs_context*)calloc(1,sizeof(*c));if(!c){result=-3;goto done;}
    c->na=h[7];c->nr=h[8];
    if(!rd(f,&c->brightness,8)||!rd(f,&c->limit,8)||!rd(f,&c->span,8))goto done;
    if(!isfinite(c->brightness)||c->brightness<0||c->brightness>255||!isfinite(c->limit)||c->limit<=0||c->limit>100||c->span!=1.0)goto done;
    if(!rd(f,c->baseline,RN)||!rd(f,c->white,RN))goto done;
    {int count=0;for(int i=0;i<RN;i++){if(c->white[i]>1)goto done;count+=c->white[i];}if(!count)goto done;}
    c->ax=(double*)malloc(c->na*sizeof(double));c->ay=(double*)malloc(c->na*sizeof(double));
    c->ux=(double*)malloc(c->na*sizeof(double));c->uy=(double*)malloc(c->na*sizeof(double));
    c->features=(float*)malloc((size_t)c->nr*LN*sizeof(float));c->labels=(double*)malloc(c->nr*sizeof(double));
    c->image=(uint8_t*)malloc((size_t)IW*IH);
    if(!c->ax||!c->ay||!c->ux||!c->uy||!c->features||!c->labels||!c->image){result=-3;goto done;}
    for(uint32_t i=0;i<c->na;i++) {
        if(!rd(f,&c->ax[i],8)||!rd(f,&c->ay[i],8)||!isfinite(c->ax[i])||!isfinite(c->ay[i])||
           c->ax[i]<100||c->ax[i]>1100||fabs(c->ay[i])>90||(i&&c->ax[i]<c->ax[i-1]))goto done;
    }
    if(!rd(f,c->features,(size_t)c->nr*LN*sizeof(float))||!rd(f,c->labels,c->nr*sizeof(double))||fgetc(f)!=EOF)goto done;
    for(size_t i=0;i<(size_t)c->nr*LN;i++)if(!isfinite(c->features[i])||fabsf(c->features[i])>100)goto done;
    for(uint32_t i=0;i<c->nr;i++)if(!isfinite(c->labels[i])||fabs(c->labels[i])>90)goto done;
    for(int x=0;x<LW;x++) {
        double scale=235.0/LW,start=x*scale,end=(x+1)*scale;
        c->xstart[x]=(int)floor(start);c->xcount[x]=(int)ceil(end)-c->xstart[x];
        for(int j=0;j<c->xcount[x];j++){int at=c->xstart[x]+j;
            double overlap=fmin(end,(double)at+1)-fmax(start,(double)at);
            c->xweight[x][j]=overlap>1e-3?(float)(overlap/scale):0;}
    }
    for(int y=0;y<LH;y++) {
        double scale=270.0/LH,start=y*scale,end=(y+1)*scale;
        c->ystart[y]=(int)floor(start);c->ycount[y]=(int)ceil(end)-c->ystart[y];
        for(int j=0;j<c->ycount[y];j++){int at=c->ystart[y]+j;
            double overlap=fmin(end,(double)at+1)-fmax(start,(double)at);
            c->yweight[y][j]=overlap>1e-3?(float)(overlap/scale):0;}
    }
    *out=c;c=NULL;result=0;
done:
    fclose(f);rs_free(c);return result;
}
static double hist_median(const int *hist,int size,int count) {
    int a=(count-1)/2,b=count/2,at=0;double first=0,second=0;int got=0;
    for(int i=0;i<size;i++){at+=hist[i];if(!got&&at>a){first=i;got=1;}if(at>b){second=i;break;}}
    return (first+second)*0.5;
}
static int cmp_double(const void *a,const void *b) { double x=*(const double*)a,y=*(const double*)b;return (x>y)-(x<y); }
/* NumPy's float32 pairwise reduction, including its 128-element block rule. */
static float sum32(const float *a,int n) {
    if(n<8){float s=-0.0f;for(int i=0;i<n;i++)s+=a[i];return s;}
    if(n<=128){float r[8];int i;for(i=0;i<8;i++)r[i]=a[i];
        for(i=8;i<=n-8;i+=8)for(int j=0;j<8;j++)r[j]+=a[i+j];
        float s=((r[0]+r[1])+(r[2]+r[3]))+((r[4]+r[5])+(r[6]+r[7]));
        for(;i<n;i++)s+=a[i];return s;}
    int k=(n/2)&~7;return sum32(a,k)+sum32(a+k,n-k);
}
static int right(rs_context *c,const uint8_t *im,int stride,rs_result *r) {
    int hist[256]={0},nh=0;
    for(int y=0;y<RH;y++)for(int x=0;x<RW;x++) {
        int sy=80+y*2,sx=100+x*2,i=y*RW+x;
        int value=im[sy*stride+sx]+im[sy*stride+sx+1]+im[(sy+1)*stride+sx]+im[(sy+1)*stride+sx+1];
        c->gray[i]=(uint8_t)((value+2)>>2);
        if(c->white[i]){hist[c->gray[i]]++;nh++;}
        c->mask[i]=(uint8_t)(c->white[i]&&((int)c->baseline[i]-(int)c->gray[i]>40));
    }
    double bright=hist_median(hist,256,nh);
    if(bright<100||fabs(bright-c->brightness)>65)return finish(r,1,2,0);
    /* Branch-free interior; the small border retains OpenCV neutral values. */
    for(int y=0;y<RH;y++)for(int x=0;x<RW;x++) {
        int i=y*RW+x;
        if(y>0&&y<RH-1&&x>0&&x<RW-1) {
            const uint8_t *a=c->mask+i;
            c->temp[i]=(uint8_t)(a[-RW-1]&a[-RW]&a[-RW+1]&a[-1]&a[0]&a[1]&a[RW-1]&a[RW]&a[RW+1]);
        } else {
            int v=1;for(int dy=-1;dy<=1&&v;dy++)for(int dx=-1;dx<=1;dx++){
                int yy=y+dy,xx=x+dx;if(yy>=0&&yy<RH&&xx>=0&&xx<RW&&!c->mask[yy*RW+xx]){v=0;break;}}
            c->temp[i]=(uint8_t)v;
        }
    }
    for(int y=0;y<RH;y++)for(int x=0;x<RW;x++) {
        int i=y*RW+x;
        if(y>0&&y<RH-1&&x>0&&x<RW-1) {
            const uint8_t *a=c->temp+i;
            c->opened[i]=(uint8_t)(a[-RW-1]|a[-RW]|a[-RW+1]|a[-1]|a[0]|a[1]|a[RW-1]|a[RW]|a[RW+1]);
        } else {
            int v=0;for(int dy=-1;dy<=1&&!v;dy++)for(int dx=-1;dx<=1;dx++){
                int yy=y+dy,xx=x+dx;if(yy>=0&&yy<RH&&xx>=0&&xx<RW&&c->temp[yy*RW+xx]){v=1;break;}}
            c->opened[i]=(uint8_t)v;
        }
    }
    memset(c->seen,0,RN);int best=0,best_right=0,best_hist[RH]={0},best_count=0;
    for(int start=0;start<RN;start++)if(c->opened[start]&&!c->seen[start]) {
        int head=0,tail=1,maxx=0;c->queue[0]=start;c->seen[start]=1;
        while(head<tail){int at=c->queue[head++],y=at/RW,x=at%RW;if(x>maxx)maxx=x;
            for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
                int yy=y+dy,xx=x+dx;if(yy<0||yy>=RH||xx<0||xx>=RW)continue;
                int k=yy*RW+xx;if(c->opened[k]&&!c->seen[k]){c->seen[k]=1;c->queue[tail++]=k;}
            }
        }
        if(tail>best){best=tail;best_right=maxx;best_count=0;memset(best_hist,0,sizeof(best_hist));
            for(int i=0;i<tail;i++)if(c->queue[i]%RW>=maxx-1){best_hist[c->queue[i]/RW]++;best_count++;}}
    }
    if(best<60)return finish(r,1,3,1);
    r->point_x=best_right*2+100;r->point_y=hist_median(best_hist,RH,best_count)*2+80;
    double x=r->point_x;
    if(x<c->ax[0]||x>c->ax[c->na-1])return finish(r,1,4,3);
    int near_count=0;double low=1000,high=-1000;
    for(uint32_t i=0;i<c->na;i++)if(fabs(c->ax[i]-x)<=6){near_count++;if(c->ay[i]<low)low=c->ay[i];if(c->ay[i]>high)high=c->ay[i];}
    if(near_count>1&&high-low>.75)return finish(r,1,5,1);
    int nu=0;
    for(uint32_t i=0;i<c->na;){uint32_t end=i+1;while(end<c->na&&c->ax[end]==c->ax[i])end++;
        double values[MAX_ANCHORS];for(uint32_t j=i;j<end;j++)values[j-i]=c->ay[j];
        qsort(values,end-i,sizeof(double),cmp_double);int n=(int)(end-i);
        c->ux[nu]=c->ax[i];c->uy[nu]=(values[(n-1)/2]+values[n/2])*0.5;nu++;i=end;
    }
    for(int i=1;i<nu;i++)if(c->uy[i]-c->uy[i-1]<-.75)return finish(r,1,6,1);
    double value=c->uy[nu-1];
    for(int i=1;i<nu;i++)if(x<=c->ux[i]){value=c->uy[i-1]+(x-c->ux[i-1])*(c->uy[i]-c->uy[i-1])/(c->ux[i]-c->ux[i-1]);break;}
    r->angle_deg=value;return finish(r,0,0,1);
}
/* INTER_AREA uint8 path: separable float32 weighted accumulation. */
static void left_feature(rs_context *c,const uint8_t *im,int stride) {
    for(int y=0;y<270;y++)for(int x=0;x<LW;x++) {
        const uint8_t *row=im+(280+y)*stride+130+c->xstart[x];
        float total=0;
        for(int j=0;j<c->xcount[x];j++)total+=(float)row[j]*c->xweight[x][j];
        c->horizontal[y*LW+x]=total;
    }
    for(int y=0;y<LH;y++)for(int x=0;x<LW;x++) {
        float total=0;
        for(int j=0;j<c->ycount[y];j++)total+=c->horizontal[(c->ystart[y]+j)*LW+x]*c->yweight[y][j];
        float rounded=nearbyintf(total);if(rounded<0)rounded=0;if(rounded>255)rounded=255;
        c->feature[y*LW+x]=rounded;
    }
    float mean=sum32(c->feature,LN)/(float)LN;
    for(int i=0;i<LN;i++){float v=c->feature[i]-mean;c->scratch[i]=v*v;}
    float scale=sqrtf(sum32(c->scratch,LN)/(float)LN)+5.0f;
    for(int i=0;i<LN;i++)c->feature[i]=(c->feature[i]-mean)/scale;
}
RS_API int rs_analyze_gray8(rs_context *c,const uint8_t *im,int width,int height,int stride,rs_result *r) {
    if(!r)return -1;reset(r);double begin=now_ms();int status;
    if(!c||!im||stride<width)return finish(r,-1,-1,0);
    if(width!=IW||height!=IH){status=finish(r,1,1,0);goto done;}
    status=right(c,im,stride,r);
    if(status==0||r->reason==2)goto done;
    if(isfinite(r->point_x)&&r->point_x>=300){r->branch=3;goto done;}
    left_feature(c,im,stride);
    int first=-1,second=-1;
    for(uint32_t j=0;j<c->nr;j++) {
        const float *ref=c->features+(size_t)j*LN;
        for(int k=0;k<LN;k++){float d=ref[k]-c->feature[k];c->scratch[k]=d*d;}
        float dist=sqrtf(sum32(c->scratch,LN)/(float)LN);c->distances[j]=dist;
        if(first<0||dist<c->distances[first]){second=first;first=(int)j;}
        else if(second<0||dist<c->distances[second])second=(int)j;
    }
    r->point_x=NAN;r->point_y=NAN;
    if(c->distances[first]>c->limit){status=finish(r,1,7,2);goto done;}
    if(fabs(c->labels[first]-c->labels[second])>c->span){status=finish(r,1,8,2);goto done;}
    double d0=c->distances[first],d1=c->distances[second];
    double w0=1.0/(d0*d0+1e-7),w1=1.0/(d1*d1+1e-7);
    r->angle_deg=(w0*c->labels[first]+w1*c->labels[second])/(w0+w1);status=finish(r,0,9,2);
done:
    r->elapsed_ms=now_ms()-begin;return status;
}
RS_API int rs_analyze_png_w(rs_context *c,const wchar_t *path,rs_result *r) {
    if(!r)return -1;reset(r);if(!c||!path)return finish(r,-1,-1,0);
#ifdef _WIN32
    double begin=now_ms();HRESULT hr=CoInitializeEx(NULL,COINIT_MULTITHREADED);
    int uninit=SUCCEEDED(hr),status=-4;
    IWICImagingFactory *factory=NULL;IWICBitmapDecoder *decoder=NULL;
    IWICBitmapFrameDecode *frame=NULL;IWICBitmapSource *source=NULL;uint8_t *rgb=NULL;
    if(FAILED(hr)&&hr!=RPC_E_CHANGED_MODE)goto done;
    hr=CoCreateInstance(&CLSID_WICImagingFactory,NULL,CLSCTX_INPROC_SERVER,&IID_IWICImagingFactory,(void**)&factory);
    if(FAILED(hr))goto done;
    hr=IWICImagingFactory_CreateDecoderFromFilename(factory,path,NULL,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder);
    if(FAILED(hr))goto done;
    GUID container;
    if(FAILED(IWICBitmapDecoder_GetContainerFormat(decoder,&container))||!IsEqualGUID(&container,&GUID_ContainerFormatPng)){status=-5;goto done;}
    UINT count=0,w=0,h=0;
    if(FAILED(IWICBitmapDecoder_GetFrameCount(decoder,&count))||count!=1){status=-5;goto done;}
    if(FAILED(IWICBitmapDecoder_GetFrame(decoder,0,&frame)))goto done;
    if(FAILED(IWICBitmapFrameDecode_GetSize(frame,&w,&h)))goto done;
    if(w!=IW||h!=IH){status=-5;goto done;}
    WICPixelFormatGUID format;
    if(FAILED(IWICBitmapFrameDecode_GetPixelFormat(frame,&format)))goto done;
    if(!IsEqualGUID(&format,&GUID_WICPixelFormat24bppRGB)&&!IsEqualGUID(&format,&GUID_WICPixelFormat24bppBGR)){status=-5;goto done;}
    hr=WICConvertBitmapSource(&GUID_WICPixelFormat24bppRGB,(IWICBitmapSource*)frame,&source);
    if(FAILED(hr))goto done;
    rgb=(uint8_t*)malloc((size_t)IW*IH*3);if(!rgb){status=-3;goto done;}
    if(FAILED(IWICBitmapSource_CopyPixels(source,NULL,IW*3,IW*IH*3,rgb)))goto done;
    for(size_t i=0;i<(size_t)IW*IH;i++)c->image[i]=(uint8_t)((9797u*rgb[3*i]+19234u*rgb[3*i+1]+3737u*rgb[3*i+2])>>15);
    status=rs_analyze_gray8(c,c->image,IW,IH,IW,r);
done:
    free(rgb);if(source)IWICBitmapSource_Release(source);if(frame)IWICBitmapFrameDecode_Release(frame);
    if(decoder)IWICBitmapDecoder_Release(decoder);if(factory)IWICImagingFactory_Release(factory);if(uninit)CoUninitialize();
    if(status<0)finish(r,status,status,0);
    r->elapsed_ms=now_ms()-begin;return status;
#else
    return finish(r,-5,-5,0);
#endif
}
