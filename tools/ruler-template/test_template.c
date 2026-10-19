#include "../ruler-signed/ruler_signed.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
int main(int argc,char **argv) {
    assert(argc==2);wchar_t path[4096];
    assert(mbstowcs(path,argv[1],4096)<4096);
    rs_context *c=NULL;assert(rs_load_w(path,&c)==0&&c);
    assert(sizeof(rs_result)==48&&rs_abi_version()==1);
    unsigned char *image=calloc(1280*1080,1);assert(image);rs_result r;
    for(int i=0;i<2;i++) {
        memset(image,i?255:0,1280*1080);
        assert(rs_analyze_gray8(c,image,1280,1080,1280,&r)==1);
        assert(r.reason==2&&isnan(r.angle_deg));
    }
    assert(rs_analyze_gray8(c,image,1279,1080,1280,&r)==1&&r.reason==1&&isnan(r.angle_deg));
    assert(rs_analyze_gray8(c,NULL,1280,1080,1280,&r)==-1&&isnan(r.angle_deg));
    for(int i=0;i<1280*1080;i++)image[i]=(unsigned char)((i*73+(i/1280)*11)%256);
    int code=rs_analyze_gray8(c,image,1280,1080,1280,&r);
    assert(code==0||code==1);assert(code==0?isfinite(r.angle_deg):isnan(r.angle_deg));
    free(image);rs_free(c);rs_free(NULL);
    c=(rs_context*)1;assert(rs_load_w(L"/nonexistent/template-model.bin",&c)<0&&!c);
    puts("template native ASan/UBSan PASS");return 0;
}
