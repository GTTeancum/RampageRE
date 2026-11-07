/* Current source is extracted into generated fragments. Dependency stubs enforce
 * ownership, not a real world, script engine, GPU or audio implementation. See README.md. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include "main-types.h"
#include "runner-api.h"
#include "city-status.inc"
#include "scene_construction_stop.inc"
#include "world_load_failed.inc"

enum { WINDOW=1, GRAPH, AIGRAPH, MODEL, INSTANCES, CITY, MANAGER, GAME, RENDERER, POPUP, GUI };
typedef struct { void *p; size_t size; int kind; } Live;
static Live live[256];
static int nlive, counts[16], freed[16], attempts, fail_window, absent_graph, properties, postinit, played;
static int failure_seen, trace[4096], ntrace, detached, read_calls, game_created, build_calls, frame_failure;
static size_t allocations_by_domain[3];
static void mark(int x){assert(ntrace<4096);trace[ntrace++]=x;}
static void *own(size_t n,int kind){void*p=calloc(1,n?n:1);assert(p);for(int i=0;i<256;i++)if(!live[i].p){live[i]=(Live){p,n,kind};nlive++;counts[kind]++;return p;}abort();}
static void release(void*p){if(!p)return;for(int i=0;i<256;i++)if(live[i].p==p){counts[live[i].kind]--;freed[live[i].kind]++;nlive--;live[i]=(Live){0};free(p);return;}assert(!"double/unowned free");}
typedef unsigned GLuint;typedef unsigned GLenum;typedef int GLsizei;
#include "renderer-types.inc"
static int gl_ids[32],buf_ids[32],finish_calls,texture_deletes,buffer_deletes;
static void glFinish(void){finish_calls++;}
static void glDeleteTextures(GLsizei n,const GLuint*ids){for(int i=0;i<n;i++){assert(ids[i]<32 && gl_ids[ids[i]]);gl_ids[ids[i]]=0;texture_deletes++;}}
static void buffer_delete(GLsizei n,const GLuint*ids){for(int i=0;i<n;i++)if(ids[i]){assert(ids[i]<32 && buf_ids[ids[i]]);buf_ids[ids[i]]=0;buffer_deletes++;}}
#define free release
#define RAMPAGE_XBOX 1
#include "renderer-teardown.inc"
#undef RAMPAGE_XBOX
#undef free
void *audit_calloc(int d,size_t n,size_t z){allocations_by_domain[d]++;attempts++;if(fail_window && attempts==fail_window){failure_seen=1;return NULL;}return own(n*z,WINDOW);}
void audit_free(int d,void*p){(void)d;release(p);}
static void mon_fx_stop_all(Mon*m){(void)m;mark(1);}
static void mon_rage_fx(Mon*m,int on){(void)m;mark(2+on);}
static Group army_find(void*ctx,const char*book,const char*name){(void)ctx;(void)book;(void)name;return (Group){0};}
static void power_from_profile(Mon*m){mark(15);m->power.abilities=7;}
static void play_state(Mon*m){assert(m->have_graph && m->run.g && m->run.in_window && m->run.sw_window);assert(!m->v->exit_game);played++;mark(16);}
static void face_default(Mon*m){assert(m->have_graph && m->run.g && !failure_seen);m->face=m->graph.nodes;postinit++;mark(14);}
#include "mon_in.inc"
#include "mon_shake.inc"
#include "have_monster.inc"
#include "mon_hp_max.inc"
#include "mon_props.inc"
Group city_book_group(City*c,const char*b,const char*g){(void)c;(void)b;(void)g;assert(!failure_seen);properties++;return (Group){0};}
Group city_book_group_wii(City*c,const char*b,const char*g){return city_book_group(c,b,g);}
const char *group_str(Group g,const char*n){(void)g;(void)n;return NULL;}
float group_num(Group g,const char*n,float def){(void)g;(void)n;return def;}
void foot_props_default(FootProps*p){memset(p,0,sizeof*p);mark(4);}
int foot_props_read(FootProps*p,Group g){(void)p;(void)g;mark(5);return 0;}
void foot_init(FootState*s,int no){(void)no;memset(s,0,sizeof*s);mark(6);}
void foot_ik_props(FootState*s,Group g,const Tree*t){(void)s;(void)g;(void)t;mark(7);}
void fx_stop(FxWorld*w,int id){(void)w;(void)id;mark(8);}
void army_mon_weapons_read(ArmyMonWeapons*w,Group g,Group(*f)(void*,const char*,const char*),void*c){(void)g;(void)f;(void)c;memset(w,0,sizeof*w);mark(9);}
void army_mon_weapons_read_checked(ArmyMonWeapons*w,Group g,Group(*f)(void*,const char*,const char*),void*c,int(*failed)(void*)){
 if(failed && failed(c))return;
 army_mon_weapons_read(w,g,f,c);
 if(failed && failed(c))return;
}
void look_limits_default(LookLimits*l){memset(l,0,sizeof*l);mark(13);}
void edible_power_init(EdiblePower*p){memset(p,0,sizeof*p);}
void face_init(FaceQueue*q){memset(q,0,sizeof*q);mark(12);}
void dir_ai_init(DirAI*a){memset(a,0,sizeof*a);a->force=-1;mark(11);}
Mat4 mat_tq(const float*t,const float*q){(void)t;(void)q;return (Mat4){0};}
int moves_read(const Bundle*b,MoveGraph*g){
 (void)b;read_calls++;memset(g,0,sizeof*g);if(absent_graph)return -1;
 g->n=3;g->states=own(3*sizeof*g->states,GRAPH);g->all=own(6*sizeof*g->all,GRAPH);g->nodes=own(sizeof*g->nodes,GRAPH);g->nnodes=1;g->props="George_prop";g->hp_max=200;
 for(int k=0;k<3;k++){g->states[k].name=k==0?"Ground_Idle":k==1?"Other":"Zero";g->states[k].length=1;g->states[k].next=k;}
 g->states[0].ntrans=1;g->states[0].trans=g->all;g->states[1].ntrans=5;g->states[1].trans=g->all+1;return 0;
}
void moves_free(MoveGraph*g){release(g->states);release(g->all);release(g->nodes);memset(g,0,sizeof*g);}
int moves_find(const MoveGraph*g,const char*n){for(int i=0;i<g->n;i++)if(!strcmp(g->states[i].name,n))return i;return -1;}
int dir_graph_read(const Bundle*b,DirGraph*g){(void)b;assert(!failure_seen);memset(g,0,sizeof*g);g->action=own(3,AIGRAPH);g->n=3;mark(10);return 0;}
void dir_graph_free(DirGraph*g){release(g->action);memset(g,0,sizeof*g);}
#define moves_start c_start
#define moves_stop c_stop
#include "start_moves.inc"
#include "baseline-start.c"
static int monster_index(const Viewer*v,const char*n){for(uint32_t i=0;i<v->nmonsters;i++)if(!strcmp(v->monsters[i],n))return (int)i;return -1;}
int city_monster(City*c,Scene*s,const char*n,int clip,float x,float y,float heading){
 (void)c;(void)n;(void)clip;(void)x;(void)y;(void)heading;assert(s->nmon<4 && !failure_seen);int k=s->nmon++;
 s->mon[k].inst=k;s->mon[k].bundle=own(sizeof(Bundle),MODEL);s->mon[k].clip="fixture";return k;
}
#include "extra_add.inc"
static int monster_female(Mon*m){(void)m;return 0;}
#include "build-monsters.inc"
int reveal_extra_slot(const Reveal*r,int players){(void)r;return players;}
#include "build-extra.inc"

void sound_objects_clear(void){mark(20);}
void sound_effect_stop_all(void){mark(21);}
void fx_clear(FxWorld*w){(void)w;mark(22);}
static void huds_close(Viewer*v){for(int k=0;k<4;k++)v->mon[k].hud=NULL;v->hud=NULL;mark(23);}
static void block_managers_free(Viewer*v){release(v->army);v->army=NULL;mark(24);}
void game_free(Game*g){release(g);mark(25);}
void scene_free(Scene*s){
 if(counts[MODEL] || counts[INSTANCES])assert(detached);assert(!counts[WINDOW] && !counts[GRAPH] && !counts[AIGRAPH]);
 for(int k=0;k<s->nmon;k++)release((void*)s->mon[k].bundle);
 release(s->instances);memset(s,0,sizeof*s);mark(26);
}
void city_close(City*c){assert(!counts[MODEL] && !counts[INSTANCES] && !counts[MANAGER]);release(c);mark(27);}
void city_activate(City*c){assert(!c);mark(28);}
void rnd_scene(Renderer*r,const Scene*s){assert(!s);renderer_scene(r,s);detached=1;mark(19);}
void rnd_free(Renderer*r){renderer_free(r);}
static void shell_profile(Viewer*v,ShellProfile*p){(void)v;memset(p,0,sizeof*p);mark(18);}
static void gui3d_free(Viewer*v){release(v->gd);v->gd=NULL;mark(29);}
void shell_close(Shell*s){release(s);}
#include "popup-free.inc"
#include "world_release.inc"
#include "shell-release.inc"

/* These are dependencies of the extracted complete main-loop rebuild block.
 * They deliberately fail a selected load, never simulate game state. */
