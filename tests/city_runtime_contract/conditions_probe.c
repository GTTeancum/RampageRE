/* Exact current-main fragments in synthetic drivers. This is control-flow proof,
 * not a whole-Viewer or GPU/audio integration test. Real City/manager injected
 * allocation failures and owned lifetimes are tested separately. */
#include <assert.h>
#include "types.h"
struct City { int failed; };
static City owned;
static int inject,result,invocations,next_effect,next_audio,next_script,renderer,fallback,settings,cleanup_boundary;
int city_load_failed(const City*c){return c&&c->failed;}
#include "scene_construction_stop.inc"
static int operation(void){invocations++;if(inject)owned.failed=1;return result;}
int streetobj_force_damage(StreetObjs*s,const char*n,const float p[3]){(void)s;(void)n;(void)p;return operation();}
int scene_adornments_hit(Scene*s,const float p[3],float r){(void)s;(void)p;(void)r;fallback++;assert(!owned.failed);return 0;}
static int set_reaction(Mon*m,int r){(void)m;(void)r;return operation();}
int vehicle_spawn(Vehicles*v,const char*n,const float p[3],float h,int b){(void)v;(void)n;(void)p;(void)h;(void)b;return operation();}
int army_spawn(Army*a,const char*n,const float p[3],float h){(void)a;(void)n;(void)p;(void)h;return operation();}
int city_camera(City*c,Group*g){(void)c;memset(g,0,sizeof *g);return operation();}
void cam_settings(CamSettings*c,Group g){(void)c;(void)g;settings++;assert(!owned.failed);}
void vehicle_update(Vehicles*v,float dt,const ArmyMonster*m,int nm,const ArmyBox*b,int nb){(void)v;(void)dt;(void)m;(void)nm;(void)b;(void)nb;operation();}
void parade_update(Parade*p,float dt,const ArmyMonster*m,int nm,float x){(void)p;(void)dt;(void)m;(void)nm;(void)x;if(!owned.failed)operation();}
#include "boss_force.inc"
#include "rail_reaction.inc"
#include "script_spawn.inc"
#include "script_unit.inc"
#include "camera.inc"
#include "free_camera.inc"
#include "next_frame.inc"
static void reset(Viewer*v){memset(v,0,sizeof *v);owned.failed=0;v->city=&owned;v->play=1;v->sobj=(StreetObjs*)1;v->veh=(Vehicles*)1;v->army=(Army*)1;v->parade=(Parade*)1;invocations=next_effect=next_audio=next_script=renderer=fallback=settings=cleanup_boundary=0;}
int main(void){static Viewer v;Mon*m=&v.mon[0];BossEvent be[2]={{0}};be[0].type=BE_FORCE_DAMAGE;be[1].type=BE_SOUND;RailEvent re[2]={{0}};re[0].type=RE_REACTION;re[1].type=RE_SOUND_STOP;
for(int ret=-2;ret<=1;ret++){reset(&v);inject=1;result=ret;boss_force_probe(&v,be,2);assert(owned.failed&&v.exit_game&&!v.play&&invocations==1&&!fallback&&!next_effect);}
for(int ret=-2;ret<=1;ret++){reset(&v);inject=0;result=ret;boss_force_probe(&v,be,2);assert(!owned.failed&&invocations==1&&fallback==(ret==-1)&&next_effect==1);}
for(int ret=0;ret<2;ret++){reset(&v);m->pending_slot=-1;inject=1;result=ret;rail_reaction_probe(&v,m,re,2);assert(owned.failed&&invocations==1&&!m->rail_knock&&!next_audio);reset(&v);m->pending_slot=-1;inject=0;result=ret;rail_reaction_probe(&v,m,re,2);assert(invocations==1&&m->rail_knock==ret&&next_audio==1);}
reset(&v);m->pending_slot=0;inject=1;rail_reaction_probe(&v,m,re,2);assert(!invocations&&!owned.failed&&next_audio==1);
for(int ret=-1;ret<2;ret++){reset(&v);inject=1;result=ret;script_spawn_probe(&v,m,"Truck");assert(owned.failed&&invocations==1&&!next_script);reset(&v);inject=1;result=ret;script_unit_probe(&v,m,"Soldier");assert(owned.failed&&invocations==1&&!next_script);}
reset(&v);inject=0;result=2;script_spawn_probe(&v,m,"Truck");script_unit_probe(&v,m,"Soldier");assert(invocations==2&&next_script==2);
for(int ret=-1;ret<=0;ret++){reset(&v);inject=1;result=ret;camera_probe(&v);assert(owned.failed&&invocations==1&&!settings&&!renderer);reset(&v);inject=0;result=ret;camera_probe(&v);assert(invocations==1&&settings==(ret==0)&&renderer==1);}
reset(&v);inject=1;free_camera_probe(v);assert(owned.failed&&invocations==1&&!next_audio&&!renderer);reset(&v);inject=0;free_camera_probe(v);assert(invocations==2&&next_audio==1&&renderer==1);
reset(&v);owned.failed=1;next_frame_probe(v);assert(cleanup_boundary==1&&!next_audio&&!next_script&&!renderer);reset(&v);next_frame_probe(v);assert(!cleanup_boundary&&next_audio==1&&next_script==1&&renderer==1);
puts("conditional consumer controls: failed positive/negative results stop queued effects; short circuit preserved; camera optional absence/healthy publication preserved; free-camera and next-frame audio suppressed");return 0;}
