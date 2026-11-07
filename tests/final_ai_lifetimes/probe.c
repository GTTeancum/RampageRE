/* Real current director.c and moves.c are separately compiled with their heap
 * calls intercepted. Main's graph loops and exit fragments are extracted, never
 * transcribed. Non-graph subsystems are explicit shutdown-order spies. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "director.h"
#include "tracked.h"

#define MONSTERS 4
#ifndef CANDIDATE
#error Define CANDIDATE to 0 or 1
#endif

typedef struct {void *p; size_t n; int family;} Owner;
static Owner ledger[256];
static size_t live[2], bytes[2], peak[2], attempts, fail_mask;
static size_t cases, faults, partials, repeated, callback_calls, graph_stops, graph_frees, ai_frees;
static int phase = -1;
static void *allocate(size_t n, int family) {
    if (!family && ((fail_mask >> attempts++) & 1)) return NULL;
    void *p = malloc(n); assert(p); memset(p, 0, n);
    for (size_t k = 0; k < sizeof ledger / sizeof *ledger; k++)
        if (!ledger[k].p) {
            ledger[k] = (Owner){p,n,family}; live[family]++; bytes[family] += n;
            if (bytes[family] > peak[family]) peak[family] = bytes[family];
            return p;
        }
    abort();
}
static void release(void *p, int family) {
    if (!p) return;
    for (size_t k = 0; k < sizeof ledger / sizeof *ledger; k++)
        if (ledger[k].p == p) {
            assert(ledger[k].family == family); live[family]--; bytes[family] -= ledger[k].n;
            ledger[k] = (Owner){0}; free(p); return;
        }
    abort(); /* foreign owner or a double free */
}
void *dir_malloc(size_t n) {return allocate(n,0);}
void dir_free(void *p) {release(p,0);}
void *move_malloc(size_t n) {return allocate(n,1);}
void *move_calloc(size_t n,size_t z) {assert(!z || n<=SIZE_MAX/z); return allocate(n*z,1);}
void move_free(void *p) {release(p,1);}
static void clean(void) {assert(!live[0] && !live[1] && !bytes[0] && !bytes[1]);}
static void zero_ai(const DirGraph *a) {DirGraph z={0}; assert(!memcmp(a,&z,sizeof z));}
static void zero_move(const MoveGraph *g) {MoveGraph z={0}; assert(!memcmp(g,&z,sizeof z));}
static void zero_run(const MoveRunner *r) {MoveRunner z={0}; assert(!memcmp(r,&z,sizeof z));}

static void put32(uint8_t *p,uint32_t n) {for(int k=0;k<4;k++)p[k]=(uint8_t)(n>>(8*k));}
typedef struct {uint8_t *blob; BdpEntry entry; const char *name; Bundle b; int n;} Fixture;
static void fixture(Fixture *f,int n) {
    memset(f,0,sizeof *f); f->n=n;
    size_t state=0xB40,hop=state+(size_t)n*0x2C,node=hop+(size_t)n*n,size=node+(size_t)n*0x40;
    f->blob=calloc(1,size); assert(f->blob);
    put32(f->blob,11); put32(f->blob+0xA54,0xB00); put32(f->blob+0xB2C,(uint32_t)n); put32(f->blob+0xB30,(uint32_t)state);
    for(int k=0;k<n;k++) {
        uint8_t *s=f->blob+state+(size_t)k*0x2C; put32(s,(uint32_t)k); put32(s+4,(uint32_t)(node+(size_t)k*0x40));
        memset(s+0x18,0xFF,16); put32(f->blob+node+(size_t)k*0x40+0x3C,DA_IDLE);
    }
    memset(f->blob+hop,0xFF,(size_t)n*n);
    f->entry=(BdpEntry){.type=ET_RAW,.data=f->blob,.size=(uint32_t)size}; f->name="FinalCleanup|MONSTER_DATA";
    f->b=(Bundle){.entries=&f->entry,.nentries=1,.names=&f->name,.nnames=1};
}

