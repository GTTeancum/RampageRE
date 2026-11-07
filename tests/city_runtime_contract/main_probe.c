/* Actual selected main source and actual Game/PRB consumers. The host's failure
 * is injected here; actual City allocation failure is separately exercised by
 * manager_probe.c. No renderer/audio device is replaced by a fake success claim. */
#include <assert.h>
#include "types.h"
#include "ledger.h"
struct City { int failed; };
static int profiles,huds,templates,swarms,effects,after_failure;
static City owned_city;
int city_load_failed(const City*c){return c&&c->failed;}
#include "scene_construction_stop.inc"
#include "world_load_failed.inc"
#include GAME_SOURCE

int profile_block_won(Profile*p,uint32_t flags,int total,int block,int destroyed){(void)p;(void)flags;(void)total;(void)block;(void)destroyed;profiles++;after_failure+=owned_city.failed;return 0;}
void hud_results(Hud*h,int screen,const HudResults*r){(void)h;(void)screen;(void)r;huds++;after_failure+=owned_city.failed;}
int reveal_was_revealed(const Reveal*r){(void)r;return 0;}
static void building_alarm(Viewer*v,SceneBuilding*sb){(void)v;(void)sb;}
static int fail_template;
Group city_template(City*c,const char*n){(void)n;templates++;if(fail_template){c->failed=1;return (Group){0};}return (Group){NULL,1};}
void people_add_swarm(People*p,Group g,const float pos[3]){(void)p;(void)g;(void)pos;swarms++;after_failure+=owned_city.failed;}
void scene_fx(Scene*s,const char*n,const float pos[3],const float dir[3]){(void)s;(void)n;(void)pos;(void)dir;effects++;after_failure+=owned_city.failed;}
#include "building_script.inc"
#include "results.inc"
const char*bdp_name(const Bundle*b,uint32_t n){return n<b->nnames?b->names[n]:"";}
static void u32(uint8_t*p,uint32_t v){for(int i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static int find_calls,inject;
static Group parent_find(void*ctx,const char*b,const char*n){Viewer*v=ctx;(void)b;(void)n;find_calls++;assert(!v->city->failed);if(inject)v->city->failed=1;return (Group){0};}
int main(void){
 static Viewer v;memset(&v,0,sizeof v);v.city=&owned_city;v.hud=(Hud*)(uintptr_t)1;v.prof.ncities=1;
 GameHost h={0};h.find=parent_find;h.ctx=&v;h.failed=world_load_failed;GameConfig cfg={0};cfg.play_type=GP_CAMPAIGN;v.game=game_new(&h,&cfg);assert(v.game);
 const char*names[]={"World","BlockData","BlockDef|A","ParentPropertySet","Parent:Present"};Bundle b={0};b.names=names;b.nnames=5;
 uint8_t d[512]={0};Book bk={d,&b};for(int k=0;k<3;k++){int a=64+64*k;u32(d+a+8,1);u32(d+a+16,20);u32(d+a+20,k==2?PK_NAME:PK_GROUP);u32(d+a+24,k+1);u32(d+a+28,k==2?4:64);}
 v.game->world=(Group){&bk,64};v.game->nblocks=1;v.game->blocks[0]="A";v.game->cur=0;GameEvent e={0};e.type=GE_RESULTS;e.a=GR_SCREEN_RESULTS;
 inject=1;results_probe(&v,&e);assert(owned_city.failed&&v.exit_game&&find_calls==1);assert(!profiles&&!huds&&!after_failure);
 /* A retry while failed must not enter even the first property callback. */
 results_probe(&v,&e);assert(find_calls==1&&!profiles&&!huds);
 game_free(v.game);v.game=NULL;assert(!ledger_live&&!ledger_bytes);memset(&v.scene,0,sizeof v.scene);owned_city.failed=0;v.exit_game=0;
 h.ctx=&v;v.game=game_new(&h,&cfg);v.game->world=(Group){&bk,64};v.game->nblocks=1;v.game->blocks[0]="A";v.game->cur=0;inject=0;
 results_probe(&v,&e);assert(!owned_city.failed&&profiles==1&&huds==1&&!after_failure);game_free(v.game);v.game=NULL;
 SceneBuilding sb={0};sb.school=1;sb.chicken=1;sb.swarm="Swarm";v.scene.folk=(People*)(uintptr_t)1;fail_template=1;
 building_script(&v,&sb,3);assert(owned_city.failed&&v.exit_game&&templates==1&&!swarms&&!effects&&!after_failure);
 building_script(&v,&sb,3);assert(templates==1&&!swarms&&!effects);
 memset(&v.scene,0,sizeof v.scene);owned_city.failed=0;v.exit_game=0;v.scene.folk=(People*)(uintptr_t)1;memset(&sb,0,sizeof sb);sb.school=1;sb.chicken=1;sb.swarm="Swarm";fail_template=0;
 building_script(&v,&sb,3);assert(!owned_city.failed&&swarms==1&&effects==1&&!after_failure);assert(!ledger_live&&!ledger_bytes);
 puts("actual GE_RESULTS parent failure: profile=0 HUD=0; actual building script failure: swarm=0 effect=0; teardown/recovery controls pass; owners=0");return 0;
}
