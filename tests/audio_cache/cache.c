#define RAMPAGE_XBOX 1
#include "tracker.h"
#include SOUND_SOURCE
static size_t read_calls, read_fail_at;
static uint8_t sdir[16+201*64];
static Vol volume;
static void le32(uint8_t *p,uint32_t n) {for(int i=0;i<4;i++)p[i]=n>>(8*i);}
static void be32(uint8_t *p,uint32_t n) {for(int i=0;i<4;i++)p[i]=n>>(24-8*i);}
int vol_read_range(const Vol *v,const VolFile *f,uint32_t off,void *out,size_t n) {
    (void)v;(void)f;read_calls++;if(read_calls==read_fail_at)return -1;
    uint8_t *p=out;for(size_t i=0;i<n;i++)p[i]=((off+i)%16<2)?0:0x24;return 0;
}
const VolFile *vol_find(const Vol *v,const char *n) {(void)v;(void)n;return NULL;}
uint8_t *vol_read(const Vol *v,const char *n,uint32_t *z) {(void)v;(void)n;(void)z;return NULL;}
uint8_t *vol_read_wii(const Vol *v,const char *n,uint32_t *z) {(void)v;(void)n;(void)z;return NULL;}
void SDL_PauseAudioDevice(SDL_AudioDeviceID d,int p){(void)d;(void)p;}
unsigned SDL_GetQueuedAudioSize(SDL_AudioDeviceID d){(void)d;return 0;}
int SDL_QueueAudio(SDL_AudioDeviceID d,const void *p,unsigned n){(void)d;(void)p;(void)n;return -1;}
void SDL_ClearQueuedAudio(SDL_AudioDeviceID d){(void)d;}
const char *SDL_GetError(void){return "stub";}
unsigned rampage_audio_pending_frames(void){return 0;}
static Bank *setup(int count,int compressed,int wii) {
    assert(!live_count);memset(&S,0,sizeof S);memset(sdir,0,sizeof sdir);
    S.nbanks=1;S.vol=&volume;S.sfx_gain=1;S.music_gain=1;S.ready=1;
    for(int i=0;i<MAX_INST;i++){S.inst[i].handle=-1;S.inst[i].gain=1;}
    Bank *b=&S.bank[0];b->smp=calloc(count,sizeof *b->smp);assert(b->smp);b->nsdir=count;b->sdir=sdir;b->samp_size=compressed;b->vol=&volume;b->wii=wii;
    for(int i=1;i<count;i++){uint8_t *e=sdir+16+i*(wii?64:16);if(wii){be32(e,0);be32(e+4,2*compressed);be32(e+12,48000);}else{le32(e,0);le32(e+4,compressed);le32(e+12,48000);}}
    return b;
}
static void finish(void){sound_free();assert(!live_count&&!live_bytes&&!S.sample_bytes);}
int main(void) {
    Bank *b=setup(201,4096,0);size_t base=live_bytes;
    for(int i=0;i<600;i++){Sample *s=sample_get(b,1+i%200);assert(s&&s->n==7168&&s->bytes==14336);assert(S.sample_bytes<=RAMPAGE_AUDIO_CACHE_BYTES);assert(live_bytes==base+S.sample_bytes);}
    size_t calls=alloc_calls,reads=read_calls;assert(sample_get(b,200));assert(alloc_calls==calls&&read_calls==reads);
    printf("cache_churn requests=600 pcm_live=%zu allocations_live=%zu peak=%zu\n",S.sample_bytes,live_count,peak_bytes);finish();
    b=setup(41,32768,0);
    for(int i=0;i<MAX_INST;i++){Sample *s=sample_get(b,i+1);assert(s);S.voice[i].on=1;S.voice[i].s=s;S.inst[i].paused=1;}
    size_t pinned=S.sample_bytes;assert(pinned==40u*114688u);for(int i=0;i<MAX_INST;i++)assert(S.voice[i].s->pcm);
    sample_make_room(0);assert(S.sample_bytes==pinned);for(int i=0;i<MAX_INST;i++)S.voice[i].on=0;
    sound_update(0);assert(S.sample_bytes<=RAMPAGE_AUDIO_CACHE_BYTES);
    printf("pinned_burst bytes=%zu after_idle_update=%zu\n",pinned,S.sample_bytes);finish();
    b=setup(4,8192,0);base=live_bytes;fail_at=alloc_calls+1;assert(!sample_get(b,1));assert(!b->smp[1].tried&&!b->smp[1].pcm&&live_bytes==base);fail_at=0;assert(sample_get(b,1));sample_discard(&b->smp[1]);assert(live_bytes==base);
    for(int i=1;i<=2;i++){read_fail_at=read_calls+i;assert(!sample_get(b,1));assert(!b->smp[1].tried&&!b->smp[1].pcm&&live_bytes==base);read_fail_at=0;assert(sample_get(b,1));sample_discard(&b->smp[1]);assert(live_bytes==base);}finish();
    b=setup(4,8192,1);base=live_bytes;read_fail_at=read_calls+2;assert(!sample_get(b,1));assert(live_bytes==base&&!b->smp[1].pcm);read_fail_at=0;Sample *ws=sample_get(b,1);assert(ws);assert(ws->n==14336&&ws->bytes==28672);
    printf("wii aligned_nibbles=16384 pcm_samples=%d allocated=%zu needed=%zu wasted=%zu read_calls_for_success=2\n",ws->n,ws->bytes,2u*(size_t)ws->n,ws->bytes-2u*(size_t)ws->n);finish();
    b=setup(4,4096,0);Sample *s=sample_get(b,1);s->loop=0;S.voice[0]=(Voice){.on=1,.s=s,.step=1,.l=10000,.r=10000};calls=alloc_calls;reads=read_calls;int16_t output[2048];for(int i=0;i<100;i++)sound_mix(output,1024);assert(alloc_calls==calls&&read_calls==reads);finish();
    for(int i=0;i<100;i++){b=setup(4,4096,0);assert(sample_get(b,1));finish();}
    printf("PASS sound: no tracked outstanding allocations after churn, pinned release, allocation/read failures, 100 teardown cycles; 100 warmed mixer pulls allocate/read zero\n");
    return 0;
}
