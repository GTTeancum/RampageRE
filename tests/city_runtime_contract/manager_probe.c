#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ledger.h"
#include "game.h"
#include "reveal.h"
#include "streetobj.h"
#ifndef CITY_SOURCE
#error CITY_SOURCE must identify a pinned City source
#endif
#include CITY_SOURCE

static void u32(uint8_t *p,uint32_t v) { for(int i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i)); }
/* A valid, empty property book. The fixture's external child inherits absent
 * optional properties through its present parent. Healthy defaults are exact. */
static void parent_file(uint8_t f[1024]) {
 memset(f,0,1024);memcpy(f,"Pipeworks Bundle File version 1.3",33);
 u32(f+0x30,1300);u32(f+0x34,0x80);u32(f+0x38,0x180);
 u32(f+0x80,1);u32(f+0x84,8);strcpy((char*)f+0x88,"Parent");
 f[0x182]=1;u32(f+0x184,0x240);u32(f+0x188,0x280);u32(f+0x19e,128);
 f[0x242]=ET_BOOK;uint8_t*d=f+0x280;u32(d+0x1c,1);u32(d+0x24,0x30);u32(d+0x30,0x40);u32(d+0x50,0x14);
}
typedef struct {
 City *city; Group root; int callbacks, after_failure, shared;
 uint8_t d[512]; const char *names[7]; Bundle b; Book book;
} Context;
static int stopped(void *v) { return city_load_failed(((Context*)v)->city); }
static void called(Context*c) { c->callbacks++; if(stopped(c))c->after_failure++; }
static Group find(void *v,const char*b,const char*n) { Context*c=v;(void)b;(void)n;called(c);return c->root; }
static Group tmpl(void*v,const char*n) { return find(v,"Objects1_Props",n); }
static float clip(void*v,const char*a,const char*s) { called(v);(void)a;(void)s;return 1; }
static int box(void*v,const char*s,float a[3],float b[3]) {called(v);(void)s;for(int i=0;i<3;i++){a[i]=-1;b[i]=1;}return 1;}
static int sphere(void*v,const char*s,float a[3],float*r) {called(v);(void)s;memset(a,0,12);*r=1;return 1;}
static int cell(void*v,int b,int s,int x){called(v);(void)b;(void)s;(void)x;return 0;}
static int place(void*v,int b,int s,int x,float out[3]){called(v);(void)b;(void)s;(void)x;memset(out,0,12);return 1;}
static int all(void*v){called(v);return 0;}
static void block_rubble(void*v,const char*n){called(v);(void)n;}
static int point(void*v,const float p[3],float r,float out[3]){called(v);(void)r;memcpy(out,p,12);return 1;}
static int weapon(void*v,ArmyWeapon*w,Group g){called(v);(void)w;(void)g;return 0;}
static void drop(void*v,const char*n,const float p[3]){called(v);(void)n;(void)p;}
static int visible(void*v,const float p[3],float r){called(v);(void)p;(void)r;return 1;}

