/* Includes the production reader, replacing only its allocator calls. Synthetic
 * fixtures own their buffers; no original game assets or graphics are required. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "data.h"
typedef struct { void *p; size_t bytes; } Allocation;
static Allocation live[128];
static size_t live_bytes, live_count, total_allocs;
static void *tracked_malloc(size_t bytes)
{
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
    memset(p, 0, count * bytes);
    return p;
}
static void *tracked_realloc(void *p, size_t bytes)
{
    if (!p) return tracked_malloc(bytes);
    size_t i = 0;
    while (i < 128 && live[i].p != p) i++;
    assert(i < 128);
    void *q = tracked_malloc(bytes);
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
const BdpEntry *bdp_entry_of(const Bundle *b, uint32_t name, int type)
{
    (void)b; (void)name; (void)type; return NULL;
}
static void le32(uint8_t *p, uint32_t n)
{
    p[0]=(uint8_t)n; p[1]=(uint8_t)(n>>8); p[2]=(uint8_t)(n>>16); p[3]=(uint8_t)(n>>24);
}
static void be32(uint8_t *p, uint32_t n)
{
    p[0]=(uint8_t)(n>>24); p[1]=(uint8_t)(n>>16); p[2]=(uint8_t)(n>>8); p[3]=(uint8_t)n;
}
/* 0: good, 1: skinned section without required normals, 2: zero vertices,
 * 3: declared stream too short (backing bytes padded so cleanup is isolated
 * from the independent reader bounds problem). */
static void ps2_case(const int *kind, int count)
{
    uint8_t data[512]={0}, vertices[192]={0};
    data[0]=(uint8_t)count;
    unsigned expected=0;
    for (int i=0;i<count;i++) {
        uint8_t *rec=data+16+0x58*i, *desc=data+320+24*i;
        le32(rec+0x44,(uint32_t)(desc-rec));
        le32(desc,kind[i]==2?0:3);
        le32(desc+8,kind[i]==1?0:0x10);
        le32(desc+16,(uint32_t)(64*i));
        le32(desc+20,kind[i]==1?48:kind[i]==3?63:64);
        vertices[64*i+48+3]=vertices[64*i+52+3]=vertices[64*i+56+3]=1;
        expected += kind[i]==0;
    }
    BdpEntry e={.type=ET_SKINNED,.data=data,.size=sizeof data,.data2=vertices,.size2=sizeof vertices};
    Mesh m;
    assert(mesh_read(&e,&m)==0);
    assert(m.nparts==expected);
    for(uint32_t i=0;i<m.nparts;i++) {
        assert(m.parts[i].nverts==3 && m.parts[i].ntris==1);
        assert(m.parts[i].tris[0]==0 && m.parts[i].tris[1]==1 && m.parts[i].tris[2]==2);
    }
    mesh_free(&m);
    mesh_free(&m); /* empty destructor is idempotent */
    assert(live_bytes==0 && live_count==0);
}
static void wii_case(int valid)
{
    uint8_t data[256]={0}, vertices[48]={0};
    be32(data,1);
    uint8_t *rec=data+0x20, *block=data+0x80, *desc=data+0xA0;
    be32(rec+0x3C,(uint32_t)(block-rec)); be32(rec+0x40,(uint32_t)(desc-rec));
    be32(block+4,1); be32(block+8,36); be32(block+12,6); be32(block+16,1);
    be32(desc,3); be32(desc+4,12); be32(desc+20,36);
    vertices[36]=(uint8_t)(valid?0x90:0x88);vertices[38]=3;vertices[39]=0;vertices[40]=1;vertices[41]=2;
    BdpEntry e={.type=ET_MESH,.data=data,.size=sizeof data,.data2=vertices,.size2=sizeof vertices,.wii=1};
    Mesh m; assert(mesh_read(&e,&m)==0);assert(m.nparts==(unsigned)valid);mesh_free(&m);
    assert(live_bytes==0 && live_count==0);
}
int main(void)
{
    static const int cases[][3]={{0,0,0},{1,0,0},{0,1,0},{1,1,1},{2,0,2},{3,0,3}};
    for(int repeat=0;repeat<1000;repeat++) {
        for(size_t i=0;i<sizeof cases/sizeof cases[0];i++) ps2_case(cases[i],3);
        wii_case(0);wii_case(1);
    }
    printf("PASS: 8000 source-backed mesh lifetime cases, %zu allocations freed, zero live bytes.\n",total_allocs);
    return 0;
}