static void build(Viewer*v){build_calls++;if(frame_failure==build_calls){v->play=0;v->exit_game=1;}}
static void game_next_block(Viewer*v){if(v->game_block>=0)build(v);}
static int open_city(Viewer*v,int city,uint32_t block,int gb){(void)city;(void)block;(void)gb;build(v);return v->have_scene&&!v->exit_game?0:-1;}
void cam_init(GameCamera*s,const CamSettings*c){(void)c;memset(s,0,sizeof*s);}
void dircam_init(CamDirector*d,GameCamera*c){(void)c;memset(d,0,sizeof*d);}
#include "frame-guards.inc"
#include "base-frame-guards.inc"
static int result_reads,missing_message,failed_exit_expected;
static ShellStart captured_shell_start;
static void assert_safe_failed_start(const ShellStart*s,const Viewer*v){
 assert(s && s->from==SHELL_FROM_GAME && s->quit==1 && s->choose_monster==0);
 assert(s->city_destroyed==0 && s->show_victory==0 && s->king_of_world==-1);
 assert(!memcmp(&s->game,&v->last_game,sizeof s->game));
}
static int game_result(const Game*g){assert(!failed_exit_expected);assert(!g || counts[GAME]);result_reads++;return 0;}
int game_quitting(const Game*g){return game_result(g);}
int game_choose_monster(const Game*g){return game_result(g);}
int game_city_destroyed(const Game*g){return game_result(g);}
int game_world_done(const Game*g){return game_result(g);}
int game_king(const Game*g,int which){(void)which;return game_result(g);}
void ap_game_left(Autoplay*a,int demo){(void)a;(void)demo;}
static void attract_end(Viewer*v,ShellStart*s){(void)v;(void)s;}
static void shell_enter(Viewer*v,const ShellStart*s){
 if(failed_exit_expected){assert(result_reads==0);assert_safe_failed_start(s,v);}
 else assert(s && result_reads>=5); /* unchanged healthy getter requirement */
 captured_shell_start=*s;shell_release(v);
}
static const char*shell_script(Viewer*v,const char*s){(void)v;return s;}
static void shell_start_game(Viewer*v){(void)v;assert(!"shot/script branch outside this fixture");}
void plat_message(const char*s,int warning){(void)s;(void)warning;missing_message++;}
#include "exit-dispatch.inc"
#include "direct-start.inc"
#include "open-return.inc"
#include "ordinary-teardown.inc"

