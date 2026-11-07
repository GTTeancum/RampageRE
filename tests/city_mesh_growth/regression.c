/* Current source functions, bounded synthetic ownership fixtures. */
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "city.h"
#include "city-status.inc"
static void *tracked_realloc(void *, size_t);
static void tracked_free(void *);
#define realloc tracked_realloc
#define free tracked_free
#include "subject.inc"
#undef realloc
#undef free

static struct { void *p; size_t n; } blocks[2048];
static size_t live, peak;
static unsigned calls, moves, acquired, released, checks, rejected, latched;
static int fail_next;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); exit(2); } } while (0)
static unsigned slot(void *p) {
    for (unsigned i=0;i<2048;i++) if (blocks[i].p==p) return i;
    CHECK(0); return 0;
}
static void *allocate(size_t n) {
    unsigned i=slot(NULL); unsigned char *p=calloc(1,n+32); CHECK(p);
    memset(p+n,0xa5,32); blocks[i].p=p; blocks[i].n=n;
    live+=n; if(live>peak) peak=live; acquired++; return p;
}
static void canary(unsigned i) {
    unsigned char *p=blocks[i].p; for(unsigned j=0;j<32;j++) CHECK(p[blocks[i].n+j]==0xa5);
}
static void tracked_free(void *p) {
    if(!p) return;
    unsigned i=slot(p); canary(i); live-=blocks[i].n;
    free(p); blocks[i].p=NULL; blocks[i].n=0; released++;
}
static void *tracked_realloc(void *old,size_t n) {
    calls++;
    if(fail_next) { fail_next=0; rejected++; return NULL; }
    size_t previous=old?blocks[slot(old)].n:0;
    void *p=allocate(n);
    if(old) { CHECK(p!=old && n>=previous); memcpy(p,old,previous); tracked_free(old); moves++; }
    return p;
}
static Scene scene;
static Bundle bundle;
static Mesh incoming;
static MeshPart *expected_parts[513];
static float *expected_pos[513];
static Mesh make_mesh(unsigned seed) {
    Mesh m={0}; m.nparts=1; m.parts=allocate(sizeof *m.parts);
    m.parts[0].nverts=1; m.parts[0].pos=allocate(3*sizeof(float));
    for(unsigned j=0;j<3;j++) m.parts[0].pos[j]=(float)(seed*3+j);
    return m;
}
static void check_mesh(unsigned i) {
    CHECK(scene.meshes[i].b==&bundle && scene.meshes[i].mesh.nparts==1);
    CHECK(scene.meshes[i].mesh.parts==expected_parts[i] && scene.meshes[i].mesh.parts[0].pos==expected_pos[i]);
    for(unsigned j=0;j<3;j++) CHECK(scene.meshes[i].mesh.parts[0].pos[j]==(float)(i*3+j));
}
static void clean(void) {
    for(unsigned i=0;i<scene.nmeshes;i++) {
        mesh_free(&scene.meshes[i].mesh);
        Mesh zero={0}; CHECK(!memcmp(&scene.meshes[i].mesh,&zero,sizeof zero));
    }
    tracked_free(scene.meshes); memset(&scene,0,sizeof scene);
    CHECK(live==0 && acquired==released);
}
static void success(void) {
    for(unsigned i=0;i<513;i++) {
        Mesh m=make_mesh(i); MeshPart *parts=m.parts; float *pos=parts[0].pos;
        expected_parts[i]=parts; expected_pos[i]=pos;
        CHECK(scene_add_mesh(&scene,m,&bundle)==(int)i);
        CHECK(scene.nmeshes==i+1 && scene.meshes_cap==(i<256?256u:i<512?512u:1024u));
        CHECK(scene.meshes[i].mesh.parts==parts && scene.meshes[i].mesh.parts[0].pos==pos);
        for(unsigned k=0;k<=i;k++) check_mesh(k);
    }
    CHECK(calls==3 && moves==2); clean();
}
static void failure(unsigned populated) {
    for(unsigned i=0;i<populated;i++) CHECK(scene_add_mesh(&scene,make_mesh(i),&bundle)==(int)i);
    incoming=make_mesh(populated);
    Scene saved=scene; size_t bytes=populated*sizeof *scene.meshes;
    void *copy=malloc(bytes?bytes:1); CHECK(copy); if(bytes) memcpy(copy,scene.meshes,bytes);
    unsigned before=calls; size_t owners=live;
    fail_next=1;
    CHECK(scene_add_mesh(&scene,incoming,&bundle)==-2);
    saved.construction_failed=1; latched++;
    CHECK(calls==before+1 && live==owners && !memcmp(&scene,&saved,sizeof scene));
    CHECK(scene_add_mesh(&scene,incoming,&bundle)==-2);
    CHECK(calls==before+1 && live==owners && !memcmp(&scene,&saved,sizeof scene));
    if(bytes) CHECK(!memcmp(copy,scene.meshes,bytes));
    free(copy);
    /* Ownership never transferred on required failure. The Scene is latched,
     * not playable: caller-owned input and published old meshes are torn down. */
    CHECK(incoming.nparts==1 && incoming.parts[0].pos[0]==(float)(populated*3));
    mesh_free(&incoming); clean();
}
static uint32_t capacity;
static void overflow(uint32_t cap,uint32_t step,size_t size) {
    capacity=cap; unsigned before=calls;
    CHECK(scene_vector_grow(NULL,&capacity,step,size,"overflow fixture")==NULL);
    CHECK(capacity==cap && calls==before && live==0);
}
static void append_overflow(void) {
    scene.nmeshes=scene.meshes_cap=(uint32_t)INT_MAX;
    Scene saved=scene; unsigned before=calls;
    CHECK(scene_add_mesh(&scene,(Mesh){0},&bundle)==-2);
    saved.construction_failed=1; latched++;
    CHECK(!memcmp(&saved,&scene,sizeof scene) && calls==before);
    memset(&scene,0,sizeof scene);
}
int main(void) {
    assert_city_status_contract();
    /* Real append rejects an owner's failure before it allocates or publishes. */
    { City c={0}; Scene s={0}; Mesh m={0}; s.owner=&c; c.load_failed=1;
      unsigned before=calls; assert(scene_add_mesh(&s,m,NULL)==-2);
      assert(s.construction_failed && !s.meshes && !s.nmeshes && calls==before); }

    success(); failure(0); failure(256);
    overflow(INT_MAX,1,1); overflow(0,UINT32_MAX,1); overflow(0,0,1);
    overflow(1,1,SIZE_MAX); append_overflow();
    CHECK(rejected==2 && latched==3 && acquired==released && !live);
    printf("PASS checks=%u acquired=%u released=%u peak_requested=%zu failed_allocations=%u latched_append_failures=%u\n",checks,acquired,released,peak,rejected,latched);
    return 0;
}
