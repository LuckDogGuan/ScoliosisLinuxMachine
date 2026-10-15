/* Fixed-mount shadow reader. No OpenCV, neural runtime, or camera ownership. */
#include "ruler_vision.h"
#include "cJSON.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_NODES 256
#define MAX_ANCHORS 16000
#define MAX_PIXELS (4096u * 4096u)
struct node { double angle, x, y; int usable; };
struct rv_context {
    int w, h, min_area, node_count, anchor_count;
    double max_anchor_error, max_gain, min_gain, max_path_distance;
    uint8_t *background, *mask, *seen;
    uint32_t *queue;
    int anchors[MAX_ANCHORS];
    struct node nodes[MAX_NODES];
    char version[97];
};
static int number(const cJSON *o, const char *key, double *value) {
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble)) return 0;
    *value=v->valuedouble; return 1;
}
static int integer(const cJSON *o,const char *key,int min,int max,int *value) {
    double n;
    if (!number(o,key,&n) || n<min || n>max || n!=floor(n)) return 0;
    *value=(int)n; return 1;
}
void rv_free(rv_context *c) {
    if (!c) return;
    free(c->background); free(c->mask); free(c->seen); free(c->queue); free(c);
}
rv_context *rv_load(const char *text,size_t length,char *error,size_t error_size) {
    rv_context *c=NULL; cJSON *j=NULL;
    const char *why="invalid calibration", *end=NULL;
    int count=0; double schema=0;
    if (!text || length==0 || length>12u*1024u*1024u) goto fail;
    j=cJSON_ParseWithLengthOpts(text,length+1,&end,1);
    if (!j || !number(j,"schema_version",&schema) || schema!=1) goto fail;
    c=(rv_context *)calloc(1,sizeof(*c)); if (!c) { why="allocation failed"; goto fail; }
    if (!integer(j,"width",16,4096,&c->w) || !integer(j,"height",16,4096,&c->h)) goto fail;
    count=c->w*c->h; if ((unsigned)count>MAX_PIXELS) goto fail;
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,"calibration_version");
    if (!cJSON_IsString(v) || !v->valuestring || !v->valuestring[0] || strlen(v->valuestring)>96) goto fail;
    for (const char *p=v->valuestring; *p; ++p) if (!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='-'||*p=='_')) goto fail;
    strcpy(c->version,v->valuestring);
    if (!integer(j,"min_area",1,count,&c->min_area) || !number(j,"max_anchor_error",&c->max_anchor_error) || c->max_anchor_error<=0 || c->max_anchor_error>30) goto fail;
    if (!number(j,"min_gain",&c->min_gain) || !number(j,"max_gain",&c->max_gain) || c->min_gain<0.25 || c->max_gain>4 || c->min_gain>=c->max_gain) goto fail;
    if (!number(j,"max_path_distance",&c->max_path_distance) || c->max_path_distance<1 || c->max_path_distance>30) goto fail;
    c->background=(uint8_t *)malloc((size_t)count); c->mask=(uint8_t *)calloc((size_t)count,1);
    c->seen=(uint8_t *)calloc((size_t)count,1); c->queue=(uint32_t *)malloc((size_t)count*sizeof(uint32_t));
    if (!c->background || !c->mask || !c->seen || !c->queue) { why="allocation failed"; goto fail; }
    v=cJSON_GetObjectItemCaseSensitive(j,"background");
    if (!cJSON_IsArray(v) || cJSON_GetArraySize(v)!=count) goto fail;
    int k=0; const cJSON *item=NULL;
    cJSON_ArrayForEach(item,v) {
        if (!cJSON_IsNumber(item)||item->valuedouble<0||item->valuedouble>255||item->valuedouble!=floor(item->valuedouble)) goto fail;
        c->background[k++]=(uint8_t)item->valueint;
    }
    v=cJSON_GetObjectItemCaseSensitive(j,"mask_indices");
    if (!cJSON_IsArray(v)||cJSON_GetArraySize(v)<10||cJSON_GetArraySize(v)>count) goto fail;
    cJSON_ArrayForEach(item,v) {
        if (!cJSON_IsNumber(item)||item->valuedouble<0||item->valuedouble>=count||item->valuedouble!=floor(item->valuedouble)) goto fail;
        c->mask[item->valueint]=1;
    }
    v=cJSON_GetObjectItemCaseSensitive(j,"anchor_indices");
    if (!cJSON_IsArray(v)||(c->anchor_count=cJSON_GetArraySize(v))<30||c->anchor_count>MAX_ANCHORS) goto fail;
    k=0; cJSON_ArrayForEach(item,v) {
        if (!cJSON_IsNumber(item)||item->valuedouble<0||item->valuedouble>=count||item->valuedouble!=floor(item->valuedouble)||c->mask[item->valueint]) goto fail;
        c->anchors[k++]=item->valueint;
    }
    v=cJSON_GetObjectItemCaseSensitive(j,"nodes");
    if (!cJSON_IsArray(v)||(c->node_count=cJSON_GetArraySize(v))<2||c->node_count>MAX_NODES) goto fail;
    k=0; int usable=0;
    cJSON_ArrayForEach(item,v) {
        struct node *n=&c->nodes[k];
        if (!number(item,"angle_deg",&n->angle)||fabs(n->angle)>180||!number(item,"x",&n->x)||!number(item,"y",&n->y)||n->x<0||n->x>=c->w||n->y<0||n->y>=c->h) goto fail;
        n->usable=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item,"usable")); usable+=n->usable;
        if (k && n->angle<=c->nodes[k-1].angle) goto fail;
        if (k && n->usable && c->nodes[k-1].usable && n->x-c->nodes[k-1].x<1) goto fail;
        ++k;
    }
    if (usable<2) goto fail;
    cJSON_Delete(j); if (error_size) error[0]=0; return c;
