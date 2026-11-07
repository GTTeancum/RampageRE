static Viewer v;
static MoveEvent events[3]={{.at=0},{.at=0},{.at=0}};
static MoveNode face_node={.clip="Test",.blend=.25f,.rate=1,.events=events,.nevents=3};
static MoveState body_node={.clip="Test",.blend=.25f,.name="Test"};
static int event_count,event_fail,root_count,fx_calls;
static void face_play(Mon*,const char*,int,int,int);
static void clip_event(Mon*m,const MoveEvent*e){(void)e;event_count++;if(event_fail)m->v->scene.construction_failed=1;}
const MoveNode *moves_node(const MoveGraph*g,const char*n){(void)g;(void)n;return &face_node;}
int moves_events_between(const MoveState*st,float a,float b,const MoveEvent**e){(void)st;(void)a;(void)b;*e=events;return 3;}
static double clip_at(Mon*m,const MoveState*st,double t){(void)m;(void)st;return t;}
static float root_yaw(Mon*m,double t){(void)m;(void)t;root_count++;return 0;}
static int root_at(Mon*m,double t,float tr[3],float q[4]){(void)m;(void)t;root_count++;memset(tr,0,12);memset(q,0,16);return 0;}
void climb_clip_started(Mount*m,const char*n){(void)m;(void)n;}
static int mon_fx_named(Mon*m,const char*a,const char*b){(void)m;(void)a;(void)b;fx_calls++;return 0;}
#include "main-functions.inc"
static Mon *ready(void){setup();memset(&v,0,sizeof v);v.scene=s;v.city=&status_owner;v.play=1;Mon*m=&v.mon[0];m->v=&v;m->slot=0;m->graph.states=&body_node;m->faces.list[0]=(FaceRec){.node=&face_node,.name="Test",.loop=1};m->faces.n=1;m->power.speed=1;event_count=event_fail=root_count=fx_calls=0;face_node.clip="Test";return m;}
int main(void){
{ Mon*m=ready();status_owner.load_failed=1;face_start(m);
  assert(v.exit_game&&!v.play&&v.scene.construction_failed&&!calls&&!event_count&&!root_count);tidy(); }

for(int f=1;f<=4;f++){Mon*m=ready();m->face_ev=123;fail_at=f;face_start(m);assert(v.scene.construction_failed&&v.exit_game&&!v.play&&!event_count&&m->face_ev==123);int c=calls;face_play(m,"Test",1,0,1);face_step(m,.1f);play_state(m);assert(calls==c&&!event_count&&!root_count);tidy();}
Mon*m=ready();face_node.clip="missing";face_start(m);assert(!v.exit_game&&!v.scene.construction_failed&&!event_count&&!m->face);tidy();
m=ready();event_fail=1;face_start(m);assert(event_count==1&&v.exit_game);tidy();
m=ready();m->face=&face_node;event_fail=1;face_step(m,.1f);/* no at-zero event in (0,.1], so force direct batch */face_events(m,&face_node,-1,0);assert(event_count==1&&v.exit_game);tidy();
m=ready();event_fail=1;clip_events(m,&body_node,0,1);assert(event_count==1&&v.exit_game);tidy();
m=ready();m->face=&face_node;in.face_t=1;fail_at=1;face_step(m,.1f);assert(v.exit_game&&!event_count);tidy();
m=ready();m->face=&face_node;m->faces.nbelch=1;m->faces.belch[0]=.01f;fail_at=1;face_step(m,.1f);assert(v.exit_game&&!event_count&&m->face_ev==0);tidy();
for(int f=1;f<=3;f++){m=ready();in.blend_left=1;m->mirror_queued=1;fail_at=f;play_state(m);assert(v.exit_game&&!v.play&&v.scene.construction_failed);assert(root_count==(f==3?1:0));tidy();}
m=ready();fail_at=1;mon_fart(m);assert(v.exit_game&&!fx_calls&&!m->vel[2]);tidy();
m=ready();face_start(m);assert(event_count==3&&!v.exit_game);play_state(m);assert(root_count==2&&!v.exit_game);tidy();puts("face events, restart, delayed request, body/mirror root stops, optional absence, reentry: PASS");return 0;}
