#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ledger.h"
#include CITY_SOURCE
#include "fixture.h"
static void vol_cleanup(Vol*v){if(v->f)fclose(v->f);free(v->files);free(v->names);for(uint32_t i=0;i<v->nwii;i++)free((void*)v->wii[i].path);free(v->wii);memset(v,0,sizeof*v);}
static uint64_t hb(uint64_t h,const void*p,size_t n){const unsigned char*q=p;while(n--)h=(h^*q++)*UINT64_C(1099511628211);return h;}
static uint64_t mesh_hash(const Scene*s){uint64_t h=UINT64_C(14695981039346656037);for(uint32_t i=0;i<s->nmeshes;i++){const Mesh*m=&s->meshes[i].mesh;h=hb(h,&m->nparts,sizeof m->nparts);h=hb(h,&m->lead,sizeof m->lead);for(uint32_t j=0;j<m->nparts;j++){const MeshPart*p=&m->parts[j];uint32_t meta[]={p->nverts,p->ntris,p->material,p->blend,p->rev};h=hb(h,meta,sizeof meta);const void*arrays[]={p->pos,p->nrm,p->uv,p->col,p->tris,p->bones,p->weights};size_t sizes[]={12u*p->nverts,12u*p->nverts,8u*p->nverts,16u*p->nverts,12u*p->ntris,sizeof *p->bones*p->nverts,sizeof *p->weights*p->nverts};for(int k=0;k<7;k++){uint8_t present=arrays[k]!=NULL;h=hb(h,&present,1);if(present)h=hb(h,arrays[k],sizes[k]);}}}return h;}

