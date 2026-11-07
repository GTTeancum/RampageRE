/* The generated loader and dispatch retain exact current-source statements from main.c. */
typedef struct { struct { struct { const Bundle *bundle; } mon[1]; } scene; } ProbeViewer;
typedef struct { int slot, have_ag, is_ai; DirGraph ag; DirAI ai; MoveRunner run; } ProbeMon;
static int ai_entries, pad_entries;
static int count_ai_step(DirAI *ai,DirGraph *ag,MoveRunner *r,const float *p,float dt,float wdt,int n,MoveCall call,void *ctx){
    ai_entries++;return dir_ai_step(ai,ag,r,p,dt,wdt,n,call,ctx);
}
static int count_moves_step(MoveRunner *r,MoveInput *i,float dt,MoveCall call,void *ctx){
    pad_entries++;return moves_step(r,i,dt,call,ctx);
}
static void caller_load(ProbeViewer *v,ProbeMon *mo){
@LOAD@
}
static int caller_frame(ProbeMon *mo,int ev){
    float root[3]={0},cdt=1.0f/30.0f,dt=cdt;int nai=1;
    MoveInput input={0};MoveInput *in=&input;MoveCall answer=NULL;
#define dir_ai_step count_ai_step
#define moves_step count_moves_step
@DISPATCH@
#undef dir_ai_step
#undef moves_step
    return switched;
}
static void caller_checks(void){
    Fixture f=make_fixture(7);bind_fixture(&f);ProbeViewer v={0};v.scene.mon[0].bundle=&f.b;
    for(int fail=0;fail<=7;fail++){
        ProbeMon m={0};reset((size_t)fail);caller_load(&v,&m);
        assert(m.have_ag==(fail==0));assert(m.is_ai==0);
        assert(!moves_start(&m.run,&f.g,0));m.is_ai=1;
        /* GE_AI may queue a bounded goal even without a DirGraph. It takes only MoveGraph. */
        assert(dir_ai_destroy(&m.ai,&f.g,1)>=0);
        ai_entries=pad_entries=0;caller_frame(&m,1);
        assert(ai_entries==(fail==0));assert(pad_entries==(fail!=0));
        ai_entries=pad_entries=0;assert(caller_frame(&m,0)==1);assert(!ai_entries&&!pad_entries);
        moves_stop(&m.run);dir_graph_free(&m.ag);zero_graph(&m.ag);
    }
    drop_fixture(&f);puts("CALLER exact_source_load_and_frame successful_ai=1 failure_fallback=7 event_short_circuit=8 zero_owners=8");
}
