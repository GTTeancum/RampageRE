#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "city.h"

/* Synthetic City boundary; no full city construction is executed. */
#include "city-status.inc"
/* Crowd publication belongs to full city_build, not these lighting fixtures. */
int sound_effect_start(const char *name, int loop) { (void)name; (void)loop; assert(!"unexpected crowd audio"); return -1; }
void sound_effect_set_point(int id, const float point[3]) { (void)id; (void)point; assert(!"unexpected crowd audio"); }
#include "constants.inc"

typedef struct { void *p; size_t n; } Allocation;
static Allocation allocations[1024];
static size_t live, bytes, peak, calls, fail_at, made, released;
static void check_guards(Allocation a)
{
    for (size_t k = 0; k < 64; k++) assert(((unsigned char *)a.p)[a.n + k] == 0xA5);
}
static size_t locate(void *p)
{
    for (size_t k = 0; k < 1024; k++) if (allocations[k].p == p) return k;
    assert(!"free/realloc of an unowned allocation"); return 0;
}
static void track(void *p, size_t n)
{
    assert(p && live < 1024 && n <= 1048576);
    size_t k = 0;
    while (allocations[k].p) k++;
    allocations[k] = (Allocation){ p, n };
    memset((unsigned char *)p + n, 0xA5, 64);
    live++; bytes += n; made++;
    if (bytes > peak) peak = bytes;
}
static int denied(void) { return ++calls == fail_at; }
static void *test_malloc(size_t n)
{
    if (denied()) return NULL;
    assert(n <= 1048576); void *p = malloc(n + 64); track(p, n); return p;
}
static void *test_calloc(size_t n, size_t z)
{
    assert(!z || n <= SIZE_MAX / z);
    void *p = test_malloc(n * z); if (p) memset(p, 0, n * z); return p;
}
static void *test_realloc(void *p, size_t n)
{
    if (denied()) return NULL;
    if (!p) { void *q = malloc(n + 64); track(q, n); return q; }
    size_t k = locate(p); check_guards(allocations[k]);
    size_t old = allocations[k].n;
    void *q = realloc(p, n + 64); assert(q);
    allocations[k] = (Allocation){ q, n };
    memset((unsigned char *)q + n, 0xA5, 64);
    bytes = bytes - old + n; if (bytes > peak) peak = bytes;
    return q;
}
static void test_free(void *p)
{
    if (!p) return;
    size_t k = locate(p); check_guards(allocations[k]);
    bytes -= allocations[k].n; live--; released++;
    allocations[k] = (Allocation){ 0 }; free(p);
}
static void start(size_t failure)
{
    assert(!live && !bytes); calls = 0; fail_at = failure;
}

#include <math.h>
static int lighting_calls;
static void shade_neighbours(Scene *s, uint32_t i) { (void)s; (void)i; }
static void light_building(Scene *s, SceneBuilding *b, int play) { (void)s; (void)b; (void)play; lighting_calls++; }
void water_free(Water *p) { (void)p; assert(0); }
void cloth_free(Cloth *p) { (void)p; assert(0); }
void tree_free(Tree *p) { (void)p; assert(0); }
void clip_free(Clip *p) { (void)p; assert(0); }
void people_free(People *p) { (void)p; assert(0); }
void building_free(Building *p) { (void)p; } /* synthetic Building has no allocations */
void bdp_free(Bundle *p) { (void)p; assert(0); }
#define malloc test_malloc
#define calloc test_calloc
#define realloc test_realloc
#define free test_free
#ifdef BASELINE
#include "baseline.inc"
#else
#include "candidate.inc"
#endif
#undef malloc
#undef calloc
#undef realloc
#undef free
static int copy_mesh(const Mesh *m, Mesh *out) {
#ifdef BASELINE
    *out=mesh_copy_lit(m); return 0;
#else
    return mesh_copy_lit(m,out);
#endif
}
static int overlay(Scene *s, SceneBuilding *b) {
#ifdef BASELINE
    ground_cracks_make(s,b); return 0;
#else
    return ground_cracks_make(s,b);
#endif
}
static uint64_t hb(uint64_t h,const void *p,size_t n) { const unsigned char *q=p; while(n--) h=(h^*q++)*UINT64_C(1099511628211); return h; }
static uint64_t mh(const Mesh *m) {
 uint64_t h=hb(UINT64_C(14695981039346656037),&m->nparts,sizeof m->nparts);
 for(unsigned i=0;i<m->nparts;i++) { const MeshPart *p=m->parts+i;
 unsigned meta[]={p->material,p->nverts,p->ntris,(unsigned)p->blend,p->rev}; h=hb(h,meta,sizeof meta);
 const void *a[]={p->pos,p->nrm,p->uv,p->col,p->tris}; size_t z[]={12*p->nverts,12*p->nverts,8*p->nverts,16*p->nverts,12*p->ntris};
 for(int j=0;j<5;j++){ unsigned yes=a[j]!=NULL; h=hb(h,&yes,sizeof yes); if(a[j]) h=hb(h,a[j],z[j]); }
 } return h;
}
static void dump_mesh(FILE *f,const Mesh *m) {
 fwrite(&m->nparts,sizeof m->nparts,1,f);fwrite(&m->lead,sizeof m->lead,1,f);
 for(unsigned i=0;i<m->nparts;i++){const MeshPart *p=m->parts+i;unsigned meta[]={p->material,p->nverts,p->ntris,(unsigned)p->blend,p->rev};fwrite(meta,sizeof meta,1,f);
 const void *a[]={p->pos,p->nrm,p->uv,p->col,p->tris};size_t z[]={12*p->nverts,12*p->nverts,8*p->nverts,16*p->nverts,12*p->ntris};
 for(int j=0;j<5;j++){unsigned yes=a[j]!=NULL;fwrite(&yes,sizeof yes,1,f);if(a[j]&&z[j])fwrite(a[j],z[j],1,f);}}
}
static FILE *dump_open(const char *kind,int shape){char path[80];
#ifdef BASELINE
 const char *prefix="baseline";
#else
 const char *prefix="candidate";
#endif
 snprintf(path,sizeof path,"%s_%s_%d.bin",prefix,kind,shape);FILE *f=fopen(path,"wb");assert(f);return f;}
