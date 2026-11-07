/* Actual city translation unit, including its private geometry helpers and scene_free. */
#include "alloc_probe.h"
#include "city.c"
#include <assert.h>
void audit_fail_site(int line,int nth);
int audit_site_hits(void);
static void close_vol(Vol*v){if(v->f)fclose(v->f);free(v->names);free(v->files);}
static SceneBuilding *with_mesh(Scene*s){for(uint32_t i=0;i<s->nbuildings;i++)if(s->buildings[i].mesh>=0)return &s->buildings[i];return NULL;}
static void trigger(Scene*s,const char*kind){
 if(!strcmp(kind,"order")){scene_occupant_pad(s,3);return;}
 if(!strcmp(kind,"slots")){for(uint32_t i=0;i<s->nbuildings&&!s->construction_failed;i++)scene_occupant_slots(s,(int)i);return;}
 SceneBuilding*b=with_mesh(s);assert(b);
 if(!strcmp(kind,"facade")){facade_passes(s,b);return;}
 if(!strcmp(kind,"windows")){open_windows(s,b);return;}
 if(!strcmp(kind,"boxes")){obstacle_boxes(s);return;}
 assert(0);
}
int main(int argc,char**argv){
 assert(argc==6);setvbuf(stdout,NULL,_IONBF,0);const char*kind=argv[2];int line=atoi(argv[3]),nth=atoi(argv[4]);
 Vol v;assert(!vol_open(&v,argv[1]));size_t baseline=audit_bytes(),base_live=audit_live();City*c=city_open(&v,"LasVegas");assert(c);city_new_world(c);
 Scene warm;assert(!city_build(c,"LV01_Casino1",0,&warm));scene_free(&warm);city_activate(NULL);size_t warm_bytes=audit_bytes(),warm_live=audit_live();
 for(int round=0;round<2;round++){
  Scene s; if(!strcmp(kind,"build"))audit_fail_site(round?0:line,nth);
  int result=city_build(c,"LV01_Casino1",0,&s);printf("round=%d city_build=%d buildings=%u obstacles=%u\n",round,result,s.nbuildings,s.nobstacles);
  if(!strcmp(kind,"build")&&round==0){assert(result!=0);assert(audit_site_hits()==1);Scene zero={0};assert(!memcmp(&s,&zero,sizeof s));}
  else {
   assert(!result);if(strcmp(kind,"build")){
    if(!strcmp(kind,"order"))scene_occupant_pad(&s,2);
    if(!strcmp(kind,"slots")){trigger(&s,"slots");printf("actual occupant slots=%d\n",s.nslots);assert(s.nslots>0);}
    if(!strcmp(kind,"windows")){SceneBuilding*b=with_mesh(&s);assert(b);assert(b->b.storeys>1);b->b.nwindows=1;b->b.window[0]=(BuildingWindow){0};b->b.window[0].o=1;b->b.window[0].w=2;b->b.window[0].h=1;b->b.window[0].open=0.5f;}
    audit_fail_site(round?0:line,nth);trigger(&s,kind);assert(s.construction_failed==(round==0));assert(audit_site_hits()==(round==0));
    if(round==0){size_t calls=audit_calls();trigger(&s,kind);assert(audit_calls()==calls);}
   }
  }
  if(round==0)assert(audit_site_hits()==1);audit_fail_site(0,0);scene_free(&s);Scene zero={0};assert(!memcmp(&s,&zero,sizeof s));city_activate(NULL);
  assert(audit_bytes()==warm_bytes&&audit_live()==warm_live);audit_report(round?"recovery_scene_closed":"failed_scene_closed");
 }
 city_close(c);city_activate(NULL);assert(audit_bytes()==baseline&&audit_live()==base_live);audit_report("city_closed");close_vol(&v);assert(!audit_bytes()&&!audit_live());audit_report("all_reachable_closed");puts("PASS injected actual-source owner teardown and clean recovery");return 0;
}