typedef struct Viewer Viewer;
typedef struct {
    Viewer *v; int slot,have_graph,have_ag;
    MoveGraph graph; MoveRunner run; DirGraph ag; DirAI ai;
    const MoveNode *face; void *water_in,*water_was;
    int fx_used,nfx_named,rage_on,rage_fx;
} Mon;
typedef struct {struct {Bundle *bundle;} mon[MONSTERS];} Scene;
typedef struct {int alive;} Window;
struct Viewer {Mon mon[MONSTERS]; Scene scene; int have_scene; void *rnd,*city,*ap;};
static Viewer *active;
static int scene_was_live, ap_return, ap_calls, cap_closes, post_fail_reached;
static int initial_exit_status;
static int mon_in(const Mon *m) {(void)m;return 1;}

static int callback(void *ctx,const char *name,const float *args) {
    Mon *m=ctx; (void)name; (void)args; assert(phase<0 && m->have_graph && m->have_ag);
    assert(m->ag.action && m->graph.states && m->run.g==&m->graph);callback_calls++;return 1;
}
static void make_moves(Mon *m,int n) {
    MoveGraph *g=&m->graph;g->n=n;
    g->states=move_calloc((size_t)n,sizeof *g->states);g->all=move_calloc((size_t)n,sizeof *g->all);
    g->all_events=move_calloc(1,sizeof *g->all_events);g->slot=move_calloc(1,sizeof *g->slot);
    g->shapes=move_calloc(1,sizeof *g->shapes);g->nodes=move_calloc(1,sizeof *g->nodes);
    g->attach=move_calloc(1,sizeof *g->attach);g->fxnode=move_calloc(1,sizeof *g->fxnode);
    for(int k=0;k<n;k++) {
        g->states[k]=(MoveState){.length=1,.next=(k+1)%n,.trans=&g->all[k],.ntrans=1};
        g->all[k]=(MoveTrans){.target=(k+1)%n,.flags=0x10,.in1=1,.sw1=1,.call="lifetime_check"};
    }
    assert(!moves_start(&m->run,g,0));m->have_graph=1;
}
static void setup(Viewer *v,Fixture *f,unsigned flags) {
    clean(); memset(v,0,sizeof *v);phase=-1;fail_mask=attempts=0;
    for(int k=0;k<MONSTERS;k++) {
        Mon *m=&v->mon[k];m->v=v;m->slot=k;v->scene.mon[k].bundle=&f->b;
        if(flags&(1u<<(2*k)))make_moves(m,f->n);
        if(flags&(2u<<(2*k))) {
            m->have_ag=dir_graph_read(&f->b,&m->ag)==0;assert(m->have_ag);
            dir_ai_init(&m->ai);
            if(m->have_graph) {
                float pos[3]={0};
                /* Guarantee one actual callback before teardown, then step real AI. */
                MoveInput in={0}; moves_step(&m->run,&in,0.01f,callback,m);
                dir_ai_step(&m->ai,&m->ag,&m->run,pos,0.01f,0.01f,MONSTERS,callback,m);
            }
        }
    }
}
static void rescue(Viewer *v) {
    /* Test-only cleanup after measuring the intentionally leaking reference. */
    phase=-1;
    for(int k=0;k<MONSTERS;k++) {moves_stop(&v->mon[k].run);moves_free(&v->mon[k].graph);dir_graph_free(&v->mon[k].ag);}
    clean();
}
static void observed_stop(MoveRunner *r) {
    assert(phase==-1 || phase==2);assert(r->g && r->g->states);graph_stops++;moves_stop(r);zero_run(r);
}
static void observed_move_free(MoveGraph *g) {
    assert(phase==-1 || phase==2);
    for(int k=0;k<MONSTERS;k++)if(&active->mon[k].graph==g)zero_run(&active->mon[k].run);
    graph_frees++;moves_free(g);zero_move(g);
}
static void observed_ai_free(DirGraph *a) {
    assert(phase==-1 || phase==2);
    for(int k=0;k<MONSTERS;k++)if(&active->mon[k].ag==a) {
        zero_run(&active->mon[k].run);zero_move(&active->mon[k].graph);
    }
    ai_frees++;dir_graph_free(a);zero_ai(a);
}
static void popup_free(Viewer *v) {assert(v==active && phase==0);phase=1;}
static void rnd_free(void *p) {(void)p;assert(phase==1);phase=2;}
static void scene_free(Scene *s) {
    assert(s==&active->scene && phase==2);assert(!live[1]);if(CANDIDATE)assert(!live[0]);phase=3;
}
static void city_close(void *p) {(void)p;assert(phase==(scene_was_live?3:2));phase=4;}
static void sound_end(void) {assert(phase==4);phase=5;}
static void sound_free(void) {assert(phase==5);phase=6;}
static void win_close(Window *w) {assert(phase==6 && w->alive);w->alive=0;phase=7;}
static int ap_close(void *p,const char *why) {(void)why;assert(p && phase==7);ap_calls++;phase=8;return ap_return;}
static int observed_fclose(FILE *f) {assert(f && phase==0);cap_closes++;return fclose(f);}
#define moves_stop observed_stop
#define moves_free observed_move_free
#define dir_graph_free observed_ai_free
#include "excerpts.inc"
#undef moves_stop
#undef moves_free
#undef dir_graph_free