static Mesh source(int shape) {
 Mesh m={0}; m.nparts=shape==2?0:4; m.parts=test_calloc(m.nparts?m.nparts:1,sizeof *m.parts);
 for(unsigned i=0;i<m.nparts;i++) { MeshPart *p=m.parts+i; p->nverts=(shape==1||shape==3)?0:3; p->ntris=(shape==1||shape==3)?0:1; p->material=17+i;p->blend=i%3;
 p->pos=test_calloc(9,sizeof(float));p->tris=test_calloc(3,sizeof(uint32_t));
 for(int j=0;j<9;j++) p->pos[j]=(float)(j-3)*0.75f;
 p->tris[0]=0;p->tris[1]=1;p->tris[2]=2;
 if(i<2) {p->nrm=test_calloc(9,sizeof(float)); for(int j=0;j<9;j++)p->nrm[j]=j%3==1?1:0;}
 if(i<3) p->uv=test_calloc(6,sizeof(float));
 if(i==1||i==2) {p->col=test_calloc(12,sizeof(float));for(int j=0;j<12;j++)p->col[j]=(float)j/12;}
 if(shape==3){test_free(p->pos);p->pos=NULL;test_free(p->tris);p->tris=NULL;}
 } return m;
}
static void binit(SceneBuilding *b,int g,int top) { b->ground=g;b->top=top;b->mesh=1;b->gf_item=b->gf_part=-1;b->b.width=2;b->b.depth=2;b->crack_k=0.125f;for(int j=0;j<4;j++)b->roof_inst[j]=-1; }
static Scene setup(int shape) {
 Scene s={0}; s.meshes_cap=32;s.meshes=test_calloc(32,sizeof *s.meshes);s.nmeshes=2;s.meshes[0].mesh=source(shape);
 s.meshes[1].mesh.nparts=3;s.meshes[1].mesh.parts=test_calloc(3,sizeof(MeshPart));s.meshes[1].mesh.parts[2].material=99;
 s.nitems=5;s.items=test_calloc(5,sizeof *s.items);for(int i=0;i<5;i++)s.items[i].instance=i;
 s.nbuildings=2;s.buildings=test_calloc(2,sizeof *s.buildings);binit(s.buildings,0,1);binit(s.buildings+1,2,3);
 s.nscenery=1;s.scenery=test_calloc(1,sizeof *s.scenery);binit(s.scenery,4,-1);
 return s;
}
static uint64_t sh(Scene *s) {
 uint64_t h=0;for(unsigned i=0;i<s->nmeshes;i++){uint64_t t=mh(&s->meshes[i].mesh);h=hb(h,&t,sizeof t);}
 for(unsigned i=0;i<s->nbuildings;i++){SceneBuilding *b=s->buildings+i;if(b->gf_item>=0){MeshPart *p=&s->meshes[s->items[b->gf_item].mesh].mesh.parts[b->gf_part];assert(p->ntris==0);h=hb(h,p->tris,12*b->gf_ntris);h=hb(h,b->gf_vert,3*p->nverts);}}
 return h;
}
int main(int argc,char **argv) {
 assert_city_status_contract();
 if(argc==4) { start(0);int shape=atoi(argv[3]);size_t point=(size_t)atoi(argv[2]);
 if(!strcmp(argv[1],"copy")){Mesh m=source(shape),o={0};calls=0;fail_at=point;int r=copy_mesh(&m,&o);printf("returned=%d calls=%zu\n",r,calls);mesh_free(&o);mesh_free(&m);}
 else {Scene s=setup(shape);assert(!copy_mesh(&s.meshes[0].mesh,&s.meshes[2].mesh));s.nmeshes=3;s.items[0].mesh=2;s.buildings[0].nlit_items=1;s.buildings[0].lit_item[0]=0;s.buildings[0].lit_src[0]=0;calls=0;fail_at=point;int r=overlay(&s,s.buildings);printf("returned=%d calls=%zu gf=%d col=%p\n",r,calls,s.buildings[0].gf_item,s.meshes[2].mesh.nparts>4?(void*)s.meshes[2].mesh.parts[4].col:NULL);scene_free(&s);}
 assert(!live&&!bytes);return 0;}
 size_t failures=0,successes=0;
 for(int shape=0;shape<4;shape++) {
 #ifdef BASELINE
 if(shape==3) continue;
#endif
 start(0);Mesh m=source(shape),o={0};calls=0;assert(!copy_mesh(&m,&o));size_t total=calls;printf("copy shape=%d calls=%zu hash=%016llx\n",shape,total,(unsigned long long)mh(&o));FILE *dump=dump_open("copy",shape);dump_mesh(dump,&o);assert(!fclose(dump));mesh_free(&o);mesh_free(&m);assert(!live);successes++;
#ifndef BASELINE
 for(size_t j=1;j<=total;j++){start(0);m=source(shape);Mesh empty={0};o=empty;uint64_t h=mh(&m);size_t oldlive=live,oldbytes=bytes;calls=0;fail_at=j;assert(copy_mesh(&m,&o)==-1);assert(calls==j);assert(!memcmp(&o,&empty,sizeof o));assert(mh(&m)==h&&live==oldlive&&bytes==oldbytes);mesh_free(&o);mesh_free(&m);assert(!live);failures++;}
#endif
 start(0);Scene s=setup(shape);calls=0;lighting_calls=0;assert(!build_tail(&s));total=calls;assert(lighting_calls==3);printf("scene shape=%d calls=%zu hash=%016llx\n",shape,total,(unsigned long long)sh(&s));dump=dump_open("scene",shape);for(unsigned z=0;z<s.nmeshes;z++)dump_mesh(dump,&s.meshes[z].mesh);for(unsigned z=0;z<s.nbuildings;z++){SceneBuilding *b=s.buildings+z;if(b->gf_item>=0){MeshPart *p=&s.meshes[s.items[b->gf_item].mesh].mesh.parts[b->gf_part];fwrite(p->tris,12*b->gf_ntris,1,dump);fwrite(b->gf_vert,3*p->nverts,1,dump);}}assert(!fclose(dump));scene_free(&s);scene_free(&s);assert(!live);successes++;
#ifndef BASELINE
 for(size_t j=1;j<=total;j++){start(0);s=setup(shape);calls=0;fail_at=j;lighting_calls=0;assert(build_tail(&s)==-1);Scene empty={0};assert(!memcmp(&s,&empty,sizeof s));assert(lighting_calls==0);scene_free(&s);assert(!live&&!bytes);failures++;}
#endif
 }
#ifndef BASELINE
 /* Overlay failure preserves a previously published overlay and its mapping. */
 for(int shape=0;shape<2;shape++)for(size_t j=1;j<=6;j++) {start(0);Scene s=setup(shape);assert(!build_tail(&s));SceneBuilding before=s.buildings[0];Mesh beforem=s.meshes[s.items[0].mesh].mesh;uint64_t h=sh(&s);size_t oldlive=live,oldbytes=bytes;calls=0;fail_at=j;assert(overlay(&s,s.buildings)==-1);assert(!memcmp(&before,s.buildings,sizeof before));assert(!memcmp(&beforem,&s.meshes[s.items[0].mesh].mesh,sizeof beforem));assert(h==sh(&s)&&live==oldlive&&bytes==oldbytes);scene_free(&s);assert(!live&&!bytes);failures++;}
 /* No applicable ground item or facade: successful no-op with no allocations. */
 for(int kind=0;kind<3;kind++){start(0);Scene s=setup(0);if(kind==1){s.buildings[0].nlit_items=1;s.buildings[0].lit_item[0]=0;s.buildings[0].lit_src[0]=0;s.buildings[0].mesh=-1;}else if(kind==2){s.buildings[0].nlit_items=1;s.buildings[0].lit_item[0]=0;s.buildings[0].lit_src[0]=1;}SceneBuilding before=s.buildings[0];calls=0;fail_at=1;assert(!overlay(&s,s.buildings));assert(calls==0&&!memcmp(&before,s.buildings,sizeof before));scene_free(&s);assert(!live);successes++;}
#endif
 assert(made==released);
#ifndef BASELINE
 assert(failures==342 && successes==11);
#else
 assert(failures==0 && successes==6);
#endif
 printf("PASS failures=%zu successes=%zu allocations=%zu peak=%zu live=%zu bytes=%zu\n",failures,successes,released,peak,live,bytes);return 0;
}