fail:
    cJSON_Delete(j); rv_free(c); if (error_size) snprintf(error,error_size,"%s",why); return NULL;
}
int rv_width(const rv_context *c) { return c->w; }
int rv_height(const rv_context *c) { return c->h; }
const char *rv_version(const rv_context *c) { return c->version; }
static double residual(const rv_context *c,const uint8_t *g,int i,int stride,double gain,double offset) {
    return (double)c->background[i]-(gain*g[(i/c->w)*stride+i%c->w]+offset);
}
rv_result rv_analyze(rv_context *c,const uint8_t *g,int w,int h,int stride) {
    rv_result r={0}; r.reason="invalid_image";
    if (!c||!g||w!=c->w||h!=c->h||stride<w||stride>32768) { r.reason="image_size_mismatch"; return r; }
    double sx=0,sy=0,sxx=0,sxy=0;
    for (int k=0;k<c->anchor_count;k++) {
        int i=c->anchors[k]; double x=g[(i/w)*stride+i%w],y=c->background[i];
        sx+=x; sy+=y; sxx+=x*x; sxy+=x*y;
    }
    double den=c->anchor_count*sxx-sx*sx;
    if (den<1) { r.reason="insufficient_contrast"; return r; }
    double gain=(c->anchor_count*sxy-sx*sy)/den,offset=(sy-gain*sx)/c->anchor_count;
    if (gain<c->min_gain||gain>c->max_gain||fabs(offset)>60) { r.reason="exposure_changed"; return r; }
    for (int k=0;k<c->anchor_count;k++) r.anchor_error+=fabs(residual(c,g,c->anchors[k],stride,gain,offset));
    r.anchor_error/=c->anchor_count;
    if (r.anchor_error>c->max_anchor_error) { r.reason="mount_or_image_changed"; return r; }
    int count=w*h; memset(c->seen,0,(size_t)count);
    double best=0,second=0,bx=0,by=0; int area=0;
    for (int i=0;i<count;i++) {
        if (!c->mask[i]||c->seen[i]||residual(c,g,i,stride,gain,offset)<=45) continue;
        int head=0,tail=0,n=0; double mass=0,mx=0,my=0;
        c->queue[tail++]=(uint32_t)i; c->seen[i]=1;
        while (head<tail) {
            int q=(int)c->queue[head++],x=q%w,y=q/w;
            double weight=residual(c,g,q,stride,gain,offset)-20;
            n++; mass+=weight; mx+=weight*x; my+=weight*y;
            int neighbors[4]={x>0?q-1:-1,x<w-1?q+1:-1,y>0?q-w:-1,y<h-1?q+w:-1};
            for(int d=0;d<4;d++) {
                int z=neighbors[d];
                if(z>=0&&!c->seen[z]&&c->mask[z]&&residual(c,g,z,stride,gain,offset)>45) { c->seen[z]=1; c->queue[tail++]=(uint32_t)z; }
            }
        }
        if (mass>best) { second=best; best=mass; bx=mx/mass; by=my/mass; area=n; }
        else if (mass>second) second=mass;
    }
    r.area=area; r.mass=best; r.x=bx; r.y=by;
    if (area<c->min_area) { r.reason="shadow_not_resolved"; return r; }
    if (second>best*0.2) { r.reason="ambiguous_shadows"; return r; }
    for(int k=0;k<c->node_count-1;k++) {
        struct node a=c->nodes[k],b=c->nodes[k+1];
        if (!a.usable||!b.usable||bx<a.x||bx>b.x) continue;
        double t=(bx-a.x)/(b.x-a.x),expected_y=a.y+t*(b.y-a.y);
        if(fabs(by-expected_y)>c->max_path_distance) { r.reason="shadow_off_path"; return r; }
        r.angle_deg=a.angle+t*(b.angle-a.angle); r.valid=1; r.reason="ok"; return r;
    }
    r.reason="outside_calibrated_path"; return r;
}
