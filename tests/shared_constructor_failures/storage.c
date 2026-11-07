#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <assert.h>
#include "city.h"
#include "city-status.inc"
static void *owners[10000]; static int live,calls,fail_at;
static int slot(void*p){for(int i=0;i<10000;i++)if(owners[i]==p)return i;assert(0);return -1;}
static void record(void*p){assert(p);for(int i=0;i<10000;i++)if(!owners[i]){owners[i]=p;live++;return;}assert(0);}
static void *tc(size_t n,size_t z){if(++calls==fail_at)return NULL;void*p=calloc(n,z);if(p)record(p);return p;}
static void tf(void*p){if(!p)return;int i=slot(p);owners[i]=NULL;live--;free(p);}
static void *tr(void*p,size_t n){if(++calls==fail_at)return NULL;int i=p?slot(p):-1;void*q=realloc(p,n);if(q){if(i>=0)owners[i]=q;else record(q);}return q;}
#define calloc tc
#define realloc tr
#define free tf
static int reader_fail,reader_calls,cloth_frees;
int mesh_read(const BdpEntry*e,Mesh*m){(void)e;reader_calls++;memset(m,0,sizeof *m);if(reader_fail)return -1;m->parts=calloc(1,sizeof *m->parts);if(!m->parts)return -1;m->nparts=1;m->parts[0].nverts=3;return 0;}
void mesh_free(Mesh*m){for(uint32_t i=0;i<m->nparts;i++)free(m->parts[i].pos);free(m->parts);memset(m,0,sizeof *m);}
void cloth_free(Cloth*c){cloth_frees++;free(c);}
#include "storage.inc"
static void reset(void){assert(live==0);calls=fail_at=reader_fail=reader_calls=0;}
static void tidy(Scene*s){for(uint32_t i=0;i<s->nitems;i++)skin_free(s->items[i].skin);for(uint32_t i=0;i<s->nmeshes;i++)mesh_free(&s->meshes[i].mesh);free(s->meshes);free(s->instances);free(s->items);memset(s,0,sizeof *s);assert(live==0);}
static uint64_t hash=1469598103934665603ULL;
static void digest(const void*p,size_t n){const unsigned char*b=p;for(size_t i=0;i<n;i++){hash^=b[i];hash*=1099511628211ULL;}}
static void fixture(void){reset();Scene s={0};Mat4 mat={{0}};for(int i=0;i<16;i++)mat.m[i]=(float)i/2;MeshPart parts[2]={{.nverts=3},{.nverts=5}};Mesh m={.parts=parts,.nparts=2};Skin*sk=new_skin(&m);assert(sk&&sk->nverts==8);digest(sk->bones,8*sizeof *sk->bones);digest(sk->weights,8*sizeof *sk->weights);digest(sk->pos,24*sizeof(float));digest(sk->nrm,24*sizeof(float));int id=new_instance(&s,mat,7);assert(id==0);digest(&s.instances[0],sizeof(Instance));BdpEntry entry={0};Bundle b={.entries=&entry,.nentries=1};int32_t cache=-1;s.city=&b;s.mesh_of_entry=&cache;int mi=(int)scene_mesh_of(&s,0);assert(mi==0);push_item(&s,mi,id,-1,sk);digest(&s.items[0].mesh,sizeof s.items[0].mesh);digest(&s.items[0].instance,sizeof(int));digest(&s.items[0].node,sizeof(int));digest(&s.items[0].world,sizeof(Mat4));digest(&s.meshes[0].mesh.nparts,sizeof(uint32_t));digest(&s.meshes[0].mesh.parts[0],sizeof(MeshPart));printf("fixture %016llx n=%u,%u,%u caps=%u,%u,%u\n",(unsigned long long)hash,s.nmeshes,s.ninstances,s.nitems,s.meshes_cap,s.instances_cap,s.items_cap);tidy(&s);}
static void failures(void){
 for(int count=0;count<2;count++)for(int f=1;f<=5;f++){reset();MeshPart part={.nverts=count?7:0};Mesh m={.parts=&part,.nparts=1};fail_at=f;assert(!new_skin(&m));assert(!live);}
 reset();MeshPart huge[2]={{.nverts=UINT32_MAX},{.nverts=1}};Mesh m={.parts=huge,.nparts=2};assert(!new_skin(&m));assert(!live);assert(calls==1);
 reset();skin_free(NULL);Skin*sk=calloc(1,sizeof *sk);sk->particle=calloc(3,sizeof(int));sk->cloth=(Cloth*)calloc(1,16);int cf=cloth_frees;skin_free(sk);assert(cloth_frees==cf+1&&!live);
 for(int later=0;later<2;later++){
  reset();uint32_t cap=later?3:0;void*old=later?calloc(cap,8):NULL;if(old)memset(old,0x5a,24);fail_at=calls+1;assert(!scene_vector_grow(old,&cap,3,8,"test"));assert(cap==(later?3u:0u));if(old)for(int i=0;i<24;i++)assert(((unsigned char*)old)[i]==0x5a);free(old);assert(!live);
  for(int kind=0;kind<3;kind++){
   reset();Scene s={0};Mat4 mat={{1}};Mesh owned={0};assert(mesh_read(NULL,&owned)==0);Skin*skin=new_skin(&owned);assert(skin);
   if(kind==2)assert(new_instance(&s,mat,-1)==0);
   if(later){if(kind==0){s.nmeshes=s.meshes_cap=1;s.meshes=calloc(1,sizeof *s.meshes);}if(kind==1){s.ninstances=s.instances_cap=1;s.instances=calloc(1,sizeof *s.instances);}if(kind==2){s.nitems=s.items_cap=1;s.items=calloc(1,sizeof *s.items);}}
   void*before=kind==0?(void*)s.meshes:kind==1?(void*)s.instances:(void*)s.items;size_t nbytes=kind==0?sizeof *s.meshes:kind==1?sizeof *s.instances:sizeof *s.items;unsigned char snapshot[sizeof(Instance)+sizeof(DrawItem)+sizeof(SceneMesh)];if(before)memcpy(snapshot,before,nbytes);int live_before=live;fail_at=calls+1;int result=kind==0?scene_add_mesh(&s,owned,NULL):kind==1?new_instance(&s,mat,1):push_item(&s,0,0,-1,skin);assert(result==-2&&s.construction_failed);void*after=kind==0?(void*)s.meshes:kind==1?(void*)s.instances:(void*)s.items;assert(before==after&&live==live_before);assert((kind==0?s.nmeshes:kind==1?s.ninstances:s.nitems)==(uint32_t)later);assert((kind==0?s.meshes_cap:kind==1?s.instances_cap:s.items_cap)==(uint32_t)later);if(before)assert(!memcmp(snapshot,before,nbytes));skin_free(skin);mesh_free(&owned);tidy(&s);
  }
 }
 reset();uint32_t cap=INT_MAX;assert(!scene_vector_grow(NULL,&cap,1,8,"overflow"));assert(cap==INT_MAX&&calls==0);cap=0;assert(!scene_vector_grow(NULL,&cap,0,8,"zero step"));assert(!scene_vector_grow(NULL,&cap,2,SIZE_MAX,"product overflow"));assert(calls==0);
 reset();cap=3;unsigned char *old=calloc(cap,8);memset(old,0x3c,24);unsigned char *grown=scene_vector_grow(old,&cap,3,8,"success");assert(grown&&cap==6&&live==1);for(int i=0;i<24;i++)assert(grown[i]==0x3c);free(grown);assert(!live);
 for(int mode=0;mode<3;mode++){reset();Scene s={0};BdpEntry entry={0};Bundle b={.entries=&entry,.nentries=1};int32_t cache=-1;s.city=&b;s.mesh_of_entry=&cache;if(mode==0)reader_fail=1;if(mode==1)fail_at=2;int result=scene_mesh_of(&s,0);if(mode<2){assert(result==-2&&s.construction_failed&&cache==-1&&!s.nmeshes&&!live);}else{assert(result==0&&cache==0&&s.nmeshes==1);int rc=reader_calls;assert(scene_mesh_of(&s,0)==0&&reader_calls==rc);}tidy(&s);}
 reset();Scene s={0};Mat4 mat={{0}};Mesh empty={0};assert(scene_fail(&s)==-2);assert(new_instance(&s,mat,0)==-2);assert(scene_add_mesh(&s,empty,NULL)==-2);assert(push_item(&s,0,0,0,NULL)==-2);assert(calls==0&&!live);tidy(&s);
 puts("fault injection: PASS (Skin 10 allocation failures, overflow, cleanup, vectors first/later, mesh cache failure/publication, sticky rejection)");
}
int main(void){assert_city_status_contract();fixture();
 failures();
 return 0;}
