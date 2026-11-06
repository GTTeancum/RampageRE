#include "test_api.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CAT_(a,b) a##b
#define CAT(a,b) CAT_(a,b)
#define FN(n) CAT(VARIANT,n)
#define movie_open FN(_movie_open)
#define movie_close FN(_movie_close)
#define movie_pick FN(_movie_pick)
#define movie_size FN(_movie_size)
#define movie_fps FN(_movie_fps)
#define movie_pictures FN(_movie_pictures)
#define movie_set_output_pump FN(_movie_set_output_pump)
#define movie_video FN(_movie_video)
#define movie_video_realtime FN(_movie_video_realtime)
#define movie_planes FN(_movie_planes)
#define movie_done FN(_movie_done)
#define movie_audio FN(_movie_audio)
#define vol_find FN(_vol_find)
#include MOVIE_SOURCE
#undef fopen

/* Only the volume lookup is replaced. Real PSS files use the actual movie_open,
 * header parsing, demux, ADPCM decode, resampling and movie_close functions. */
const VolFile *vol_find(const Vol *v, const char *name) { (void)name; return v->files; }
static void put32(uint8_t *p, uint32_t n) { for (int i=0;i<4;i++) p[i]=(uint8_t)(n>>(i*8)); }

void *FN(_new)(int ch, int inter, int rate, int mode)
{
    Movie *m = calloc(1, sizeof *m); assert(m);
    m->eof = 1;
    if (mode == 6) { m->au_head=1; m->au_ch=ch; m->au_inter=inter; m->au_rate=rate; return m; }
    uint8_t p[44] = {0xff,0xa1,0,0};
    memcpy(p+4,"SShd",4); put32(p+8, mode==5 ? 16 : 24);
    put32(p+12, mode==4 ? 0x20 : 0x10); put32(p+16,rate); put32(p+20,ch); put32(p+24,inter);
    put32(p+28, mode==1 || mode==3 ? 0 : 0xffffffffu);
    put32(p+32, mode==2 || mode==3 ? 1024 : 0xffffffffu);
    memcpy(p+(mode==5?28:36),"SSbd",4);
    audio_data(m,p,mode==5?36:44);
    return m;
}
void FN(_feed)(void *ctx, const uint8_t *data, size_t n)
{
    Movie *m=ctx;
    uint8_t p[2052]={0xff,0xa1,0,0};
    while(n) { size_t z=n>2048?2048:n; memcpy(p+4,data,z); audio_data(m,p,(int)z+4); data+=z; n-=z; }
}
int FN(_pull)(void *ctx, int16_t *out, int n)
{
    Movie *m=ctx;
    int result=movie_audio(m,out,n);
    /* This audio-only test deliberately does not retain unused video ES. Its
     * contents never enter the audio decoder; normal demux compacts next time. */
    m->es_pos=m->es_n;
    return result;
}
Snapshot FN(_state)(void *ctx)
{
    Movie *m=ctx; Snapshot s={0};
    s.pos=m->au_pos; s.n=m->au_n; s.have=m->au_have; s.at=m->au_at;
    memcpy(s.hist,m->au_hist,sizeof s.hist); s.eof=m->eof; s.failed=m->failed; s.phase=m->au_phase;
    return s;
}
int FN(_block)(void *ctx) { return audio_block(ctx); }
void FN(_close)(void *ctx)
{
    Movie *m=ctx; FILE *f=m?m->f:NULL; movie_close(m); if(f) fclose(f);
}
void *FN(_open)(const char *path)
{
    FILE *f=fopen(path,"rb"); assert(f);
    assert(!fseeko(f,0,SEEK_END)); off_t size=ftello(f); assert(size>0 && size<UINT32_MAX);
    VolFile file={"test.pss",0,(uint32_t)size,NULL}; Vol v={0}; v.f=f; v.files=&file;
#ifdef AUDIO_ONLY
    /* Sanitizer audio-core extraction excludes video init. The full-source
     * normal run above exercises the real movie_open/close lifecycle. */
    Movie *m=calloc(1,sizeof *m); assert(m);m->f=f;m->size=size;
#else
    Movie *m=movie_open(&v,"test.pss"); assert(m);
#endif
    return m;
}
void *FN(_ps)(const uint8_t *data, size_t n)
{
    Movie *m=calloc(1,sizeof *m); assert(m);
    /* fmemopen is an in-memory input, not a temporary file or copied asset. */
    m->f=fmemopen((void *)data,n,"rb"); assert(m->f); m->size=(int64_t)n;
    return m;
}
