/* Includes the production bundle reader, replacing only its allocator calls. Synthetic
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
#ifndef BUNDLE_SOURCE
#define BUNDLE_SOURCE "../../src/bundle.c"
#endif
#include BUNDLE_SOURCE
#undef malloc
#undef calloc
#undef realloc
#undef free
static void le32(uint8_t *p,uint32_t n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);p[2]=(uint8_t)(n>>16);p[3]=(uint8_t)(n>>24);}
static void be32(uint8_t *p,uint32_t n){p[0]=(uint8_t)(n>>24);p[1]=(uint8_t)(n>>16);p[2]=(uint8_t)(n>>8);p[3]=(uint8_t)n;}
static void le16(uint8_t*p,uint16_t n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void be16(uint8_t*p,uint16_t n){p[0]=(uint8_t)(n>>8);p[1]=(uint8_t)n;}
enum { FILE_BYTES=2496 };
static uint8_t *fixture(int wii,int material_first)
{
    uint8_t *f=tracked_calloc(FILE_BYTES,1);
    memcpy(f,wii?"Pipeworks Bundle File version 1.4":"Pipeworks Bundle File version 1.3",33);
    void(*u32)(uint8_t*,uint32_t)=wii?be32:le32;
    void(*u16)(uint8_t*,uint16_t)=wii?be16:le16;
    u32(f+0x30,wii?1400:1300);u32(f+0x34,0x70);u32(f+0x38,0x90);
    le32(f+0x70,2);le32(f+0x74,12);le32(f+0x78,16);memcpy(f+0x7C,"Mat",4);memcpy(f+0x80,"Book",5);
    uint8_t *seg=f+0x90;u16(seg+2,(uint16_t)(1+material_first));u32(seg+4,0xE0);u32(seg+8,0x180);
    if(material_first) {
        f[0xE0+2]=ET_MATERIAL;u32(seg+0x18+6,0x40);
        uint8_t*d=f+0x180;d[2]=2;u32(d+8,0x3F800000);u32(d+0x18,0x3F800000);
    }
    uint8_t *rec=seg+0x18+0x12*material_first,*ent=f+0xE0+16*material_first;
    ent[2]=ET_BOOK;u32(ent+4,1);u32(rec+2,0x40);u32(rec+6,2048);
    uint8_t*d=f+0x1C0;u32(d+0x1C,1);u32(d+0x24,0x30);u32(d+0x30,0x40);u32(d+0x40+8,66);
    for(int i=0;i<66;i++){uint8_t*r=d+0x40+20+12*i;r[0]=PK_GROUP;u32(r+8,0x400-0x40);}
    u32(d+0x400+8,2);for(int i=0;i<2;i++){uint8_t*r=d+0x400+20+12*i;r[0]=PK_GROUP;u32(r+8,0x500-0x400);}
    return f;
}
static uint64_t hash_bytes(const uint8_t *p,size_t n)
{
    uint64_t h=UINT64_C(14695981039346656037);
    while(n--)h=(h^*p++)*UINT64_C(1099511628211);
    return h;
}
static uint64_t run_case(int wii,int material_first,int failure)
{
    uint8_t *f=fixture(wii,material_first);Bundle b;size_t before=allocation_calls;
    fail_at=failure?before+(size_t)failure:0;
    int r=bdp_load(&b,f,FILE_BYTES);fail_at=0;uint64_t h=0;
    if(failure) {
        assert(r==-1);Bundle zero={0};assert(!memcmp(&b,&zero,sizeof b));
        assert(live_count==1 && live_bytes==FILE_BYTES); /* input still owned by caller */
        if(wii && material_first && failure>6) assert(rd32(f+0x180+8)==0x3F800000); /* partial conversion */
        tracked_free(f);
    } else {
        assert(r==0);assert(b.file==f);assert(b.nentries==(unsigned)(1+material_first));
        assert(!strcmp(bdp_name(&b,1),"Book"));assert(bdp_entry_of(&b,1,ET_BOOK));
        assert(rd32(b.entries[material_first].data+0x1C)==1);
        assert(allocation_calls-before==(size_t)(wii?9+material_first:5));
        h=hash_bytes(f,FILE_BYTES);
    }
    bdp_free(&b);bdp_free(&b);assert(!live_count&&!live_bytes);return h;
}
int main(int argc,char **argv)
{
    int controls=argc>1&&!strcmp(argv[1],"--control");
    for(int wii=0;wii<=1;wii++)for(int first=0;first<=1;first++) {
        static const uint64_t expected[2][2] = {
            { UINT64_C(0x4e0d3e1622df0af7), UINT64_C(0x69f0c08657818de0) },
            { UINT64_C(0xdd07d01d48c38a38), UINT64_C(0xd2b06beb75cfb18d) }
        }; /* complete converted-buffer hashes from the pre-fix production reader */
        uint64_t h=run_case(wii,first,0);
        assert(h == expected[wii][first]);
        printf("success wii=%d material_first=%d hash=%016llx\n",wii,first,(unsigned long long)h);
        if(!controls)for(int repeat=0;repeat<100;repeat++)for(int i=1;i<=(wii?9+first:5);i++)run_case(wii,first,i);
    }
    if(!controls)for(int kind=0;kind<3;kind++) {
        uint8_t *f=fixture(0,0);Bundle b;
        if(kind==1)f[0]=0;else if(kind==2)f[0x30]=0;
        assert(bdp_load(&b,f,kind?FILE_BYTES:32)==-1);
        Bundle zero={0};assert(!memcmp(&b,&zero,sizeof b));assert(live_count==1);
        tracked_free(f);bdp_free(&b);assert(!live_count);
    }
    printf("PASS: %zu allocations freed, zero live bytes; %s.\n",total_allocs,controls?"successful output controls":"2900 injected allocation failures and successful/invalid-header cases");
    return 0;
}