static void fixture(Viewer*v,int players){
 memset(v,0,sizeof*v);v->nmon=players;v->extra=-1;v->game_block=-1;v->play=1;v->have_scene=1;v->nblocks=1;v->nmonsters=4;v->clip=-1;v->rnd=own(sizeof(Renderer),RENDERER);
 v->scene.instances=own(4*sizeof*v->scene.instances,INSTANCES);v->city=own(sizeof *v->city,CITY);v->scene.owner=v->city;v->army=own(1,MANAGER);v->game=own(1,GAME);
 const char*names[]={"George","Lizzie","Ralph","Boris"};
 for(int k=0;k<4;k++){v->monsters[k]=names[k];v->mon[k].v=v;v->mon[k].slot=k;v->mon[k].monster=k;}
 v->roster.n=4;for(int k=0;k<4;k++)v->roster.name[k]=names[k];
 detached=0;failure_seen=0;absent_graph=0;fail_window=0;attempts=0;properties=postinit=played=read_calls=game_created=ntrace=0;
}
static void close_world(Viewer*v){Window w={0};v->shell_mode=1;v->exit_game=1;result_reads=0;failed_exit_expected=v->scene.construction_failed||city_load_failed(v->city);assert(!exit_dispatch(v,&w));
 if(failed_exit_expected){assert(result_reads==0 && !w.quit);assert_safe_failed_start(&captured_shell_start,v);}
 else assert(result_reads==5 && !w.quit); /* preserve healthy oracle */
 failed_exit_expected=0;ordinary_teardown(v);assert(!nlive);assert(!counts[WINDOW] && !counts[GRAPH] && !counts[AIGRAPH]);for(int k=0;k<4;k++){assert(!v->mon[k].have_graph && !v->mon[k].have_ag && !v->mon[k].face && !v->mon[k].ai.view);}assert(!v->have_scene && !v->city);}
