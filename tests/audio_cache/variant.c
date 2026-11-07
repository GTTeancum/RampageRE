#include "cache.c"
void API(prepare)(void) {setup(4,65536,1); S.bank[0].samp_size = UINT32_MAX;}
const int16_t *API(load)(unsigned start,unsigned count,size_t *bytes,int *samples,int *rate,int *loop) {
 Bank *b=&S.bank[0];uint8_t *e=sdir+16+64;be32(e,start);be32(e+4,count);be32(e+8,count&1);
 for(int i=0;i<16;i++) {int v=i%2?-256:512;e[16+2*i]=v>>8;e[17+2*i]=v;}
 Sample *s=sample_get(b,1);assert(s);*bytes=s->bytes;*samples=s->n;*rate=s->rate;*loop=s->loop;return s->pcm;
}
void API(discard)(void) {sample_discard(&S.bank[0].smp[1]);}
void API(done)(void) {finish();}
void API(errors)(void) {
 Bank *b=&S.bank[0];size_t base=live_bytes;
 fail_at=alloc_calls+1;assert(!sample_get(b,1));assert(live_bytes==base&&!b->smp[1].tried);fail_at=0;
 for(int i=1;i<=2;i++){read_fail_at=read_calls+i;assert(!sample_get(b,1));assert(live_bytes==base&&!b->smp[1].tried);read_fail_at=0;assert(sample_get(b,1));sample_discard(&b->smp[1]);}
}
