#include "ruler_signed.h"
#include <assert.h>
#include <locale.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
    assert(argc==2);setlocale(LC_ALL,"");
    wchar_t path[4096];assert(mbstowcs(path,argv[1],4096)<4096);
    rs_context *ctx=NULL;rs_result r;
    assert(rs_abi_version()==1&&sizeof(rs_result)==48);
    assert(rs_load_w(NULL,&ctx)==-1&&ctx==NULL);
    assert(rs_load_w(path,&ctx)==0&&ctx);
    uint8_t *im=(uint8_t*)calloc(1280*1080,1);assert(im);
    assert(rs_analyze_gray8(ctx,im,1280,1080,1280,&r)==1&&isnan(r.angle_deg));
    memset(im,255,1280*1080);
    assert(rs_analyze_gray8(ctx,im,1280,1080,1280,&r)==1&&isnan(r.angle_deg));
    assert(rs_analyze_gray8(ctx,im,640,480,640,&r)==1&&r.reason==1);
    assert(rs_analyze_gray8(ctx,im,1280,1080,2,&r)==-1);
    assert(rs_analyze_gray8(NULL,im,1280,1080,1280,&r)==-1);
    assert(rs_analyze_gray8(ctx,NULL,1280,1080,1280,&r)==-1);
    assert(rs_analyze_gray8(ctx,im,1280,1080,1280,NULL)==-1);
    unsigned seed=17;
    for(int pass=0;pass<12;pass++) {
        for(int i=0;i<1280*1080;i++){seed=seed*1664525u+1013904223u;im[i]=(uint8_t)(seed>>24);}
        int status=rs_analyze_gray8(ctx,im,1280,1080,1280,&r);
        assert(status==r.status&&(status==0||status==1));
        assert(isfinite(r.elapsed_ms)&&r.elapsed_ms>=0);
        assert(status==0?(isfinite(r.angle_deg)&&fabs(r.angle_deg)<=90):isnan(r.angle_deg));
    }
    rs_free(ctx);rs_free(NULL);free(im);puts("native C boundary and sanitizer checks passed");return 0;
}