static void setup_old(Viewer*v){build_monsters(v);assert(!v->exit_game);for(int k=0;k<v->nmon;k++){Mon*m=&v->mon[k];m->ai.force=2;m->ai.view=(DirBotView*)(uintptr_t)123;m->faces.n=1;m->faces.list[0].node=m->graph.nodes;m->face=m->graph.nodes;m->hp=123;m->energy=45;m->power.rage=1;}}
static void next_scene(Viewer*v){for(int k=0;k<v->scene.nmon;k++)release((void*)v->scene.mon[k].bundle);memset(v->scene.mon,0,sizeof v->scene.mon);v->scene.nmon=0;v->game_built=1;failure_seen=0;}
static void compare_mon(Mon*a,Mon*b){
 assert(a->have_graph==b->have_graph && a->have_ag==b->have_ag && a->is_ai==b->is_ai && a->run.state==b->run.state && a->run.time==b->run.time);
 Mon x=*a,y=*b;x.v=y.v=NULL;memset(&x.graph,0,sizeof x.graph);memset(&y.graph,0,sizeof y.graph);memset(&x.ag,0,sizeof x.ag);memset(&y.ag,0,sizeof y.ag);x.face=y.face=NULL;
 x.run.g=y.run.g=NULL;x.run.in_window=y.run.in_window=NULL;x.run.sw_window=y.run.sw_window=NULL;
 assert(!memcmp(&x,&y,sizeof x));
}
static void reference_cleanup(Viewer*v){
 for(int k=0;k<4;k++)if(v->mon[k].have_graph){b_stop(&v->mon[k].run);moves_free(&v->mon[k].graph);v->mon[k].have_graph=0;}
 close_world(v);
}
static void set_old_face(Mon*m){m->ai.force=2;m->ai.view=(DirBotView*)(uintptr_t)123;m->faces.n=1;m->faces.list[0].node=m->graph.nodes;m->face=m->graph.nodes;m->hp=123;m->energy=45;}
static void equivalence(void){
 static Viewer a,b;int cases=0;
 for(int prior=0;prior<2;prior++)for(int play=0;play<2;play++)for(int which=0;which<4;which++){
  fixture(&a,1);a.play=play;a.mon[0].monster=which;city_monster(a.city,&a.scene,"fixture",-1,0,0,0);
  if(prior){reference_start_moves(&a.mon[0]);set_old_face(&a.mon[0]);}
  ntrace=0;reference_start_moves(&a.mon[0]);int bt[4096],bn=ntrace;memcpy(bt,trace,(size_t)bn*sizeof*trace);Mon saved=a.mon[0];reference_cleanup(&a);
  fixture(&b,1);b.play=play;b.mon[0].monster=which;city_monster(b.city,&b.scene,"fixture",-1,0,0,0);
  if(prior){assert(!start_moves(&b.mon[0]));set_old_face(&b.mon[0]);}
  ntrace=0;assert(!start_moves(&b.mon[0]));assert(ntrace==bn && !memcmp(bt,trace,(size_t)bn*sizeof*trace));compare_mon(&saved,&b.mon[0]);close_world(&b);cases++;
 }
 /* All three pre-existing no-graph cases retain their nonfatal status. */
 for(int missing=0;missing<3;missing++){
  fixture(&a,1);if(missing!=0)city_monster(a.city,&a.scene,"George",-1,0,0,0);
  const Bundle *bundle=missing==1?a.scene.mon[0].bundle:NULL;if(missing==1)a.scene.mon[0].bundle=NULL;
  absent_graph=missing==2;reference_start_moves(&a.mon[0]);Mon old=a.mon[0];ntrace=0;assert(start_moves(&a.mon[0])==0);assert(!memcmp(&old,&a.mon[0],sizeof old));assert(!a.exit_game && !a.mon[0].have_graph);
  if(missing==1)a.scene.mon[0].bundle=bundle;close_world(&a);
 }
 printf("PASS %d full start_moves normal/replacement paths, exact state and dependency trace; three absent-graph paths nonfatal and unchanged\n",cases);
}
static void initial_and_rebuild(void){
 static Viewer v;int cases=0;
 for(int prior=0;prior<2;prior++)for(int players=1;players<=4;players++)for(int fail=1;fail<=players;fail++){
  fixture(&v,players);if(prior){setup_old(&v);next_scene(&v);}int old_properties=properties,old_post=postinit;fail_window=attempts+fail;build_monsters(&v);
  assert(v.exit_game && !v.play);assert(v.scene.nmon==fail);Mon*m=&v.mon[fail-1];assert(!m->have_graph && !m->have_ag && !m->run.g && !m->face && !m->faces.n && !m->ai.view && m->ai.force==-1 && !m->is_ai);
  assert(properties-old_properties==3*(fail-1) && postinit-old_post==fail-1);
  if(prior){assert(m->ai.force!=2);for(int k=0;k<fail-1;k++)assert(v.mon[k].ai.force==2 && v.mon[k].hp==123 && v.mon[k].energy==45);}
  close_world(&v);cases++;
  fixture(&v,players);build_monsters(&v);assert(!v.exit_game && v.scene.nmon==players);for(int k=0;k<players;k++){assert(v.mon[k].have_graph);MoveInput in={0};assert(c_step(&v.mon[k].run,&in,2,NULL,NULL)==1);}close_world(&v);
 }
 printf("PASS %d initial/rebuild OOM cases with every multiplayer failure position, no post-init on failure, retry and zero ownership ledger\n",cases);
}
static void extra_tests(void){
 static Viewer v;fixture(&v,1);build_monsters(&v);fail_window=attempts+1;int before=game_created;build_extra(&v,3);
 assert(v.exit_game && !v.play && v.extra==-1 && v.scene.nmon==2 && game_created==before);assert(counts[MODEL]==2);assert(!v.mon[1].have_graph && !v.mon[1].have_ag && !v.mon[1].face);close_world(&v);
 fixture(&v,1);build_monsters(&v);build_extra(&v,3);assert(!v.exit_game && v.extra==1 && v.scene.nmon==2 && mon_in(&v.mon[1])->hidden && game_created==1);close_world(&v);
 puts("PASS extra model owned before failed runner, unpublished reveal, no game creation, normal exit releases it, retry succeeds");
}
static void guard_tests(void){
 static Viewer v;Window w;int keys[]={VK_NEXT,VK_PRIOR,'N','M','L','2'};int cases=0;
 for(int k=-1;k<6;k++){memset(&v,0,sizeof v);memset(&w,0,sizeof w);v.nmon=1;v.nmonsters=4;v.nblocks=3;v.game_block=k==-1?0:-1;v.have_scene=1;v.clip=-1;v.mon[0].monster=0;if(k>=0)w.pressed[keys[k]]=1;
  /* A simultaneous later toggle/rebuild must not run after the first failed load. */
  w.pressed['G']=w.pressed['K']=w.pressed['3']=1;build_calls=0;frame_failure=1;assert(!frame_guards(&v,w));assert(v.exit_game && !v.play && build_calls==1);cases++;
 }
 memset(&v,0,sizeof v);memset(&w,0,sizeof w);v.nmon=1;v.nmonsters=4;v.nblocks=3;v.game_block=-1;v.have_scene=1;w.pressed['N']=1;build_calls=0;frame_failure=1;assert(unguarded_frame_guards(&v,w));assert(v.exit_game); /* baseline falls through */
 for(int k=0;k<6;k++){Viewer base={0},candidate={0};memset(&w,0,sizeof w);base.nmon=1;base.nmonsters=4;base.nblocks=3;base.game_block=-1;base.have_scene=1;base.mon[0].monster=0;base.clip=-1;candidate=base;w.pressed[keys[k]]=1;frame_failure=0;build_calls=0;int a=unguarded_frame_guards(&base,w),n=build_calls;build_calls=0;int b=frame_guards(&candidate,w);assert(a==b && n==build_calls && !memcmp(&base,&candidate,sizeof base));}
 printf("PASS %d rebuild/city-switch same-frame failure guards, unguarded control-flow fallthrough reproduced, six normal control-flow comparisons\n",cases);
 memset(&v,0,sizeof v);v.have_scene=1;build_calls=0;frame_failure=1;assert(open_return(&v)==-1 && v.have_scene && v.exit_game);
 missing_message=0;assert(direct_start_check(-1,1)==1 && !missing_message); /* actual fatal startup now preserves status 1 */assert(direct_start_check(-1,0)==1 && missing_message==1);assert(!direct_start_check(0,0));
 memset(&v,0,sizeof v);memset(&w,0,sizeof w);v.exit_game=1;assert(!exit_dispatch(&v,&w) && w.quit && !v.exit_game);
 puts("PASS open_city rejects partial scene, direct startup preserves missing-bundle distinction, bare viewer exits before simulation");
}
static void renderer_keys(Viewer*v){
 Renderer*r=v->rnd;r->scene=&v->scene;r->ncache=1;r->cache[0].b=v->scene.mon[0].bundle;r->cache[0].n=1;
 r->cache[0].tex=own(sizeof(GLuint),RENDERER);r->cache[0].tex[0]=1;gl_ids[1]=1;
 r->cache[0].hud_tex=own(sizeof(GLuint),RENDERER);r->cache[0].hud_tex[0]=2;gl_ids[2]=1;
 r->cache[0].reg=own(sizeof(RegionTex),RENDERER);r->cache[0].nreg=1;r->cache[0].reg[0].id=3;gl_ids[3]=1;
 r->video_tex=4;gl_ids[4]=1;r->nmbuf=1;r->mbuf=own(sizeof(MeshBuf),RENDERER);r->mbuf[0].nparts=1;r->mbuf[0].pb=own(sizeof(PartBuf),RENDERER);r->mbuf[0].pb[0].vbo=5;r->mbuf[0].pb[0].ibo=6;buf_ids[5]=buf_ids[6]=1;g_buf.del=buffer_delete;
 r->nbound=1;r->bound=own(sizeof(MeshBound),RENDERER);r->held=own(sizeof(HeldPart),RENDERER);r->stage=own(16,RENDERER);r->water_uv=own(16,RENDERER);r->nwater_uv=2;
}
static void bare_exit_tests(void){
 static Viewer v;Window w;int cases=0;
 for(int state=0;state<4;state++){
  memset(&w,0,sizeof w);finish_calls=texture_deletes=buffer_deletes=0;
  if(state==0){memset(&v,0,sizeof v);detached=0;}
  else {fixture(&v,3);if(state==1)fail_window=2;build_monsters(&v);if(state>=2)for(int k=0;k<3;k++)set_old_face(&v.mon[k]);renderer_keys(&v);v.popup=own(1,POPUP);v.gd=own(1,GUI);}
  /* Shared account/profile/Vol and GUI bundles are deliberately outside this cleanup. */
  v.gui_world.file=(uint8_t*)(uintptr_t)0x1234;v.vol.names=(char*)(uintptr_t)0x5678;v.prof.loaded=1;
  v.exit_game=1;assert(!exit_dispatch(&v,&w) && w.quit && !v.exit_game);assert(!v.have_scene && !v.city && !v.popup && !v.gd && !counts[WINDOW] && !counts[GRAPH] && !counts[AIGRAPH] && !counts[MODEL] && !counts[MANAGER]);
  if(state){assert(v.rnd->ncache==0 && !v.rnd->mbuf && !v.rnd->scene);assert(texture_deletes==3 && buffer_deletes==2 && finish_calls==1);}
  ordinary_teardown(&v);assert(!nlive);assert(v.gui_world.file==(uint8_t*)(uintptr_t)0x1234 && v.vol.names==(char*)(uintptr_t)0x5678 && v.prof.loaded==1);
  if(state)assert(texture_deletes==4 && buffer_deletes==2 && finish_calls==2);
  cases++;
 }
 printf("PASS %d low-memory bare-viewer null/partial/full exits: real renderer drop/free, stale keys discarded before world, subsequent ordinary teardown zero ledger\n",cases);
}
/* Only the actual initial shell Start guard is executed; successful launch
 * beyond it is outside this owner-release fixture. */