extern int test_sound_starts;
static City *late_city;
static int late_hits;
int __real_people_crowd(People*,const Group*,int,const float[3]);
int __wrap_people_crowd(People*p,const Group*t,int n,const float at[3]){
 int result=__real_people_crowd(p,t,n,at);
 if(late_city){City*c=late_city;late_city=NULL;late_hits++;ledger_fail_read_name="camera.bdp";assert(!city_book_group(c,"camera","NoSuchGroup").at);assert(city_load_failed(c));}
 return result;
}
static void seed(void){wrand_seed_pm(0x2545F491u);wrand_seed_lf(12345);srand(1);}
static void zero_scene(const Scene*s){const unsigned char*p=(const unsigned char*)s;for(size_t i=0;i<sizeof*s;i++)assert(!p[i]);}
static void runtime_contract(void){
 uint8_t raw[1024];FILE*f=tmpfile();assert(f);fixture(raw,0,0,1);assert(fwrite(raw,1,1024,f)==1024);fixture(raw,0,0,0);assert(fwrite(raw,1,1024,f)==1024);VolFile rows[2]={{0}};rows[0].name="Child.bdp";rows[0].size=1024;rows[1].name="Present.bdp";rows[1].size=1024;rows[1].offset=1024;Vol v={0};v.f=f;v.files=rows;v.count=2;
 for(int mode=0;mode<8;mode++){
  City*c=fresh(&v);city_activate(c);LoadedBook*child=load_book(c,"Child");assert(child);Group g=book_group(&child->book,"Root");Scene s={0};s.owner=c;s.city=&c->bundle;
  s.instances=calloc(1,sizeof*s.instances);s.ninstances=1;s.instances[0].base=mat_identity();
  if(mode==1){s.buildings=calloc(1,sizeof*s.buildings);s.nbuildings=1;s.buildings[0].props=g;s.occ_setup=1;}
  if(mode>=3&&mode<=5){s.folk=people_new(people_clip_seconds,c);assert(s.folk);people_set_failure(s.folk,people_city_failed,c);s.nscripted=1;s.scripted[0].hut=1;s.scripted[0].instance=0;s.scripted[0].t=g;s.scripted[0].hut_id=people_add_hut(s.folk,(Group){0},(float[3]){0});assert(s.scripted[0].hut_id==0);if(mode==4)people_hut_hit(s.folk,0,(float[3]){1,0,1},s.scripted[0].vel);}
  if(mode==6){ArmyHost host={0};host.ctx=c;host.failed=people_city_failed;s.army=army_new(&host);assert(s.army);s.buildings=calloc(1,sizeof*s.buildings);s.nbuildings=1;SceneBuilding*sb=s.buildings;sb->fronts_told=1;sb->props=g;sb->hitter=0;sb->mesh=-1;sb->b.collapse=-1;sb->b.nscore=1;sb->b.score[0].event=BSCORE_CELL;sb->b.score[0].state=2;sb->b.pre_collapse=sb->b.drop_dust=1;sb->shade_nb[0]=sb->shade_nb[1]=-1;}
  if(mode==7){s.folk=people_new(people_clip_seconds,c);assert(s.folk);people_set_failure(s.folk,people_city_failed,c);s.slots=calloc(2,sizeof*s.slots);s.nslots=2;s.order=calloc(2,sizeof*s.order);s.norder=s.order_civ=2;for(int k=0;k<2;k++){int who=people_add_window(s.folk,(Group){0},(float[3]){0},0,0,k+1);assert(who==k);Person*q=(Person*)people_get(s.folk,who);q->tmpl=g;q->kind=PK_WINDOW;q->phase=PP_SCRIPT;s.slots[k].column=k+1;s.order[k]=k;}}
  ledger_fail_read_name="Present.bdp";size_t hits=ledger_fail_hits;
  if(mode==0){Person person={0};person.tmpl=g;strcpy(person.anim,"Look");assert(person_look(c,&s,0,&person)==-2);assert(!s.nlooks);}
  if(mode==1){scene_occupant_slots(&s,0);assert(!s.norder&&!s.nslots);}
  if(mode==2){RailObj o={0};o.uid=1;o.tmpl=g;rail_flags(&s,&o,1);assert(!s.nrail_flag);}
  if(mode==3){Group hit={0};assert(!scene_huts_hit(&s,(float[3]){0},10,&hit,1));assert(!hit.at&&people_hut_state(s.folk,0)==0);}
  if(mode==4){scene_huts_update(&s,1.0f/30);assert(!s.scripted[0].cm_on&&!s.instances[0].dynamic);}
  if(mode==5){scene_huts_collide_add(&s,(CollideWorld*)1);assert(s.scripted[0].cobj==-1);}
  if(mode==6){scene_buildings_update(&s,0);assert(s.buildings[0].b.nscore==1&&s.buildings[0].b.pre_collapse==1&&!s.nfxreq);}
  if(mode==7){scene_cell_broken(&s,0,0,0,2);assert(s.slots[0].retired&&!s.slots[1].retired);}
  assert(ledger_fail_hits==hits+1&&city_load_failed(c)&&scene_failed(&s));assert(s.instances);size_t calls=ledger_calls;scene_huts_update(&s,1);scene_people_spawn(&s,1);assert(calls==ledger_calls);scene_fx(&s,"must_not_publish",(float[3]){0},(float[3]){0});assert(!s.nfxreq);if(s.army){army_free(s.army);s.army=NULL;}scene_free(&s);zero_scene(&s);city_reset_load_failure(c);assert(group_num(g,"Answer",-1)==42&&child->book.b==&child->b);city_close(c);assert(!ledger_bytes&&!ledger_live);
  printf("runtime_boundary=%d actual_parent_failure=1 no_later_effect=1 no_free_under_callback=1 actual_teardown_zero=1\n",mode);
 }
 fclose(f);
}
int main(int argc,char**argv){assert(argc==4);setvbuf(stdout,NULL,_IONBF,0);runtime_contract();Vol v;assert(!vol_open(&v,argv[1]));size_t base=ledger_bytes,live=ledger_live;uint64_t healthy=0;int healthy_crowd=0,healthy_people=0;
 for(int mode=0;mode<3;mode++){
  City*c=city_open(&v,argv[2]);assert(c&&!city_load_failed(c));city_new_world(c);const char*block=city_game_block(c,0);assert(block);Scene s;seed();test_sound_starts=0;
  int saved_nrunners=c->nrunners,saved_crowd=c->create_crowd;Group saved_runners[16];memcpy(saved_runners,c->runners,sizeof saved_runners);
  if(mode==1)ledger_fail_read_name=argv[3];if(mode==2)late_city=c;size_t hits=ledger_fail_hits;int r=city_build(c,block,0,&s);
  if(!mode){assert(!r&&!scene_failed(&s));healthy=mesh_hash(&s);healthy_crowd=test_sound_starts;healthy_people=s.people;assert(healthy_crowd==1);printf("healthy city=%s block=%s hash=%016llx meshes=%u buildings=%u people=%d sounds=%d\n",argv[2],block,(unsigned long long)healthy,s.nmeshes,s.nbuildings,s.people,test_sound_starts);scene_free(&s);zero_scene(&s);}
  else{
   assert(r==-1&&city_load_failed(c)&&ledger_fail_hits==hits+1);zero_scene(&s);assert(c->nrunners==saved_nrunners&&c->create_crowd==saved_crowd&&!memcmp(c->runners,saved_runners,sizeof saved_runners));assert(!test_sound_starts);if(mode==2)assert(late_hits==1);
   size_t reads=ledger_read_calls,allocs=ledger_calls;city_activate(c);city_new_world(c);assert(city_load_failed(c));assert(city_build(c,block,0,&s)==-1);zero_scene(&s);assert(reads==ledger_read_calls&&allocs==ledger_calls);
   /* Actual constructor already freed partial Scene; repeat real destructor is harmless.
    * No managers survive this point, so only now may the caller reset the City attempt. */
   scene_free(&s);city_reset_load_failure(c);seed();test_sound_starts=0;assert(!city_build(c,block,0,&s)&&!scene_failed(&s));assert(mesh_hash(&s)==healthy&&s.people==healthy_people&&test_sound_starts==healthy_crowd);scene_free(&s);zero_scene(&s);
   /* Successful Books survive explicit attempt resets, but the old Scene never does. */
   for(int repeat=0;repeat<2;repeat++){city_reset_load_failure(c);city_new_world(c);seed();test_sound_starts=0;assert(!city_build(c,block,0,&s));assert(mesh_hash(&s)==healthy&&s.people==healthy_people);scene_free(&s);zero_scene(&s);}
   printf("failure mode=%s real_scene_freed=1 crowd_runners_rollback=1 no_early_sound=1 preexisting_status_rejected=1 explicit_same_city_recovery=1 hash=%016llx\n",mode==1?"early-book":"late-after-real-crowd",(unsigned long long)healthy);
  }
  city_close(c);assert(!active_city&&ledger_bytes==base&&ledger_live==live);
 }
 /* An open failure after resolver installation restores the previous live City. */
 City*prior=city_open(&v,argv[2]);assert(prior&&active_city==prior);size_t before=ledger_bytes,owners=ledger_live;char props[96];snprintf(props,96,"%s_Props.bdp",argv[2]);ledger_fail_read_name=props;assert(!city_open(&v,argv[2]));assert(active_city==prior&&ledger_bytes==before&&ledger_live==owners);assert(city_game_block(prior,0));city_close(prior);
 vol_cleanup(&v);ledger_report("all_closed");assert(!ledger_live&&!ledger_bytes);return 0;
}