static void child(Context*c,const char*script,int weapon_list) {
 memset(c->d,0,sizeof c->d);c->names[0]="Test";c->names[1]="ParentPropertySet";c->names[2]="Parent:Present";
 c->names[3]="Script|ScriptName";c->names[4]=script;c->names[5]="Weapon|A";c->names[6]="WeaponName";
 c->b.names=c->names;c->b.nnames=7;c->book=(Book){c->d,&c->b};c->root=(Group){&c->book,0x40};
 u32(c->d+0x48,weapon_list?1:2);u32(c->d+0x50,0x14);
 uint8_t*r=c->d+0x54;
 if(weapon_list){u32(r,PK_GROUP);u32(r+4,5);u32(r+8,0x80);u32(c->d+0xc8,1);u32(c->d+0xd0,0x14);r=c->d+0xd4;}
 u32(r,PK_NAME);u32(r+4,1);u32(r+8,2);
 if(!weapon_list){r+=12;u32(r,PK_NAME);u32(r+4,3);u32(r+8,4);}
}
static int exercise(Context*c,int family) {
 float p[3]={0},m[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};int events=0;
 switch(family) {
 case 0:{ArmyHost h={0};h.find=find;h.clip_seconds=clip;h.shape_box=box;h.shape_sphere=sphere;h.visible=visible;h.ctx=c;h.failed=stopped;
  Army*a=army_new(&h);assert(a);army_spawn(a,"Test",p,0);if(stopped(c))assert(army_drop(a,"Test",p)==-1);ArmyEvent e[32];events=army_events(a,e,32);army_update(a,0.1f,NULL,0,NULL,0);assert(!c->after_failure);army_free(a);break;}
 case 1:{VehicleHost h={0};h.find=find;h.shape_box=box;h.shape_sphere=sphere;h.ctx=c;h.failed=stopped;
  Vehicles*a=vehicle_new(&h);assert(a);vehicle_spawn(a,"Test",p,0,-1);if(stopped(c))assert(vehicle_kind_of(a,"Test")==-1);VehicleEvent e[32];events=vehicle_events(a,e,32);vehicle_update(a,.1f,NULL,0,NULL,0);vehicle_free(a);break;}
 case 2:{RailHost h={0};h.find=find;h.shape_box=box;h.shape_sphere=sphere;h.ctx=c;h.failed=stopped;
  Rail*a=rail_new(&h);assert(a);rail_spawn(a,"Test",p,0,(Group){0});if(stopped(c))assert(rail_kind_of(a,"Test")==-1);RailEvent e[32];events=rail_events(a,e,32);rail_update(a,.1f,NULL,0,NULL,0);rail_free(a);break;}
 case 3:{ParadeHost h={0};h.find=find;h.shape_box=box;h.shape_sphere=sphere;h.clip_seconds=clip;h.ctx=c;h.failed=stopped;
  Parade*a=parade_new(&h);assert(a);parade_spawn(a,"Test",p,0);ParadeEvent e[32];events=parade_events(a,e,32);parade_update(a,.1f,NULL,0,0);parade_free(a);break;}
 case 4:{BossHost h={0};h.find=find;h.shape_box=box;h.shape_sphere=sphere;h.clip_seconds=clip;h.weapon_make=weapon;h.generators=&c->shared;h.ctx=c;h.failed=stopped;
  Boss*a=boss_new(&h);assert(a);boss_spawn(a,"Test",(Group){0},p);BossEvent e[32];events=boss_events(a,e,32);boss_update(a,.1f,NULL,0,NULL,0);boss_free(a);break;}
 case 5:{StreetHost h={0};h.find=tmpl;h.shape_box=box;h.shape_sphere=sphere;h.ctx=c;h.failed=stopped;
  StreetObjs*a=streetobj_new(&h);assert(a);streetobj_add(a,c->root,(Group){0},m,-1,0);StreetEvent e[32];events=streetobj_events(a,e,32);streetobj_update(a,.1f,NULL,0,NULL,0);streetobj_free(a);break;}
 case 6:{People*a=people_new(clip,c);assert(a);people_set_failure(a,stopped,c);people_set_templates(a,tmpl,c);people_set_drop(a,drop,c);people_set_visible(a,visible,c);people_set_band_sound(a,&c->shared);
  people_add(a,c->root,p,0);PeopleEvent e[32];events=people_events(a,e,32);people_update(a,.1f,NULL,0);people_free(a);break;}
 case 7:{EdibleHost h={0};h.find=find;h.cell_state=cell;h.cell_place=place;h.cell_follow=place;h.ctx=c;h.failed=stopped;
  Edibles*a=edible_new(&h);assert(a);edible_load_block(a,c->root);EdibleEvent e[32];events=edible_events(a,e,32);edible_free(a);break;}
 case 8:{GameHost h={0};h.find=find;h.all_revealed=all;h.block_rubble=block_rubble;h.rubble_point=point;h.ctx=c;h.failed=stopped;GameConfig cfg={0};
  Game*a=game_new(&h,&cfg);assert(a);game_load_city(a,"Test");GameEvent e[32];events=game_events(a,e,32);game_update(a,.1f,NULL);game_free(a);assert(game_events(NULL,NULL,0)==0);break;}
 case 9:{Rubble a={0};RubbleHost h={0};h.tmpl=tmpl;h.shape_box=box;h.ctx=c;h.failed=stopped;rubble_load(&a,c->root,1,&h);break;}
 case 10:{ArmyMonWeapons w={0};army_mon_weapons_read_checked(&w,c->root,find,c,stopped);break;}
 }
 return events;
}
#include "entry_sentinel_tests.inc"
int main(void) {
 static const char*scripts[]={"Vehicle_Attack.bsf","Vehicle.bsf","Train.bsf","Biker.bsf","Boss_NY_Generator.bsf","Fountain.bsf","BandMember.bsf","", "", "", ""};
 setvbuf(stdout,NULL,_IONBF,0);
 uint8_t raw[1024];parent_file(raw);FILE*f=tmpfile();assert(f&&fwrite(raw,1,1024,f)==1024);fflush(f);
 VolFile vf={0};vf.name="Present.bdp";vf.size=1024;Vol v={0};v.f=f;v.files=&vf;v.count=1;
 for(int i=0;i<11;i++) {
  Context c={0};c.city=calloc(1,sizeof *c.city);assert(c.city);c.city->vol=&v;strcpy(c.city->name,"Test");city_activate(c.city);child(&c,scripts[i],i==10);
  size_t base=ledger_bytes,owners=ledger_live;size_t hits=ledger_fail_hits;ledger_fail_read_name="Present.bdp";
  int events=exercise(&c,i);assert(stopped(&c));assert(ledger_fail_hits==hits+1);assert(!c.after_failure&&!events);assert(ledger_live==owners&&ledger_bytes==base);assert(!c.shared);
  int callbacks=c.callbacks;exercise(&c,i);assert(c.callbacks==callbacks);assert(ledger_live==owners&&ledger_bytes==base);
  city_reset_load_failure(c.city);assert(!stopped(&c));c.callbacks=0;events=exercise(&c,i);assert(!stopped(&c)&&!c.after_failure);
  if(i<=4||i==8) assert(c.callbacks>0);
  assert(events==((i==4||i==8)?1:0));
  city_activate(NULL);city_close(c.city);assert(!ledger_bytes&&!ledger_live);
  printf("family=%d injected_nested_allocation=1 later_callbacks=0 events_after_failure=0 owners_after_teardown=0 same_city_recovery=1 healthy_events=%d healthy_callbacks=%d\n",i,events,c.callbacks);
 }
 test_entry_sentinels(&v);
 fclose(f);ledger_report("closed");return 0;
}
