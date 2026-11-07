/* Includes the complete selected director.c. Only its heap calls are intercepted. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "director.h"
#include "wrand.h"

typedef struct { void *p; size_t n; } Owner;
static Owner owners[16];
static size_t attempts, fail_at, live, bytes, peak, successful, failures, recoveries;
static uint64_t trace = UINT64_C(1469598103934665603);
static void hash(const void *p,size_t n) {
    const unsigned char *b=p;
    while(n--){trace^=*b++;trace*=UINT64_C(1099511628211);}
}
static void *tracked_malloc(size_t n) {
    attempts++;
    if(attempts==fail_at)return NULL;
    void *p=malloc(n);assert(p);memset(p,0xA5,n);
    for(size_t i=0;i<16;i++)if(!owners[i].p){owners[i]=(Owner){p,n};live++;bytes+=n;if(bytes>peak)peak=bytes;return p;}
    abort();
}
static void tracked_free(void *p) {
    if(!p)return;
    for(size_t i=0;i<16;i++)if(owners[i].p==p){live--;bytes-=owners[i].n;owners[i]=(Owner){0};free(p);return;}
    abort();
}
#define malloc tracked_malloc
#define free tracked_free
#include SOURCE_FILE
#undef malloc
#undef free

FILE *plat_fopen(const char *p,const char *m){return fopen(p,m);}
int iso_is(FILE *f){(void)f;return 0;}
int iso_find(FILE*f,const char*p,int64_t*o,uint32_t*s){(void)f;(void)p;(void)o;(void)s;return 0;}

static void zero_graph(const DirGraph *a){
    DirGraph z={0}; assert(!memcmp(a,&z,sizeof z));assert(!live&&!bytes);
}
static void reset(size_t fail){assert(!live&&!bytes);attempts=0;fail_at=fail;}
static void hash_graph(const DirGraph *a){
    size_t n=(size_t)a->n;
    hash(&a->n,sizeof a->n);hash(&a->src,sizeof a->src);hash(&a->nq,sizeof a->nq);
    hash(a->action,n);hash(a->fallback,16*n);hash(a->hop,n*n);
    hash(a->cost,4*n);hash(a->pred,n);hash(a->mark,n);hash(a->queue,n);
}
static void exercise(DirGraph *a,const MoveGraph *g){
    hash_graph(a);
    for(int k=0;k<96;k++){
        search_step(a,g,k%g->n,1.0f/30.0f,1+k%4,NULL,NULL);
        hash_graph(a);
    }
    DirAI ai;dir_ai_init(&ai); MoveRunner r;
    assert(!moves_start(&r,g,0));
    float pos[3]={0};
    for(int k=0;k<96;k++){
        int switched=dir_ai_step(&ai,a,&r,pos,1.0f/30.0f,1.0f/30.0f,1,NULL,NULL);
        hash(&switched,sizeof switched);hash(&r.state,sizeof r.state);hash(&r.time,sizeof r.time);hash_graph(a);
    }
    moves_stop(&r);
}
static void healthy(const char *name,Bundle *b,const MoveGraph *g,int print){
    DirGraph a;memset(&a,0xCD,sizeof a);reset(0);
    assert(!dir_graph_read(b,&a));assert(a.n==g->n&&attempts==7&&live==7);
    assert(bytes==(size_t)a.n*a.n+(size_t)24*a.n);
    exercise(&a,g);size_t used=bytes;
    dir_graph_free(&a);zero_graph(&a);dir_graph_free(&a);zero_graph(&a);
    successful++;
    if(print)printf("HEALTHY %s states=%d graph_bytes=%zu heap_calls=7 trace=%016llx\n",name,g->n,used,(unsigned long long)trace);
}
static void fault_sweep(const char *name,Bundle *b,const MoveGraph *g){
    uint64_t saved=trace;
    for(int i=1;i<=7;i++){
        DirGraph a;memset(&a,0xCD,sizeof a);reset((size_t)i);
        int rc=dir_graph_read(b,&a);assert(rc==-1);zero_graph(&a);failures++;
        dir_graph_free(&a);zero_graph(&a);
        reset(0);assert(!dir_graph_read(b,&a));exercise(&a,g);recoveries++;
        dir_graph_free(&a);zero_graph(&a);
    }
    trace=saved;
    printf("FAULTS %s positions=7 empty=7 recoveries=7\n",name);
}
static void put32(uint8_t *p,uint32_t x){for(int i=0;i<4;i++)p[i]=(uint8_t)(x>>(8*i));}
typedef struct {uint8_t *blob; BdpEntry entry; const char *name; Bundle b; MoveState *states; MoveGraph g;} Fixture;
static Fixture make_fixture(int n){
    Fixture f={0};size_t state_at=0xB40,hop_at=state_at+(size_t)n*0x2C,node_at=hop_at+(size_t)n*n,size=node_at+(size_t)n*0x40;
    f.blob=calloc(1,size);f.states=calloc((size_t)n,sizeof *f.states);assert(f.blob&&f.states);
    put32(f.blob,11);put32(f.blob+0xA54,0xB00);put32(f.blob+0xB2C,(uint32_t)n);put32(f.blob+0xB30,(uint32_t)state_at);
    for(int k=0;k<n;k++){
        uint8_t *s=f.blob+state_at+(size_t)k*0x2C;put32(s,(uint32_t)k);put32(s+4,(uint32_t)(node_at+(size_t)k*0x40));
        memset(s+0x18,0xFF,16);put32(f.blob+node_at+(size_t)k*0x40+0x3C,(uint32_t)(k%16));
        f.states[k].next=(k+1)%n;f.states[k].length=0.25f;
    }
    memset(f.blob+hop_at,0xFF,(size_t)n*n);
    f.entry=(BdpEntry){.type=ET_RAW,.data=f.blob,.size=(uint32_t)size};f.name="Synthetic|MONSTER_DATA";
    f.g=(MoveGraph){.states=f.states,.n=n};return f;
}
static void bind_fixture(Fixture *f){f->b=(Bundle){.entries=&f->entry,.nentries=1,.names=&f->name,.nnames=1};}
static void drop_fixture(Fixture *f){free(f->blob);free(f->states);}
#include "caller.inc"

static void malformed(void){
    Fixture f=make_fixture(7);bind_fixture(&f);DirGraph a;
    for(int i=0;i<4;i++){
        uint32_t old=f.entry.size;uint8_t *p=NULL;uint32_t save=0;
        if(i==0)f.entry.size=0xAA7;
        if(i==1){p=f.blob+0xB2C;save=rd32(p);put32(p,256);}
        if(i==2){p=f.blob+0xB40+3*0x2C;save=rd32(p);put32(p,99);}
        if(i==3)f.entry.size=0xB40+7*0x2C+48;
        reset(0);memset(&a,0xCD,sizeof a);assert(dir_graph_read(&f.b,&a)==-1);zero_graph(&a);
        assert(attempts==(i<2 ? 0u : 2u));
        dir_graph_free(&a);zero_graph(&a);
        f.entry.size=old;if(p)put32(p,save);
    }
    drop_fixture(&f);puts("MALFORMED existing bounded rejection paths=4 zero_owners=4");
}
static void synthetic(int faults){
    const int counts[]={1,2,7,31,255};
    for(size_t j=0;j<sizeof counts/sizeof *counts;j++){
        Fixture f=make_fixture(counts[j]);bind_fixture(&f);char name[32];snprintf(name,sizeof name,"synthetic-%d",counts[j]);
        healthy(name,&f.b,&f.g,1);
        if(faults)for(int cycle=0;cycle<10;cycle++)fault_sweep(name,&f.b,&f.g);
        drop_fixture(&f);
    }
    malformed();
}
static void assets(int argc,char **argv,int faults){
    Vol v;assert(argc>=3&&!vol_open(&v,argv[0]));
    for(int k=1;k<argc;k++){
        uint32_t size=0;uint8_t *data;
        if(strchr(argv[k],'/')){FILE*f=fopen(argv[k],"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0&&n<32*1024*1024);size=(uint32_t)n;rewind(f);data=malloc(size);assert(data&&fread(data,1,size,f)==size);assert(!fclose(f));}
        else data=vol_read(&v,argv[k],&size);
        assert(data);Bundle b;assert(!bdp_load(&b,data,size));MoveGraph g;assert(!moves_read(&b,&g));
        healthy(argv[k],&b,&g,1);if(faults)fault_sweep(argv[k],&b,&g);
        moves_free(&g);bdp_free(&b);
    }
    assert(!fclose(v.f));free(v.files);free(v.names);
}
int main(int argc,char **argv){
    if(argc>1&&!strcmp(argv[1],"--baseline-fault")){
        assert(argc==3);Fixture f=make_fixture(7);bind_fixture(&f);DirGraph a;reset((size_t)atoi(argv[2]));
        int r=dir_graph_read(&f.b,&a);int complete=a.action&&a.fallback&&a.hop&&a.cost&&a.pred&&a.mark&&a.queue;
        printf("BASELINE fault=%s returned=%d complete=%d live=%zu bytes=%zu\n",argv[2],r,complete,live,bytes);fflush(stdout);
        if(r==0)search_step(&a,&f.g,0,1.0f/30.0f,1,NULL,NULL);
        dir_graph_free(&a);zero_graph(&a);drop_fixture(&f);return 0;
    }
    int faults=argc>1&&!strcmp(argv[1],"--candidate");synthetic(faults);
    if(argc>2)assets(argc-2,argv+2,faults);
    if(faults)caller_checks();
    printf("FINAL trace=%016llx success=%zu failed_construction=%zu same_process_recovery=%zu tracked_live=%zu tracked_bytes=%zu peak_graph_bytes=%zu\n",(unsigned long long)trace,successful,failures,recoveries,live,bytes,peak);
    assert(!live&&!bytes);return 0;
}
