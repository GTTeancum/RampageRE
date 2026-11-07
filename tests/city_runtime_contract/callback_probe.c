#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "callbacks.inc"
typedef struct { int failed, calls, fail_inside; } Ctx;
static int fail(void*v){return ((Ctx*)v)->failed;}
static const char *arg_failure(Ctx*c){c->failed=1;return "inherited";}
static int shape(void*v,const char*n,float lo[3],float hi[3]){Ctx*c=v;assert(!c->failed);c->calls++;(void)n;lo[0]=3;hi[0]=4;if(c->fail_inside)c->failed=1;return 1;}
static int rubble_shape(void*v,const char*n,float lo[3],float hi[3]){shape(v,n,lo,hi);return 0;}
static Group find(void*v,const char*b,const char*n){Ctx*c=v;assert(!c->failed);c->calls++;(void)b;(void)n;if(c->fail_inside)c->failed=1;return (Group){NULL,7};}
static Group find_t(void*v,const char*n){return find(v,"",n);}
static int cell(void*v,int b,int st,int col,float out[3]){(void)b;(void)st;(void)col;float tmp[3];return shape(v,"",out,tmp);}
static int point(void*v,const float p[3],float r,float out[3]){(void)p;(void)r;float tmp[3];return shape(v,"",out,tmp);}
static float clip(void*v,const char*n,const char*s){float a[3],b[3];(void)s;return (float)shape(v,n,a,b);}
static const char *swarm_str(People*p,int o,const char*prefix,const char*key){(void)o;(void)prefix;(void)key;((Ctx*)p->failed_ctx)->failed=1;return "local fallback after inherited failure";}
#define CHECK_SHAPE(type,mod) do { \
 Ctx c={0};type h={0};h.ctx=&c;h.failed=fail;h.shape_box=shape;float a[3]={0},b[3]={0}; \
 assert(mod##_host_shape_box(&h,"healthy",a,b)==1&&c.calls==1&&a[0]==3); \
 a[0]=0;assert(!mod##_host_shape_box(&h,arg_failure(&c),a,b)&&c.calls==1&&a[0]==0); \
 c.failed=0;c.fail_inside=1;int consumed=0;if(mod##_host_shape_box(&h,"callback fails after write",a,b))consumed++; \
 assert(c.failed&&c.calls==2&&!consumed&&a[0]==3); \
 printf(#mod " shape: healthy=1 argument_failure_dispatch=0 callback_failure_consume=0\n"); } while(0)
#define CHECK_FIND(type,mod) do {Ctx c={0};type h={0};h.ctx=&c;h.failed=fail;h.find=find; \
 assert(mod##_host_find(&h,"book","healthy").at==7&&c.calls==1); \
 c.fail_inside=1;assert(!mod##_host_find(&h,"book","failed output").at&&c.calls==2); \
 assert(!mod##_host_find(&h,"book","later").at&&c.calls==2); } while(0)
int main(void){
 CHECK_SHAPE(ArmyHost,army);CHECK_SHAPE(VehicleHost,vehicle);CHECK_SHAPE(RailHost,rail);CHECK_SHAPE(ParadeHost,parade);CHECK_SHAPE(BossHost,boss);CHECK_SHAPE(StreetHost,streetobj);
 CHECK_FIND(ArmyHost,army);CHECK_FIND(VehicleHost,vehicle);CHECK_FIND(RailHost,rail);CHECK_FIND(ParadeHost,parade);CHECK_FIND(EdibleHost,edible);CHECK_FIND(BossHost,boss);CHECK_FIND(GameHost,game);
 Ctx c={0};float a[3]={0},b[3]={0};RubbleHost rh={0};rh.ctx=&c;rh.failed=fail;rh.tmpl=find_t;rh.shape_box=rubble_shape;
 assert(reveal_host_tmpl(&rh,"healthy").at==7);assert(!reveal_host_shape_box(&rh,"healthy",a,b));c.fail_inside=1;assert(reveal_host_shape_box(&rh,"failed",a,b)==-1);assert(!reveal_host_tmpl(&rh,"later").at&&c.calls==3);
 c=(Ctx){0};EdibleHost eh={0};eh.ctx=&c;eh.failed=fail;eh.cell_place=cell;assert(edible_host_cell_place(&eh,0,0,0,a));c.fail_inside=1;assert(!edible_host_cell_place(&eh,0,0,0,a));assert(c.calls==2&&a[0]==3);
 c=(Ctx){0};GameHost gh={0};gh.ctx=&c;gh.failed=fail;gh.rubble_point=point;assert(game_host_rubble_point(&gh,b,1,a));c.fail_inside=1;assert(!game_host_rubble_point(&gh,b,1,a));assert(c.calls==2);
 c=(Ctx){0};People p={0};p.failed=fail;p.failed_ctx=&c;p.clip_seconds=clip;p.ctx=&c;p.find=find_t;p.find_ctx=&c;
 assert(call_clip(&p,"healthy","shape")==1&&c.calls==1);assert(!call_clip(&p,arg_failure(&c),"shape")&&c.calls==1);c.failed=0;c.fail_inside=1;assert(!call_clip(&p,"callback fails","shape")&&c.calls==2);
 c=(Ctx){0};assert(call_find(&p,"healthy").at==7);c.fail_inside=1;assert(!call_find(&p,"failed").at&&c.calls==2);c=(Ctx){0};swarm_sound(&p,0,"sound");assert(c.failed&&p.nev==0);
 puts("nine actual typed Host families + People callbacks: healthy callbacks required; argument and output failures suppressed; People suppressed-event annotation safe");return 0;
}
