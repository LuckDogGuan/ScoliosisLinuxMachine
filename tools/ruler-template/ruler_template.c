/* Reference-image matching, independent candidate; ABI-compatible with signed reader. */
#define _CRT_SECURE_NO_WARNINGS
#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <wincodec.h>
#endif
#include "../ruler-signed/ruler_signed.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <time.h>
#define MAXREF 256
#define MAXPIX 400000
struct rs_context {
    uint32_t w,h,x,y,rw,rh,block,n;
    size_t pixels;
    double *refs,*gram,*labels,*scratch;
};
static double timer_ms(void) { return (double)clock()*1000.0/CLOCKS_PER_SEC; }
static FILE *open_w(const wchar_t *path,const wchar_t *mode) {
#ifdef _WIN32
    return _wfopen(path,mode);
#else
    char p[4096],m[32]; if(wcstombs(p,path,sizeof(p))>=sizeof(p)||wcstombs(m,mode,sizeof(m))>=sizeof(m))return NULL;
    return fopen(p,m);
#endif
}
static int result(rs_result *r,int status,int reason) {
    r->status=status;r->reason=reason;r->branch=0;return status;
}
static void reset(rs_result *r) { memset(r,0,sizeof(*r));r->angle_deg=r->point_x=r->point_y=NAN; }
RS_API uint32_t rs_abi_version(void) { return 1; }
RS_API void rs_free(rs_context *c) { if(c){free(c->refs);free(c->gram);free(c->labels);free(c->scratch);free(c);} }
static int geometry(rs_context *c) {
    if(!c->w||!c->h||c->w>4096||c->h>4096||!c->block||c->block>16||
       !c->rw||!c->rh||c->x>=c->w||c->y>=c->h||
       c->rw>c->w-c->x||c->rh>c->h-c->y||c->rw%c->block||c->rh%c->block||
       c->n<2||c->n>MAXREF)return 0;
    c->pixels=(size_t)(c->rw/c->block)*(c->rh/c->block);
    return c->pixels<=MAXPIX;
}
static int allocate(rs_context *c) {
    c->refs=(double*)calloc(c->n*c->pixels,sizeof(double));
    c->labels=(double*)calloc(c->n,sizeof(double));
    c->gram=(double*)calloc((size_t)c->n*c->n,sizeof(double));
    c->scratch=(double*)calloc(c->pixels,sizeof(double));
    return c->refs&&c->labels&&c->gram&&c->scratch;
}
static void gram(rs_context *c) {
    for(uint32_t i=0;i<c->n;i++)for(uint32_t j=0;j<=i;j++){
        double v=0;for(size_t k=0;k<c->pixels;k++)v+=c->refs[i*c->pixels+k]*c->refs[j*c->pixels+k];
        c->gram[i*c->n+j]=c->gram[j*c->n+i]=v;
    }
}
RS_API int rs_load_w(const wchar_t *path,rs_context **out) {
    char magic[8];uint32_t version=0,header[8];int code=-2;rs_context *c=NULL;
    if(!out)return -1;
    *out=NULL;
    if(!path)return -1;
    const uint16_t endian=1;if(*(const uint8_t*)&endian!=1)return -1;
    FILE *f=open_w(path,L"rb");if(!f)return -2;
    if(fread(magic,1,8,f)!=8||memcmp(magic,"RSRTBIN1",8)||fread(&version,4,1,f)!=1||version!=1||fread(header,4,8,f)!=8)goto done;
    c=(rs_context*)calloc(1,sizeof(*c));if(!c){code=-3;goto done;}
    c->w=header[0];c->h=header[1];c->x=header[2];c->y=header[3];
    c->rw=header[4];c->rh=header[5];c->block=header[6];c->n=header[7];
    if(!geometry(c))goto done;
    if(!allocate(c)){code=-3;goto done;}
    if(fread(c->labels,8,c->n,f)!=c->n||fread(c->refs,8,c->n*c->pixels,f)!=c->n*c->pixels||fgetc(f)!=EOF)goto done;
    for(uint32_t i=0;i<c->n;i++){
        if(!isfinite(c->labels[i])||fabs(c->labels[i])>90||(i&&c->labels[i]<c->labels[i-1]))goto done;
        double mean=0,variance=0;
        for(size_t k=0;k<c->pixels;k++){double v=c->refs[i*c->pixels+k];if(!isfinite(v)||fabs(v)>255)goto done;mean+=v;variance+=v*v;}
        if(fabs(mean/c->pixels)>1e-7||variance/c->pixels<4)goto done;
    }
    gram(c);*out=c;c=NULL;code=0;
done: fclose(f);rs_free(c);return code;
}
static double feature(rs_context *c,const uint8_t *im,int stride,double *out) {
    size_t k=0;double mean=0;
    for(uint32_t y=c->y;y<c->y+c->rh;y+=c->block)for(uint32_t x=c->x;x<c->x+c->rw;x+=c->block){
        double v=0;for(uint32_t dy=0;dy<c->block;dy++)for(uint32_t dx=0;dx<c->block;dx++)v+=im[(size_t)(y+dy)*stride+x+dx];
        out[k++]=v/(c->block*c->block);mean+=out[k-1];
    }
    mean/=c->pixels;double variance=0;
    for(k=0;k<c->pixels;k++){out[k]-=mean;variance+=out[k]*out[k];}
    return variance;
}
RS_API int rs_analyze_gray8(rs_context *c,const uint8_t *im,int w,int h,int stride,rs_result *r) {
    if(!r)return -1;
    reset(r);double begin=timer_ms();int status;
    if(!c||!im||w<1||h<1||stride<w)return result(r,-1,-1);
    if(w!=(int)c->w||h!=(int)c->h)return result(r,1,1);
    double yy=feature(c,im,stride,c->scratch);
    if(yy/c->pixels<4)return result(r,1,2);
    double xy[MAXREF],best=HUGE_VAL,angle=NAN;uint32_t top=0;
    for(uint32_t i=0;i<c->n;i++){
        xy[i]=0;for(size_t k=0;k<c->pixels;k++)xy[i]+=c->scratch[k]*c->refs[i*c->pixels+k];
        double e=yy-xy[i]*xy[i]/c->gram[i*c->n+i];
        if(e<best){best=e;top=i;angle=c->labels[i];}
    }
    /* Same top +/-1 degree neighborhood and 0.01 mixture grid as test.py.
       Duplicate labels retain separate references; all qualifying pairs compete. */
    for(uint32_t i=0;i<c->n;i++)if(fabs(c->labels[i]-c->labels[top])<=1)
    for(uint32_t j=i+1;j<c->n;j++)if(fabs(c->labels[j]-c->labels[top])<=1&&c->labels[j]>c->labels[i]){
        /* Only adjacent distinct angles, exactly the original candidate rule. */
        int adjacent=1;for(uint32_t z=i+1;z<j;z++)if(c->labels[z]>c->labels[i]&&c->labels[z]<c->labels[j]){adjacent=0;break;}
        if(!adjacent)continue;
        for(int step=0;step<=100;step++){
            double f=step/100.0,g=1-f;
            double q=g*g*c->gram[i*c->n+i]+f*f*c->gram[j*c->n+j]+2*f*g*c->gram[i*c->n+j];
            double v=g*xy[i]+f*xy[j];if(q<=1e-12)continue;
            double e=yy-v*v/q;
            if(e<best){best=e;angle=c->labels[i]+f*(c->labels[j]-c->labels[i]);}
        }
    }
    if(!isfinite(angle)||best/yy>.5){status=result(r,1,7);}
    else {r->angle_deg=angle;status=result(r,0,0);}
    r->elapsed_ms=timer_ms()-begin;return status;
}
/* System decoder, no Python runtime. Luminance follows Pillow's RGB->L integers. */
static int decode(const wchar_t *path,uint8_t **out,uint32_t *width,uint32_t *height) {
    *out=NULL;
#ifdef _WIN32
    HRESULT hr=CoInitializeEx(NULL,COINIT_MULTITHREADED);int uninit=SUCCEEDED(hr),status=-4;
    IWICImagingFactory *factory=NULL;IWICBitmapDecoder *decoder=NULL;IWICBitmapFrameDecode *frame=NULL;
    IWICBitmapSource *source=NULL;uint8_t *rgb=NULL;UINT w=0,h=0,count=0;GUID format;
    if(FAILED(hr)&&hr!=RPC_E_CHANGED_MODE)goto done;
    if(FAILED(CoCreateInstance(&CLSID_WICImagingFactory,NULL,CLSCTX_INPROC_SERVER,&IID_IWICImagingFactory,(void**)&factory)))goto done;
    if(FAILED(IWICImagingFactory_CreateDecoderFromFilename(factory,path,NULL,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder)))goto done;
    if(FAILED(IWICBitmapDecoder_GetContainerFormat(decoder,&format))||
       (!IsEqualGUID(&format,&GUID_ContainerFormatPng)&&!IsEqualGUID(&format,&GUID_ContainerFormatJpeg)))goto done;
    if(FAILED(IWICBitmapDecoder_GetFrameCount(decoder,&count))||count!=1||
       FAILED(IWICBitmapDecoder_GetFrame(decoder,0,&frame))||
       FAILED(IWICBitmapFrameDecode_GetSize(frame,&w,&h))||!w||!h||w>4096||h>4096)goto done;
    if(FAILED(WICConvertBitmapSource(&GUID_WICPixelFormat24bppRGB,(IWICBitmapSource*)frame,&source)))goto done;
    rgb=(uint8_t*)malloc((size_t)w*h*3);*out=(uint8_t*)malloc((size_t)w*h);
    if(!rgb||!*out){status=-3;goto done;}
    if(FAILED(IWICBitmapSource_CopyPixels(source,NULL,w*3,w*h*3,rgb)))goto done;
    for(size_t i=0;i<(size_t)w*h;i++)(*out)[i]=(uint8_t)((19595u*rgb[3*i]+38470u*rgb[3*i+1]+7471u*rgb[3*i+2]+32768u)>>16);
    *width=w;*height=h;status=0;
done:
    free(rgb);if(source)IWICBitmapSource_Release(source);if(frame)IWICBitmapFrameDecode_Release(frame);
    if(decoder)IWICBitmapDecoder_Release(decoder);if(factory)IWICImagingFactory_Release(factory);if(uninit)CoUninitialize();
    if(status){free(*out);*out=NULL;}return status;
#else
    (void)path;(void)width;(void)height;return -4;
#endif
}
RS_API int rs_analyze_png_w(rs_context *c,const wchar_t *path,rs_result *r) {
    if(!r)return -1;
    reset(r);
    if(!c||!path)return result(r,-1,-1);
    uint8_t *im=NULL;uint32_t w=0,h=0;double begin=timer_ms();int code=decode(path,&im,&w,&h);
    if(!code)code=rs_analyze_gray8(c,im,(int)w,(int)h,(int)w,r);else result(r,code,code);
    free(im);r->elapsed_ms=timer_ms()-begin;return code;
}
#ifdef RS_TOOL
#ifdef _WIN32
static int train(int argc,wchar_t **argv) {
    if(argc!=13)return 2;
    rs_context *c=(rs_context*)calloc(1,sizeof(*c));if(!c)return 3;
    c->w=(uint32_t)_wtoi(argv[4]);c->h=(uint32_t)_wtoi(argv[5]);c->x=(uint32_t)_wtoi(argv[6]);c->y=(uint32_t)_wtoi(argv[7]);
    c->rw=(uint32_t)_wtoi(argv[8]);c->rh=(uint32_t)_wtoi(argv[9]);c->block=(uint32_t)_wtoi(argv[10]);c->n=MAXREF;
    if(!geometry(c)||!allocate(c)){rs_free(c);return 3;}
    FILE *f=_wfopen(argv[2],L"rt, ccs=UTF-8");if(!f){rs_free(c);return 4;}
    wchar_t line[8192];uint32_t n=0;int code=0;
    while(fgetws(line,8192,f)){
        wchar_t *end=NULL;double label=wcstod(line,&end);
        if(end==line||*end!=L'\t'||!isfinite(label)||fabs(label)>90){code=5;break;}
        wchar_t *group=end+1,*path=wcschr(group,L'\t');if(!path){code=5;break;}*path++=0;
        path[wcscspn(path,L"\r\n")]=0;
        if(wcscmp(group,argv[11])==0)continue;
        if(n>=MAXREF){code=5;break;}
        uint8_t *im=NULL;uint32_t w=0,h=0;
        if(decode(path,&im,&w,&h)||w!=c->w||h!=c->h){free(im);code=6;break;}
        double var=feature(c,im,(int)w,c->refs+n*c->pixels);free(im);
        if(var/c->pixels<4){code=7;break;}c->labels[n++]=label;
    }
    if(ferror(f))code=5;fclose(f);c->n=n;
    if(n<2)code=5;
    if(!code){
        for(uint32_t i=0;i<n;i++)for(uint32_t j=i+1;j<n;j++)if(c->labels[j]<c->labels[i]){
            double a=c->labels[i];c->labels[i]=c->labels[j];c->labels[j]=a;
            for(size_t k=0;k<c->pixels;k++){a=c->refs[i*c->pixels+k];c->refs[i*c->pixels+k]=c->refs[j*c->pixels+k];c->refs[j*c->pixels+k]=a;}
        }
        /* Never overwrite a candidate, and require explicit 65/100 provenance. */
        if(wcscmp(argv[12],L"65")&&wcscmp(argv[12],L"100"))code=5;
        FILE *exists=_wfopen(argv[3],L"rb");if(exists){fclose(exists);code=8;}
        if(!code){
            f=_wfopen(argv[3],L"wb");if(!f)code=4;
            else {
                uint32_t version=1,header[8]={c->w,c->h,c->x,c->y,c->rw,c->rh,c->block,n};
                if(fwrite("RSRTBIN1",1,8,f)!=8||fwrite(&version,4,1,f)!=1||fwrite(header,4,8,f)!=8||
                   fwrite(c->labels,8,n,f)!=n||fwrite(c->refs,8,n*c->pixels,f)!=n*c->pixels)code=9;
                if(fclose(f))code=9;
            }
        }
    }
    printf("{\"status\":%d,\"references\":%u}\n",code,n);rs_free(c);return code;
}
int wmain(int argc,wchar_t **argv) {
    if(argc>1&&!wcscmp(argv[1],L"train"))return train(argc,argv);
    if(argc==4&&!wcscmp(argv[1],L"predict")){
        rs_context *c=NULL;rs_result r;int code=rs_load_w(argv[2],&c);if(code){fprintf(stderr,"model error %d\n",code);return 3;}
        code=rs_analyze_png_w(c,argv[3],&r);
        printf("{\"status\":%d,\"reason\":%d,\"angle\":",r.status,r.reason);
        if(isfinite(r.angle_deg))printf("%.17g",r.angle_deg);else printf("null");
        printf(",\"elapsed_ms\":%.6f}\n",r.elapsed_ms);rs_free(c);return code<0?4:0;
    }
    if((argc==4&&!wcscmp(argv[1],L"gray"))||(argc==5&&!wcscmp(argv[1],L"inspect"))){
        uint8_t *im=NULL;uint32_t w=0,h=0;if(decode(argv[2],&im,&w,&h))return 4;
        int code=0;
        if(argc==4){
            FILE *exists=_wfopen(argv[3],L"rb");if(exists){fclose(exists);free(im);return 8;}
            FILE *f=_wfopen(argv[3],L"wb");if(!f){free(im);return 4;}
            fprintf(f,"P5\n%u %u\n255\n",w,h);if(fwrite(im,1,(size_t)w*h,f)!=(size_t)w*h)code=9;if(fclose(f))code=9;
        } else {
            int x=_wtoi(argv[3]),y=_wtoi(argv[4]);
            if(x<0||y<0||x>=(int)w||y>=(int)h)code=2;
            else printf("{\"x\":%d,\"y_bottom\":%d,\"row_top\":%u,\"gray\":%u}\n",x,y,h-1-y,im[(size_t)(h-1-y)*w+x]);
        }
        free(im);return code;
    }
    fprintf(stderr,"train list.tsv model.bin W H Xtop Ytop ROIW ROIH block exclude|- backlight65|100\npredict model.bin image\ngray image output.pgm\ninspect image x y_bottom\n");return 2;
}
#endif
#endif
