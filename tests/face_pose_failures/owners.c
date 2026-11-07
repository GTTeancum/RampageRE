#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <math.h>
#include "city.h"
#include "city-status.inc"
#define _stricmp strcasecmp
static void *owners[100]; static int live,calls,fail_at;
static void record(void*p){if(!p)return;for(int i=0;i<100;i++)if(!owners[i]){owners[i]=p;live++;return;}abort();}
static void *tm(size_t n){if(++calls==fail_at)return NULL;void*p=malloc(n);record(p);return p;}
static void *tc(size_t n,size_t z){if(++calls==fail_at)return NULL;void*p=calloc(n,z);record(p);return p;}
static void tf(void*p){if(!p)return;for(int i=0;i<100;i++)if(owners[i]==p){owners[i]=NULL;live--;free(p);return;}abort();}
#define malloc tm
#define calloc tc
#define free tf
/* Reader double obeys the accepted zero/free-safe failure contract. Two owners emulate a keyed Clip. */
int clip_read(const BdpEntry*e,Clip*c){memset(c,0,sizeof *c);c->trans=calloc(1,sizeof *c->trans);if(!c->trans)return -1;c->rot=calloc(1,sizeof *c->rot);if(!c->rot){free(c->trans);memset(c,0,sizeof *c);return -1;}c->data=e->data;c->bones=5;c->duration=1;return 0;}
void clip_free(Clip*c){free(c->trans);free(c->rot);memset(c,0,sizeof *c);}
const char *bdp_name(const Bundle*b,uint32_t i){return b->names[i];}
#include "extracted.inc"
static TreeNode nodes[5]={{.name="Bip01",.parent=-1,.mirror=0},{.name="neck",.parent=0,.mirror=1},{.name="Bip01 Head",.parent=1,.mirror=2},{.name="Bip01 L Eye",.parent=2,.mirror=4},{.name="Bip01 R Eye",.parent=2,.mirror=3}};
static const char *names[]={"000Test","Other","Eyegore_Skeleton"};
static BdpEntry entries[]={{.type=ET_ANIM,.name=0,.data=(const uint8_t*)"a"},{.type=ET_ANIM,.name=1,.data=(const uint8_t*)"b"},{.type=ET_TREE,.name=2}};
static Bundle bundle={.entries=entries,.nentries=2,.names=names,.nnames=3};
static City status_owner; static Scene s; static Instance in;static float pose[5][7],held[5][7];
static void setup(void){assert_city_status_contract();assert(!live);memset(&s,0,sizeof s);memset(&status_owner,0,sizeof status_owner);s.owner=&status_owner;memset(&in,0,sizeof in);s.instances=&in;s.ninstances=1;s.nmon=1;s.mon[0].inst=0;s.mon[0].anims=&bundle;s.mon[0].bundle=&bundle;in.tree.nodes=nodes;in.tree.n=5;in.pose=pose;in.held=held;bundle.nentries=2;calls=fail_at=0;for(int i=0;i<5;i++){for(int j=0;j<7;j++)pose[i][j]=0;pose[i][0]=i+1;pose[i][1]=i+2;pose[i][6]=1;}memcpy(held,pose,sizeof held);}
static void tidy(void){
/* Exact instance face/body owner lines from scene_free, generated alongside function extracts. */
#include "teardown.inc"
assert(!live);}

static void healthy(void){setup();for(int i=0;i<1000;i++){assert(!scene_monster_face(&s,0,i%2?"Test":"Other",i%2));scene_monster_face_blend(&s,0,.25f);assert(!scene_monster_play(&s,0,i%2?"Test":"Other"));in.blend_left=in.blend_time=1;
for(int j=0;j<5;j++){float a=(i*5+j)*.013f;held[j][3]=sinf(a);held[j][4]=held[j][5]=0;held[j][6]=cosf(a);in.face_held[j][3]=0;in.face_held[j][4]=sinf(a);in.face_held[j][5]=0;in.face_held[j][6]=cosf(a);}
scene_monster_mirror(&s,0,!in.mirrored);assert(live==6);fwrite(in.face_mask,1,5,stdout);fwrite(held,1,sizeof held,stdout);fwrite(in.face_held,1,sizeof held,stdout);}fprintf(stderr,"allocations=%d retained=%d\n",calls,live);tidy();setup();bundle.nentries=3;assert(!scene_monster_face(&s,0,"Test",0));assert(in.face_mask[0]&&in.face_mask[1]&&!in.face_mask[2]);tidy();setup();in.tree.n=0;assert(!scene_monster_face(&s,0,"Test",0));scene_monster_face_blend(&s,0,1);scene_monster_mirror(&s,0,1);tidy();}
#ifdef CANDIDATE
static void faults(void){
setup();status_owner.load_failed=1;
assert(scene_monster_face(&s,0,"Test",1)==-2&&s.construction_failed&&!calls&&!live);
assert(scene_monster_play(&s,0,"Test")==-2&&!calls);tidy();

for(int f=1;f<=3;f++){setup();fail_at=f;assert(scene_monster_face(&s,0,"Test",1)==-2&&s.construction_failed&&!in.has_face&&!in.face_mask&&!live);int c=calls;assert(scene_monster_face(&s,0,"Test",1)==-2);assert(scene_monster_play(&s,0,"Test")==-2);scene_monster_face_blend(&s,0,1);scene_monster_mirror(&s,0,1);assert(calls==c);tidy();}
for(int body=0;body<=1;body++)for(int f=1;f<=2;f++){setup();assert(!scene_monster_face(&s,0,"Test",1));assert(!scene_monster_play(&s,0,"Test"));Clip old=body?in.clip:in.face;int n=live;uint8_t mask[5];memcpy(mask,in.face_mask,5);fail_at=calls+f;assert((body?scene_monster_play(&s,0,"Other"):scene_monster_face(&s,0,"Other",0))==-2);assert(s.construction_failed&&live==n&&!memcmp(&old,body?&in.clip:&in.face,sizeof old)&&!memcmp(mask,in.face_mask,5));tidy();}
setup();fail_at=1;scene_monster_face_blend(&s,0,1);assert(s.construction_failed&&!in.face_held&&!live);tidy();
for(int body=0;body<=1;body++)for(int face=0;face<=1;face++){setup();if(face)scene_monster_face_blend(&s,0,1);in.blend_left=body;float old[5][7],oldf[5][7];memcpy(old,held,sizeof old);if(face)memcpy(oldf,in.face_held,sizeof oldf);int n=live,c=calls;fail_at=calls+1;scene_monster_mirror(&s,0,1);assert(live==n);if(body||face)assert(s.construction_failed&&!in.mirrored&&calls==c+1&&!memcmp(old,held,sizeof old)&&(!face||!memcmp(oldf,in.face_held,sizeof oldf)));else assert(!s.construction_failed&&in.mirrored&&calls==c);tidy();}
setup();assert(scene_monster_face(&s,0,"missing",1)==-1&&!s.construction_failed&&!calls);assert(scene_monster_play(&s,0,"missing")==-1&&!s.construction_failed&&!calls);tidy();puts("faults and sticky reentry PASS");}
#endif
int main(int argc,char**argv){assert(argc==2);if(!strcmp(argv[1],"healthy"))healthy();
#ifdef CANDIDATE
else faults();
#endif
return 0;}