static int shell_start_count;
#include "shell-start-guard.inc"
static void status_recovery_tests(void){
 static Viewer v;
 for(int kind=0;kind<3;kind++){
  fixture(&v,2);build_monsters(&v);renderer_keys(&v);
  if(kind==0)v.have_scene=0; /* partially owned Scene, not a published world */
  if(kind==2)v.city->load_failed=1;
  else v.scene.construction_failed=1;
  int before=shell_start_count;
  shell_start_probe(&v);
  assert(shell_start_count==before && v.exit_game && !v.play);
  shell_release(&v); /* extracted shell boundary invokes actual world_release */
  Scene empty={0};assert(!memcmp(&v.scene,&empty,sizeof empty));
  assert(!v.city&&!v.have_scene&&!counts[MODEL]&&!counts[INSTANCES]&&!counts[MANAGER]);
  assert(!counts[WINDOW]&&!counts[GRAPH]&&!counts[AIGRAPH]&&!counts[GAME]);
  shell_start_probe(&v);assert(shell_start_count==before+1);
  ordinary_teardown(&v);assert(!nlive);
 }
 puts("PASS 3 failed-world releases and shell Start guard retries: partial Scene, runtime Scene, City owner failure");
}
/* Positive control for ordinary-memory retention. The final process-exit
 * fragment does not cover every manager/DirGraph owner, so test disposal is
 * explicitly world_release followed by the existing final-tail fragment. */
