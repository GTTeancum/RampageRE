/* Exact open_city, stop helper, direct-start and final-status code are extracted.
 * These dependency doubles model City admission/staging, not parser internals.
 * Existing City tokens remain borrowed; only an unpublished new token is closed. */
#include <assert.h>
#include "main-types.h"
#include "city-status.inc"
enum { MISSING, OPEN_FATAL, ROSTER_FATAL, BLOCKS_FATAL, GAME_BLOCK_FATAL };
static City old_city, pending_city;
static Viewer v, before;
static int renderer_token, autoplay_token;
static int mode, old_alive, new_alive, opened, closed, active_calls;
static int book_calls, roster_calls, block_calls, game_block_calls;
static int renderer_calls, detached_calls, reattached_calls;
static int build_calls, release_calls, scene_free_calls, messages, ap_calls, ap_result;
static City *active;
static const Scene *renderer_scene;
City *city_open_checked(const Vol *vol,const char *name,int *failed){
 assert(vol==&v.vol&&!strcmp(name,CITIES[2])&&failed);opened++;*failed=0;
 if(mode==MISSING)return NULL;
 if(mode==OPEN_FATAL){*failed=1;return NULL;}
 assert(!new_alive);new_alive=1;return &pending_city;
}
void city_activate(City*c){active_calls++;active=c;assert(c==v.city);}
void city_close(City*c){assert(c==&pending_city&&new_alive);new_alive=0;closed++;}
Group city_book_group(City*c,const char*book,const char*name){
 assert(c==&pending_city&&new_alive);assert(!strcmp(book,"ShellData")&&!strcmp(name,"MonsterData"));book_calls++;
 if(mode==ROSTER_FATAL)c->load_failed=1;return (Group){0};
}
int reveal_roster_load(RevealRoster*r,Group g){
 (void)g;assert(r!=&v.roster&&r->n==1);roster_calls++;r->n=3;r->name[0]="new staged roster";return r->n;
}
uint32_t city_blocks(City*c,const char**names,uint32_t max){
 assert(c==&pending_city&&new_alive&&!c->load_failed&&max==64);block_calls++;names[0]="new staged block";
 if(mode==BLOCKS_FATAL)c->load_failed=1;return 1;
}
const char *city_game_block(City*c,int block){
 assert(c==&pending_city&&new_alive&&!c->load_failed&&block==7);game_block_calls++;
 assert(mode==GAME_BLOCK_FATAL);c->load_failed=1;return "new staged block";
}
void rnd_scene(Renderer*r,const Scene*s){
 assert(r==(Renderer*)&renderer_token);renderer_calls++;if(s){assert(s==&v.scene);reattached_calls++;}else detached_calls++;
 renderer_scene=s;
}
static void world_release(Viewer*p){(void)p;release_calls++;assert(!"old world released during failed staging");}
void scene_free(Scene*s){(void)s;scene_free_calls++;assert(!"old scene freed during failed staging");}
static void build(Viewer*p){(void)p;build_calls++;assert(!"failed metadata published to build");}
void plat_message(const char*s,int warning){assert(warning&&s);messages++;}
int ap_close(Autoplay*a,const char*why){assert(a==(Autoplay*)&autoplay_token&&!why);ap_calls++;return ap_result;}
#include "scene_construction_stop.inc"
#include "checked-open.inc"
#include "direct-start.inc"
#include "final-status.inc"
static void reset(int existing){
 assert(!new_alive);memset(&v,0,sizeof v);memset(&old_city,0,sizeof old_city);memset(&pending_city,0,sizeof pending_city);
 opened=closed=active_calls=book_calls=roster_calls=block_calls=game_block_calls=0;
 renderer_calls=detached_calls=reattached_calls=build_calls=release_calls=scene_free_calls=messages=ap_calls=0;
 old_alive=existing;v.city=existing?&old_city:NULL;v.have_scene=existing;v.scene.owner=v.city;
 v.city_index=4;v.block=1;v.nblocks=2;v.blocks[0]="old block zero";v.blocks[1]="old block one";
 v.roster.n=1;v.roster.name[0]="old roster";v.roster.flags[0]=37;v.play=1;v.ny_generators=11;v.band_sound=13;
 v.rnd=(Renderer*)&renderer_token;active=v.city;renderer_scene=existing?&v.scene:NULL;before=v;
}
int main(void){
 assert_city_status_contract();int cases=0;
 for(int existing=0;existing<2;existing++)for(mode=MISSING;mode<=GAME_BLOCK_FATAL;mode++){
  reset(existing);int result=open_city(&v,2,5,7);assert(result==-1&&opened==1);
  assert(old_alive==existing&&!new_alive&&v.city==before.city&&active==before.city&&active_calls==1);
  assert(!old_city.load_failed&&!build_calls&&!release_calls&&!scene_free_calls);
  assert(closed==(mode>=ROSTER_FATAL));assert(book_calls==(mode>=ROSTER_FATAL));assert(roster_calls==book_calls);
  assert(block_calls==(mode>=BLOCKS_FATAL));assert(game_block_calls==(mode==GAME_BLOCK_FATAL));
  Viewer expected=before;
  if(mode!=MISSING){expected.scene.construction_failed=1;expected.play=0;expected.exit_game=1;}
  assert(!memcmp(&v,&expected,sizeof v)); /* no staged metadata or old owners published/changed */
#ifdef RAMPAGE_LOW_MEMORY
  assert(renderer_calls==(mode==MISSING?2:1));
  assert(reattached_calls==(mode==MISSING&&existing));
  assert(detached_calls==renderer_calls-reattached_calls);
  assert(renderer_scene==(mode==MISSING&&existing?&v.scene:NULL));
#else
  assert(!renderer_calls&&!reattached_calls&&!detached_calls);
  assert(renderer_scene==(existing?&v.scene:NULL));
#endif
  if(!existing){
   assert(direct_start_check(result,v.exit_game)==1);
   assert(messages==(mode==MISSING)); /* absence reports missing media; fatal does not */
   if(mode!=MISSING){v.ap=(Autoplay*)&autoplay_token;ap_result=3;assert(final_status(&v,1)==1&&ap_calls==1);}
  }
  old_alive=0;cases++; /* explicit test-token disposal; actual City destructor is not modeled */
 }
 memset(&v,0,sizeof v);ap_calls=0;assert(final_status(&v,0)==0&&!ap_calls);
 v.ap=(Autoplay*)&autoplay_token;ap_result=3;assert(final_status(&v,0)==3&&ap_calls==1);
 assert(direct_start_check(0,0)==0);
 printf("PASS checked open: %d missing/fatal/staged failures, old owners preserved, no premature publication, fatal startup status 1\n",cases);
 return 0;
}
