#define AUDIT_ALLOC_IMPL
#include "alloc_probe.h"
#include <string.h>
#include <assert.h>
enum { NSLOTS = 131071 };
typedef struct { void *p; size_t n; const char *file; int line; } Slot;
static Slot slots[NSLOTS];
static size_t bytes, peak, live, calls, fail, made, freed, max_read;
static size_t cloth_calls, cloth_bytes, cloth_max;
static void guard(void *p,size_t n){memset((char*)p+n,0xA5,64);}
static void check(Slot s){for(int k=0;k<64;k++)if(((unsigned char*)s.p)[s.n+k]!=0xA5){fprintf(stderr,"ALLOCATION_CANARY_CORRUPTED at %s:%d size=%zu offset=%d\n",s.file,s.line,s.n,k);exit(86);}}
static size_t find(void *p) { size_t i=((uintptr_t)p>>4)%NSLOTS; while (slots[i].p && slots[i].p != p) i=(i+1)%NSLOTS; return i; }
static void put(void *p,size_t n,const char*f,int l) { if(!p)return; assert(live+1<NSLOTS/2); size_t i=find(p); assert(!slots[i].p); slots[i]=(Slot){p,n,f,l}; guard(p,n); if(strstr(f,"city.c")&&l==3532){cloth_calls++;cloth_bytes+=n;if(n>cloth_max)cloth_max=n;} bytes+=n; live++; made++; if(bytes>peak)peak=bytes; }
static Slot take(void*p) { size_t i=find(p); assert(slots[i].p); Slot s=slots[i]; check(s); bytes-=s.n; live--; freed++; slots[i]=(Slot){0}; size_t j=(i+1)%NSLOTS; while(slots[j].p) { Slot t=slots[j]; slots[j]=(Slot){0}; slots[find(t.p)]=t; j=(j+1)%NSLOTS; } return s; }
static int fail_line, fail_remaining, fail_hits;
void audit_fail_site(int line,int nth){fail_line=line;fail_remaining=nth;fail_hits=0;}
int audit_site_hits(void){return fail_hits;}
static int denied(const char*f,int line){calls++;if(fail_line&&line==fail_line&&strstr(f,getenv("FAIL_FILE")?getenv("FAIL_FILE"):"city.c")&&!--fail_remaining){fail_hits++;fprintf(stderr,"INJECTED_ALLOCATION_FAILURE %s:%d\n",f,line);return 1;}return fail&&calls==fail;}
void *audit_malloc(size_t n,const char*f,int l) { if(denied(f,l))return NULL; void*p=malloc(n+64); put(p,n,f,l); return p; }
void *audit_calloc(size_t n,size_t s,const char*f,int l) { if(denied(f,l))return NULL; void*p=calloc(1,n*s+64); put(p,n*s,f,l); return p; }
void *audit_realloc(void*p,size_t n,const char*f,int l) { if(denied(f,l))return NULL; if(!p){void*q=malloc(n+64);put(q,n,f,l);return q;} Slot old=take(p); void*q=realloc(p,n+64); if(q)put(q,n,f,l); else if(n)put(p,old.n,old.file,old.line); return q; }
void audit_free(void*p) { if(p){(void)take(p);free(p);} }
size_t audit_fread(void*p,size_t s,size_t n,FILE*f) { if(!s||!n)return 0; assert(n<=SIZE_MAX/s); size_t want=s*n,got=0; while(got<want){size_t z=want-got;if(z>1048576)z=1048576;if(z>max_read)max_read=z;size_t r=fread((char*)p+got,1,z,f);got+=r;if(r!=z)break;}return got/s; }
size_t audit_bytes(void){return bytes;} size_t audit_live(void){return live;} size_t audit_calls(void){return calls;} void audit_fail_at(size_t n){fail=n;}
void audit_report(const char*label){ printf("{\"label\":\"%s\",\"live_bytes\":%zu,\"live_allocations\":%zu,\"peak_bytes\":%zu,\"allocation_calls\":%zu,\"allocations\":%zu,\"frees\":%zu,\"max_read_bytes\":%zu}\n",label,bytes,live,peak,calls,made,freed,max_read);printf("{\"cloth_temp_calls\":%zu,\"cloth_temp_bytes_total\":%zu,\"cloth_temp_max_bytes\":%zu}\n",cloth_calls,cloth_bytes,cloth_max);if(live&&live<30) for(size_t i=0;i<NSLOTS;i++)if(slots[i].p)printf("  retained %zu at %s:%d\n",slots[i].n,slots[i].file,slots[i].line); }