static void healthy_retention_test(void){
#ifndef RAMPAGE_LOW_MEMORY
 static Viewer v;fixture(&v,2);build_monsters(&v);renderer_keys(&v);
 City *city=v.city;Army *army=v.army;Game *game=v.game;Scene saved=v.scene;
 MoveGraph graph[2]={v.mon[0].graph,v.mon[1].graph};
 int held_counts[16];memcpy(held_counts,counts,sizeof counts);int held_live=nlive;
 shell_release(&v);
 assert(v.city==city&&v.army==army&&v.game==game&&v.have_scene);
 assert(!memcmp(&v.scene,&saved,sizeof saved)&&!memcmp(held_counts,counts,sizeof counts)&&nlive==held_live);
 assert(!memcmp(&v.mon[0].graph,&graph[0],sizeof graph[0])&&!memcmp(&v.mon[1].graph,&graph[1],sizeof graph[1]));
 assert(v.rnd->scene==&v.scene&&v.rnd->ncache==1&&!detached);
 rnd_scene(v.rnd,NULL);world_release(&v);ordinary_teardown(&v);assert(!nlive);
 puts("PASS healthy ordinary shell retains City, Scene, managers, Game, graphs and renderer keys");
#endif
}
static void safe_failed_exit_tests(void){
 static Viewer v;Window w;
 for(int kind=0;kind<3;kind++){
  fixture(&v,2);build_monsters(&v);renderer_keys(&v);memset(&w,0,sizeof w);
  if(kind==0)v.have_scene=0;
  if(kind==2){v.city->load_failed=1;assert(!v.scene.construction_failed);}
  else v.scene.construction_failed=1;
  /* Poisoned progress must not be read, and retained previous choices survive. */
  v.kept.quit=0;v.kept.choose_monster=v.kept.city_destroyed=v.kept.show_victory=1;v.kept.king_of_world=2;
  v.last_game.play_type=2;v.last_game.num_players=2;v.last_game.seed[0]=37;v.last_game.seed[1]=83;
  strcpy(v.last_game.city,"LasVegas");v.shell_mode=1;v.exit_game=1;v.play=1;
  result_reads=0;failed_exit_expected=1;
  assert(!exit_dispatch(&v,&w));assert(!w.quit&&result_reads==0);
  assert_safe_failed_start(&captured_shell_start,&v);failed_exit_expected=0;
  Scene empty={0};assert(!memcmp(&v.scene,&empty,sizeof empty));
  assert(!v.city&&!v.have_scene&&!counts[MODEL]&&!counts[INSTANCES]&&!counts[MANAGER]&&!counts[GAME]);
  assert(!counts[WINDOW]&&!counts[GRAPH]&&!counts[AIGRAPH]);
  int before=shell_start_count;shell_start_probe(&v);assert(shell_start_count==before+1);
  ordinary_teardown(&v);assert(!nlive);
 }
 puts("PASS 3 poisoned-Game exits: zero result getters, safe ShellStart, prior game choices retained, clean Start guard");
}
#include "runner_regression.c"
int main(void){assert_city_status_contract();
#ifdef STATUS_RECOVERY_ONLY
 status_recovery_tests();healthy_retention_test();safe_failed_exit_tests();return 0;
#endif
 status_recovery_tests();healthy_retention_test();safe_failed_exit_tests();runner_tests();equivalence();initial_and_rebuild();extra_tests();guard_tests();bare_exit_tests();assert(!nlive);puts("PASS move lifetime and low-memory startup integration regressions");return 0;}
