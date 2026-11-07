/* Includes the production mesh reader, replacing only its allocator calls. Synthetic
 * fixtures own their buffers; no original game assets or graphics are required. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "data.h"
typedef struct { void *p; size_t bytes; } Allocation;
static Allocation live[128];
static size_t live_bytes, live_count, total_allocs, allocation_calls, fail_at;
static void *tracked_malloc(size_t bytes)
{
    allocation_calls++;
    if (fail_at && allocation_calls == fail_at) return NULL;
    void *p = malloc(bytes ? bytes : 1);
    assert(p);
    size_t i = 0;
    while (i < 128 && live[i].p) i++;
    assert(i < 128);
    live[i] = (Allocation){ p, bytes };
    live_bytes += bytes;
    live_count++;
    total_allocs++;
    return p;
}
static void tracked_free(void *p)
{
    if (!p) return;
    size_t i = 0;
    while (i < 128 && live[i].p != p) i++;
    assert(i < 128); /* also rejects double frees */
    live_bytes -= live[i].bytes;
    live_count--;
    live[i] = (Allocation){ 0 };
    free(p);
}
static void *tracked_calloc(size_t count, size_t bytes)
{
    assert(!bytes || count <= SIZE_MAX / bytes);
    void *p = tracked_malloc(count * bytes);
    if (p) memset(p, 0, count * bytes);
    return p;
}
static void *tracked_realloc(void *p, size_t bytes)
{
    if (!p) return tracked_malloc(bytes);
    size_t i = 0;
    while (i < 128 && live[i].p != p) i++;
    assert(i < 128);
    void *q = tracked_malloc(bytes);
    if (!q) return NULL;
    memcpy(q, p, live[i].bytes < bytes ? live[i].bytes : bytes);
    tracked_free(p);
    return q;
}
#define malloc tracked_malloc
#define calloc tracked_calloc
#define realloc tracked_realloc
#define free tracked_free
#ifndef MESH_SOURCE
#define MESH_SOURCE "../../src/mesh.c"
#endif
#include MESH_SOURCE
#undef malloc
#undef calloc
#undef realloc
#undef free
const BdpEntry *bdp_entry_of(const Bundle *b, uint32_t name, int type){(void)b;(void)name;(void)type;return NULL;}
static void le32(uint8_t*p,uint32_t n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);p[2]=(uint8_t)(n>>16);p[3]=(uint8_t)(n>>24);}
static void be32(uint8_t*p,uint32_t n){p[0]=(uint8_t)(n>>24);p[1]=(uint8_t)(n>>16);p[2]=(uint8_t)(n>>8);p[3]=(uint8_t)n;}
static uint64_t hash(uint64_t h,const void *p,size_t n){const unsigned char*b=p;while(n--)h=(h^*b++)*UINT64_C(1099511628211);return h;}
static uint64_t run_case(int wii,int skinned,int four,int count,int failure,size_t *calls)
{
    uint8_t data[1024]={0},vertices[1024]={0};
    void(*u32)(uint8_t*,uint32_t)=wii?be32:le32;
    if(skinned){if(wii)data[1]=(uint8_t)count;else data[0]=(uint8_t)count;}else u32(data,(uint32_t)count);
    for(int i=0;i<count;i++) {
        uint8_t *rec=data+(skinned?0x10:0x20)+(skinned?0x58:0x50)*i,*desc=data+512+64*i;
        u32(rec+(skinned?0x44:0x40),(uint32_t)(desc-rec));
        u32(desc,3);u32(desc+8,(uint32_t)(0x150|(four?4:0)));u32(desc+16,(uint32_t)(384*i));
        if(!wii){u32(desc+20,96);vertices[384*i+51]=vertices[384*i+55]=vertices[384*i+59]=1;}
        else {
            unsigned stride=36+(skinned?(four?20:8):0);u32(desc+4,stride);u32(desc+20,3*stride);
            uint8_t*block=desc+24;u32(rec+(skinned?0x40:0x3C),(uint32_t)(block-rec));
            u32(block+4,1);u32(block+8,(uint32_t)(384*i+200));u32(block+12,69);u32(block+16,1);
            vertices[384*i+200]=0x90;vertices[384*i+202]=3;vertices[384*i+203]=0;vertices[384*i+204]=1;vertices[384*i+205]=2;
            vertices[384*i+206]=0x90;vertices[384*i+208]=60;for(int v=0;v<60;v++)vertices[384*i+209+v]=(uint8_t)(v%3);
        }
    }
    BdpEntry e={.type=skinned?ET_SKINNED:ET_MESH,.data=data,.size=sizeof data,.data2=vertices,.size2=sizeof vertices,.wii=(uint8_t)wii};
    size_t at=allocation_calls;fail_at=failure?at+(size_t)failure:0;Mesh m;int r=mesh_read(&e,&m);fail_at=0;
    *calls=allocation_calls-at;uint64_t h=UINT64_C(14695981039346656037);
    if(failure){assert(r==-1);Mesh empty={0};assert(!memcmp(&m,&empty,sizeof m));assert(!live_count&&!live_bytes);}
    else {
        assert(r==0 && m.nparts==(unsigned)count);
        for(uint32_t j=0;j<m.nparts;j++){const MeshPart*p=&m.parts[j];assert(p->pos&&p->nrm&&p->col&&p->uv&&p->tris);assert(!skinned||(p->bones&&p->weights));h=hash(h,p->pos,12*p->nverts);h=hash(h,p->nrm,12*p->nverts);h=hash(h,p->col,16*p->nverts);h=hash(h,p->uv,8*p->nverts);h=hash(h,p->tris,12*p->ntris);if(skinned){h=hash(h,p->bones,4*p->nverts);h=hash(h,p->weights,16*p->nverts);}}
    }
    mesh_free(&m);mesh_free(&m);assert(!live_count&&!live_bytes);return h;
}
int main(int argc,char**argv)
{
    int control=argc>1&&!strcmp(argv[1],"--control");size_t failures=0;
    for(int wii=0;wii<=1;wii++)for(int skinned=0;skinned<=1;skinned++)for(int four=0;four<=(wii&&skinned);four++)for(int count=1;count<=2;count++) {
        static const uint64_t expected[5][2]={
            {UINT64_C(0xec32669a74fcae65),UINT64_C(0x66e368127e9e89a5)},
            {UINT64_C(0xba72b55e32ef5277),UINT64_C(0x3bfc0f8cbdfcc869)},
            {UINT64_C(0x45f400127e177c76),UINT64_C(0xdd01423b810f47b5)},
            {UINT64_C(0x54f6842d790b75d7),UINT64_C(0xf33c73b5859b3769)},
            {UINT64_C(0x945b7fb9e55149b7),UINT64_C(0xefba62ecdf6b2969)}
        }; /* pre-fix production reader output */
        size_t calls;uint64_t h=run_case(wii,skinned,four,count,0,&calls);
        assert(h==expected[wii?2+skinned+four:skinned][count-1]);
        printf("success wii=%d skinned=%d four=%d count=%d calls=%zu hash=%016llx\n",wii,skinned,four,count,calls,(unsigned long long)h);
        if(!control)for(int repeat=0;repeat<100;repeat++)for(size_t f=1;f<=calls;f++){size_t ignored;run_case(wii,skinned,four,count,(int)f,&ignored);failures++;}
    }
    printf("PASS: %zu injected failures, %zu allocations freed, zero live bytes.\n",failures,total_allocs);return 0;
}