static void all_flag_combinations(void) {
    const int counts[]={1,2,7,31,255};
    for(size_t j=0;j<sizeof counts/sizeof *counts;j++) {
        Fixture f;fixture(&f,counts[j]);
        for(unsigned flags=0;flags<256;flags++) {
            Viewer v;setup(&v,&f,flags);active=&v;
            size_t old_live=live[0],old_bytes=bytes[0],old_callbacks=callback_calls;
            extracted_final_loop(&v);assert(callback_calls==old_callbacks && !live[1]);
            if(CANDIDATE) {
                clean();
                for(int k=0;k<MONSTERS;k++)assert(!v.mon[k].have_graph && !v.mon[k].have_ag);
                for(int r=0;r<3;r++){extracted_final_loop(&v);repeated++;clean();}
            } else {assert(live[0]==old_live && bytes[0]==old_bytes);}
            if(flags==255)printf("%s n=%d before_owners=%zu before_bytes=%zu retained_owners=%zu retained_bytes=%zu\n",CANDIDATE?"CANDIDATE":"BASELINE",f.n,old_live,old_bytes,live[0],bytes[0]);
            rescue(&v);cases++;
        }
        free(f.blob);
    }
}
static void world_then_final(void) {
    Fixture f;fixture(&f,7);
    for(unsigned flags=0;flags<256;flags++) {
        Viewer v;setup(&v,&f,flags);active=&v;
        extracted_world_loop(&v);clean();extracted_final_loop(&v);clean();
        for(int k=0;k<MONSTERS;k++)assert(!v.mon[k].have_ag && !v.mon[k].have_graph);
        repeated++;
    }
    free(f.blob);puts("WORLD_RELEASE all_256_flags_then_final zero_owners");
}
static void absent_replacement(void) {
    Fixture f;fixture(&f,7);Viewer v;setup(&v,&f,3);active=&v;
    Bundle absent={0};v.scene.mon[0].bundle=&absent;
    assert(extracted_absent_prefix(&v.mon[0])==0);
    assert(!v.mon[0].have_graph && v.mon[0].have_ag && live[0]==7 && !live[1]);
    extracted_final_loop(&v);
    assert(live[0]==(CANDIDATE?0u:7u));rescue(&v);free(f.blob);
    puts("ABSENT_REPLACEMENT actual_moves_read have_graph=0_have_ag=1");
}
static void failed_and_partial(void) {
    Fixture f;fixture(&f,7);
    for(unsigned mask=1;mask<128;mask++) {
        Viewer v;setup(&v,&f,0x55);active=&v;attempts=0;fail_mask=mask;
        Mon *m=&v.mon[2];m->have_ag=dir_graph_read(&f.b,&m->ag)==0;assert(!m->have_ag);zero_ai(&m->ag);
        assert(!live[0]);extracted_final_loop(&v);clean();faults++;
        fail_mask=attempts=0;
        /* Partial-owner destructor contract, independent of caller success flags. */
        DirGraph a={.n=7};
        if(mask&1)a.action=dir_malloc(7);
        if(mask&2)a.fallback=dir_malloc(16*7);
        if(mask&4)a.hop=dir_malloc(7*7);
        if(mask&8)a.cost=dir_malloc(sizeof(float)*7);
        if(mask&16)a.pred=dir_malloc(7);
        if(mask&32)a.mark=dir_malloc(7);
        if(mask&64)a.queue=dir_malloc(7);
        dir_graph_free(&a);zero_ai(&a);clean();dir_graph_free(&a);zero_ai(&a);clean();partials++;
    }
    for(int absent=0;absent<3;absent++) {
        Viewer v;setup(&v,&f,0);active=&v;Bundle b=f.b;BdpEntry e=f.entry;
        b.entries=&e;if(absent==0)b.nentries=0;if(absent==1)e.type=ET_MESH;if(absent==2)e.data=NULL;
        assert(dir_graph_read(&b,&v.mon[3].ag)==-1);zero_ai(&v.mon[3].ag);extracted_final_loop(&v);clean();
    }
    free(f.blob);puts("FAILURES all_127_allocation_masks; PARTIAL_DESTRUCTOR all_127_owner_masks_twice; ABSENT 3");
}
static void final_paths(void) {
    Fixture f;fixture(&f,31);
    for(int initial=0;initial<2;initial++)
    for(int cap=0;cap<4;cap++)for(int scene=0;scene<2;scene++)for(int ap=0;ap<2;ap++)for(int problem=0;problem<2;problem++) {
        initial_exit_status=initial;
        Viewer v;setup(&v,&f,0xAB);active=&v;v.have_scene=scene;v.ap=ap?&v:NULL;
        v.rnd=&v;v.city=scene?&v:NULL;scene_was_live=scene;ap_return=problem?3:0;ap_calls=cap_closes=post_fail_reached=0;phase=0;
        FILE *cv=cap==2?fopen("capture-video.tmp","wb"):NULL,*ca=cap==3?fopen("capture-audio.tmp","wb"):NULL;
        assert(cap!=2 || cv);assert(cap!=3 || ca);
        size_t ai_before=live[0],move_before=live[1];
        int rc=extracted_shutdown(&v,cap,cv,ca);
        assert(rc==((cap || initial)?1:ap?ap_return:0));
        if(CANDIDATE || !cap) {
            assert(phase==(ap?8:7) && ap_calls==ap && !live[1]);
            assert(live[0]==(CANDIDATE?0:ai_before));
            if(cap)assert(cap_closes==((cap==2 || cap==3)?1:0) && !post_fail_reached);
        } else {
            assert(phase==0 && !ap_calls && !cap_closes && !post_fail_reached);
            assert(live[0]==ai_before && live[1]==move_before);
            /* Rescue after recording reference leak. */
            if(cv)fclose(cv);
            if(ca)fclose(ca);
        }
        rescue(&v);cases++;
    }
    initial_exit_status=0;
    free(f.blob);puts("SHUTDOWN initial_status_0_1 x normal_and_3_capture_failures x scene_0_1 x autoplay_0_1 x autoplay_return_0_3 = 64");
}
static void completed_capture(void) {
    Fixture f;fixture(&f,31);const unsigned flags[]={0,3,0xAB,255};
    for(int i=0;i<4;i++)for(int scene=0;scene<2;scene++)for(int problem=0;problem<2;problem++) {
        Viewer v;setup(&v,&f,flags[i]);active=&v;v.have_scene=scene;v.ap=&v;
        scene_was_live=scene;ap_return=problem?3:0;ap_calls=cap_closes=0;phase=0;
        FILE *cv=fopen("completed-video.tmp","wb"),*ca=fopen("completed-audio.tmp","wb");assert(cv && ca);
        size_t old_live=live[0];
        int rc=extracted_completed_capture(&v,cv,ca);
        assert(rc==ap_return && phase==8 && ap_calls==1 && cap_closes==2 && !live[1]);
        assert(live[0]==(CANDIDATE?0:old_live));rescue(&v);cases++;
    }
    free(f.blob);puts("COMPLETED_CAPTURE 16 routes closed_each_stream_once autoplay_return_preserved");
}
int main(void) {
    all_flag_combinations();world_then_final();absent_replacement();failed_and_partial();final_paths();completed_capture();clean();
    assert(callback_calls>0);
    printf("FINAL candidate=%d cases=%zu fault_masks=%zu partial_masks=%zu repeated_graph_cleanup=%zu callbacks_before_shutdown=%zu stop=%zu moves_free=%zu dir_free=%zu tracked_live=0 tracked_bytes=0 peak_ai_bytes=%zu peak_move_bytes=%zu\n",CANDIDATE,cases,faults,partials,repeated,callback_calls,graph_stops,graph_frees,ai_frees,peak[0],peak[1]);
    return 0;
}
