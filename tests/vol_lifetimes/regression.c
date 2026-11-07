/* Includes the production volume reader, replacing only its allocator calls. Synthetic
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
    void *p = malloc(bytes + 64);
    assert(p);
    memset((unsigned char *)p + bytes, 0xA5, 64);
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
    for (int k=0;k<64;k++) assert(((unsigned char *)p)[live[i].bytes+k]==0xA5);
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
static unsigned char archive[128];
static size_t archive_size;
static int open_fail, image_mode, handles, seek_fail;
FILE *plat_fopen(const char *path,const char *mode)
{
    (void)path;(void)mode;
    if(open_fail)return NULL;
    FILE*f=fmemopen(archive,archive_size,"rb");assert(f);handles++;return f;
}
static int tracked_fseeko(FILE*f,off_t at,int whence){return seek_fail?-1:fseeko(f,at,whence);}
static int tracked_fclose(FILE*f){assert(f&&handles>0);handles--;return fclose(f);}
int iso_is(FILE*f){(void)f;return image_mode!=0;}
int iso_find(FILE*f,const char*p,int64_t*at,uint32_t*size)
{
    (void)f;(void)p;*at=16;*size=image_mode==2?8:64;return image_mode!=1;
}
int plat_exists(const char*p){(void)p;return 0;}
int zip_each(FILE*f,const char*p,void(*fn)(void*,const char*,int64_t,uint32_t),void*ctx)
{(void)f;(void)p;(void)fn;(void)ctx;return 0;}
#define malloc tracked_malloc
#define calloc tracked_calloc
#define realloc tracked_realloc
#define free tracked_free
#define fclose tracked_fclose
#define fseeko tracked_fseeko
#ifndef VOL_SOURCE
#define VOL_SOURCE "../../src/vol.c"
#endif
#include VOL_SOURCE
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef fclose
#undef fseeko
static void le32(unsigned char*p,uint32_t n){p[0]=(unsigned char)n;p[1]=(unsigned char)(n>>8);p[2]=(unsigned char)(n>>16);p[3]=(unsigned char)(n>>24);}
static void fixture(int embedded)
{
    memset(archive,0,sizeof archive);unsigned char*p=archive+(embedded?16:0);
    memcpy(p,"PVOL",4);le32(p+4,0x1001);le32(p+8,1);le32(p+12,32);
    le32(p+16,64);le32(p+20,3);le32(p+28,1);le32(p+32,8);memcpy(p+36,"same.bdp",9);
    archive_size=embedded?80:64;image_mode=embedded?3:0;open_fail=0;
}
static void cleanup(Vol*v)
{
    if(v->f)tracked_fclose(v->f);
    tracked_free(v->files);tracked_free(v->names);
    for(uint32_t i=0;i<v->nwii;i++)tracked_free((void*)v->wii[i].path);
    tracked_free(v->wii);memset(v,0,sizeof*v);
}
static void open_case(int embedded,int failure,int short_at,int img,int missing,int expected)
{
    fixture(embedded);if(short_at>=0)archive_size=(size_t)short_at;if(img>=0){image_mode=img;archive[0]='X';}
    open_fail=missing;fail_at=failure?allocation_calls+(size_t)failure:0;Vol v;
    int r=vol_open(&v,"synthetic.vol");fail_at=0;assert(r==expected);
    if(r){Vol empty={0};assert(!memcmp(&v,&empty,sizeof v));assert(!handles&&!live_count&&!live_bytes);}
    else {assert(v.count==1&&v.base==(embedded?16:0));assert(v.files[0].offset==64&&v.files[0].size==3);assert(!strcmp(v.files[0].name,"same.bdp"));assert(handles==1&&live_count==2);}
    cleanup(&v);assert(!handles&&!live_count&&!live_bytes);
}
static void growth_case(int existing,int failure)
{
    Vol v={0};uint32_t cap=0;
    if(existing){cap=v.nwii=256;v.wii=tracked_calloc(cap,sizeof*v.wii);for(uint32_t i=0;i<cap;i++)v.wii[i].name="old.cmg";}
    VolFile*old=v.wii;uint32_t oldcap=cap,oldn=v.nwii;
    fail_at=failure?allocation_calls+(size_t)failure:0;add_file(&v,&cap,"Game","same.bdg",3,0,'/');fail_at=0;
    if(failure==1){assert(v.wii==old&&cap==oldcap&&v.nwii==oldn);}
    else if(failure==2){assert(v.wii&&cap==(existing?512u:256u)&&v.nwii==oldn);}
    else assert(v.nwii==oldn+1);
    add_file(&v,&cap,"Game","same.bdg",3,0,'/');assert(v.nwii==oldn+(failure?1u:2u));
    const VolFile ps2={"same.bdp",17,2,NULL};v.files=(VolFile*)&ps2;v.count=1;
    assert(vol_find(&v,"same.bdp")==&ps2);v.files=NULL;v.count=0;
    assert(vol_find(&v,"same.bdp")==&v.wii[oldn]); /* Wii fallback only when PS2 is absent */
    cleanup(&v);assert(!live_count&&!live_bytes);
}
int main(int argc,char**argv)
{
    int control=argc>1&&!strcmp(argv[1],"--control");
    open_case(0,0,-1,-1,0,0);open_case(1,0,-1,-1,0,0);
    growth_case(0,0);growth_case(1,0);
    if(!control){
        fixture(1);seek_fail=1;Vol v;assert(vol_open(&v,"synthetic.vol")==VOL_NOT_IN_IMAGE);seek_fail=0;
        Vol empty={0};assert(!memcmp(&v,&empty,sizeof v));assert(!handles&&!live_count);
        Vol huge={0};uint32_t cap=UINT32_MAX;huge.nwii=cap;size_t calls=allocation_calls;
        add_file(&huge,&cap,"Game","skip.cmg",1,0,'/');assert(!huge.wii&&cap==UINT32_MAX&&allocation_calls==calls);
    }
    if(!control)for(int repeat=0;repeat<100;repeat++) {
        for(int embedded=0;embedded<2;embedded++)for(int f=1;f<=3;f++)open_case(embedded,f,-1,-1,0,VOL_UNREADABLE);
        open_case(0,0,-1,-1,1,VOL_UNREADABLE);open_case(0,0,8,-1,0,VOL_UNREADABLE);
        open_case(0,0,20,-1,0,VOL_UNREADABLE);open_case(0,0,40,-1,0,VOL_UNREADABLE);
        open_case(0,0,-1,0,0,VOL_UNREADABLE);open_case(0,0,-1,1,0,VOL_NOT_IN_IMAGE);
        open_case(0,0,-1,2,0,VOL_NOT_IN_IMAGE);open_case(0,0,20,3,0,VOL_NOT_IN_IMAGE);
        open_case(0,0,-1,3,0,VOL_NOT_IN_IMAGE);
        for(int existing=0;existing<2;existing++)for(int f=1;f<=2;f++)growth_case(existing,f);
    }
    printf("PASS: %s, %zu allocations freed, no live allocations or FILE handles.\n",control?"success/archive/overlay controls":"1900 repeated failure cases, seek/capacity edge cases and controls",total_allocs);return 0;
}
