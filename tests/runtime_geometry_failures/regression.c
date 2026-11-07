#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "city.h"
#include "city-status.inc"
#include "wrand.h"
static void *owners[4096]; static size_t sizes[4096],live,bytes,calls,fail_at,made,freed,peak;
static void *tm(size_t n){calls++;if(fail_at==calls)return NULL;void*p=malloc(n);assert(p);int i=0;while(owners[i])i++;owners[i]=p;sizes[i]=n;live++;made++;bytes+=n;if(bytes>peak)peak=bytes;return p;}
static void tf(void*p){if(!p)return;int i=0;while(i<4096&&owners[i]!=p)i++;assert(i<4096);owners[i]=NULL;live--;freed++;bytes-=sizes[i];free(p);}
static void *tc(size_t n,size_t z){void*p=tm(n*z);if(p)memset(p,0,n*z);return p;}
static void *tr(void*p,size_t n){calls++;if(fail_at==calls)return NULL;int i=0;if(p){while(i<4096&&owners[i]!=p)i++;assert(i<4096);bytes-=sizes[i];}else{while(owners[i])i++;live++;made++;}void*q=realloc(p,n);assert(q);owners[i]=q;sizes[i]=n;bytes+=n;if(bytes>peak)peak=bytes;return q;}
#define malloc tm
#define calloc tc
#define realloc tr
#define free tf
#define CELL 5.0f
#define STOREY 3.0f
#include "current.inc"
#include "building.c"
static unsigned seed=1;
uint32_t wrand_lf(void){seed=seed*1664525+1013904223;return seed;}
float wrand_frand(void){return (wrand_lf()>>8)*(1.0f/16777216);}
float group_num(Group g,const char*k,float d){(void)g;(void)k;return d;}
static Scene s;static SceneBuilding sb;static SceneMesh meshes[1];
static void init(void){assert_city_status_contract();memset(&s,0,sizeof s);memset(&sb,0,sizeof sb);memset(meshes,0,sizeof meshes);s.buildings=&sb;s.nbuildings=1;s.meshes=meshes;s.nmeshes=1;sb.mesh=0;sb.open_part=4;s.cube_tint[0]=127;s.cube_tint[1]=200;s.cube_tint[2]=255;float lo[3]={0},hi[3]={10,10,12};assert(!building_init(&sb.b,lo,hi,2,1,3));
#if defined(RAMPAGE_LOW_MEMORY) && !defined(RAMPAGE_FIXED_BUILDING_ROWS)
sb.quad=calloc(3,sizeof *sb.quad);
#endif
meshes[0].mesh.nparts=5;meshes[0].mesh.parts=calloc(5,sizeof(MeshPart));for(int j=0;j<4;j++){sb.outline[j][0]=1;sb.outline[j][1]=0.4f;sb.outline[j][2]=4;sb.outline[j][3]=2.5f;}for(int j=0;j<3;j++)sb.crack_tint[j]=0.6f;sb.crack_k=0.2f; s.occ_setup=1;s.noccupants=2;s.nmilitary=1;s.fill_rate=1;s.nonmilitary_rate=0.5f;
}
static void done(void){mesh_free(&meshes[0].mesh);building_free(&sb.b);
#if defined(RAMPAGE_LOW_MEMORY) && !defined(RAMPAGE_FIXED_BUILDING_ROWS)
free(sb.quad);
#endif
free(s.order);free(s.slots);free(s.obstacle_of);free(s.obstacles);assert(!live&&!bytes);}
static void dump(FILE*f){for(int i=1;i<5;i++){MeshPart*p=&meshes[0].mesh.parts[i];fwrite(&p->nverts,sizeof p->nverts,1,f);fwrite(&p->ntris,sizeof p->ntris,1,f);fwrite(&p->rev,sizeof p->rev,1,f);fwrite(p->pos,12,p->nverts,f);fwrite(p->col,16,p->nverts,f);fwrite(p->tris,12,p->ntris,f);int flags=!!p->uv+2*!!p->nrm;fwrite(&flags,4,1,f);if(p->uv)fwrite(p->uv,8,p->nverts,f);if(p->nrm)fwrite(p->nrm,12,p->nverts,f);}}
int main(int argc,char**argv){assert(argc>=2);init();if(!strcmp(argv[1],"success")){FILE*f=fopen(argv[2],"wb");assert(f);for(int phase=0;phase<12;phase++){sb.b.nwindows=phase==11?0:1;sb.b.window[0]=(BuildingWindow){0};sb.b.window[0].o=1;sb.b.window[0].column=1;sb.b.window[0].x0=1;sb.b.window[0].z0=0.5;sb.b.window[0].w=2;sb.b.window[0].h=1.5;sb.b.window[0].open=phase/12.0f;sb.b.window[0].opens_h=phase%2;for(int o=0;o<3;o++)for(int c=0;c<4;c++)sb.b.cell[o][c].state=phase%3;for(int o=0;o<=3;o++)for(int face=0;face<3;face++)for(int c=0;c<=2;c++)sb.b.vert[o][face][c].level=phase%4;meshes[0].mesh.parts[1].material=phase%2;facade_passes(&s,&sb);open_windows(&s,&sb);dump(f);}for(int o=0;o<3;o++)for(int c=0;c<4;c++)sb.b.cell[o][c].state=0;scene_occupant_slots(&s,0);scene_occupant_pad(&s,4);obstacle_add(&s,7);obstacle_boxes(&s);if(s.norder)fwrite(s.order,sizeof *s.order,s.norder,f);if(s.nslots)fwrite(s.slots,sizeof *s.slots,s.nslots,f);fwrite(s.obstacles,sizeof *s.obstacles,s.nobstacles,f);fclose(f);done();printf("PASS success calls=%zu owners=%zu freed=%zu peak=%zu zero_live\n",calls,made,freed,peak);return 0;}
int target=atoi(argv[2]);if(!strcmp(argv[1],"slots_later")){scene_occupant_slots(&s,0);assert(s.nslots>0);}facade_passes(&s,&sb);sb.b.nwindows=1;sb.b.window[0].o=1;open_windows(&s,&sb);scene_occupant_pad(&s,1);obstacle_add(&s,7);obstacle_boxes(&s);void*old_slots=s.slots;int old_nslots=s.nslots;void*old_order=s.order,*old_ob=s.obstacle_of,*old_boxes=s.obstacles;int old_norder=s.norder;calls=0;fail_at=target;
if(!strcmp(argv[1],"facade_unlit"))meshes[0].mesh.parts[1].material=1;
if(!strcmp(argv[1],"facade")||!strcmp(argv[1],"facade_unlit"))facade_passes(&s,&sb);else if(!strcmp(argv[1],"windows"))open_windows(&s,&sb);else if(!strcmp(argv[1],"order"))scene_occupant_pad(&s,4);else if(!strcmp(argv[1],"slots")||!strcmp(argv[1],"slots_later"))scene_occupant_slots(&s,0);else if(!strcmp(argv[1],"obstacle"))obstacle_add(&s,8);else if(!strcmp(argv[1],"boxes"))obstacle_boxes(&s);else abort();
assert(s.construction_failed);if(!strcmp(argv[1],"slots_later")){assert(s.nslots==old_nslots);if(target==1)assert(s.slots==old_slots);}assert(calls>=fail_at);if(!strcmp(argv[1],"order")){assert(s.order==old_order);assert(s.norder==old_norder);}if(!strcmp(argv[1],"obstacle")){assert(s.obstacle_of==old_ob);assert(s.nobstacles==1);}if(!strcmp(argv[1],"boxes"))assert(s.obstacles==old_boxes);size_t before=calls;facade_passes(&s,&sb);open_windows(&s,&sb);scene_occupant_pad(&s,3);scene_occupant_slots(&s,0);obstacle_boxes(&s);assert(calls==before);

fail_at=0;done();printf("PASS %s fault=%d calls=%zu owners=%zu freed=%zu zero_live\n",argv[1],target,calls,made,freed);return 0;}
