from pathlib import Path
import hashlib,json,re,sys
root=Path(sys.argv[2]); s=(Path(sys.argv[1])/'main.c').read_text()
checks={}
# Added status checks may precede the existing stop guard, but no other work.
extra = r'(?:if \(scene_construction_stop\(\(Viewer \*\)(?:v|ctx)\)\) return(?: 0| NULL)?;\s*)?'
def has(label,pat):
 if label not in ('city_build_left_guard','army_update_exit'):
  pat=pat.replace(r';\s*', r';\s*'+extra)
 checks[label]=bool(re.search(pat,s,re.S)); assert checks[label],label
has('city_build_failure_exits_all_configs',r'have_scene = city_build\([^;]+;\s*if \(!v->have_scene\).*?v->exit_game = 1;.*?return;')
has('city_build_left_guard',r'city_build_left\([^;]+;\s*if \(scene_construction_stop\(\(Viewer \*\)v\)\)\s*return;')
assert s.count('int added = city_monster(')==2
assert s.count('if (scene_construction_stop(v))\n        return -1;')>=1
has('monster_build_guard',r'int added = city_monster\([^;]+;\s*if \(scene_construction_stop\(v\)\)\s*return;')
has('street_shape_guard',r'case SE_SHAPE:\s*scene_street_shape\([^;]+;\s*if \(scene_construction_stop\(v\)\)\s*return;')
has('street_play_chain',r'street_step\(v, dt\);\s*if \(scene_construction_stop\(v\) \|\| v->exit_game\)\s*return;\s*army_step')
has('army_callback_exit',r'scene_unit_node_now\([^;]+;\s*if \(scene_construction_stop\(v\)\)\s*return 0;')
has('army_update_exit',r'army_update\([^;]+;\s*if \(scene_construction_stop\(\(Viewer \*\)v\)\)\s*return;\s*vehicle_step')
has('script_update_before_hit',r'scene_update\(&v->scene, 0\);\s*if \(scene_construction_stop\(v\)\)\s*return [^;]+;\s*hit_spheres_all')
has('viewer_update_before_hit',r'scene_update\(&v.scene, t\);\s*if \(scene_construction_stop\(&v\)\)\s*continue;\s*if \(playing\)\s*hit_spheres_all')
has('viewer_exit_before_hud',r'while \(!w.quit\) \{\s*if \(!v.shell && \(scene_construction_stop\(&v\) \|\| v.exit_game\)\)\s*goto leave_world;[^\n]*\n#ifdef RAMPAGE_XBOX')
# Extract exact stop helper and callback into executable test with dependency doubles.
def function(name):
 m=re.search(r'^static [^\n]+\b'+name+r'\([^;]*?\)\n\{',s,re.M);assert m
 i=m.end();d=1
 while d:
  d+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[m.start():i]
prefix='''#include <assert.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include "city.h"\n#include "city-status.inc"\ntypedef struct { City *city; Scene scene; Army *army; int play, exit_game; } Viewer;
static ArmyUnit unit;
static int old_calls,new_calls,mode;
const ArmyUnit *army_unit(const Army *a,int i){(void)a;(void)i;return &unit;}
int scene_unit_node(const Scene *s,int i,const char *n,float out[16]){(void)s;(void)i;(void)n;(void)out;old_calls++;return mode==1?0:-1;}
int scene_unit_node_now(Scene *s,int i,const ArmyUnit *u,const char *n,float out[16]){(void)i;(void)u;(void)n;(void)out;new_calls++;if(mode==2){s->construction_failed=1;return -2;}return mode==3?0:-1;}
'''
main='''int main(void){assert_city_status_contract();Viewer v={0};float out[16]={0};v.army=(Army*)&unit;v.play=1;
mode=0;assert(!army_node_world(&v,&unit,"node",out));assert(old_calls==1&&new_calls==1&&!v.exit_game);
mode=1;assert(army_node_world(&v,&unit,"node",out));assert(old_calls==2&&new_calls==1);
mode=3;assert(army_node_world(&v,&unit,"node",out));assert(old_calls==3&&new_calls==2);
mode=2;assert(!army_node_world(&v,&unit,"node",out));assert(v.exit_game&&!v.play&&v.scene.construction_failed);assert(old_calls==4&&new_calls==3);
assert(!army_node_world(&v,&unit,"node",out));assert(old_calls==4&&new_calls==3);
City owner={0};Viewer poisoned={0};poisoned.city=&owner;poisoned.play=1;owner.load_failed=1;
assert(!army_node_world(&poisoned,&unit,"node",out));assert(poisoned.exit_game&&!poisoned.play&&poisoned.scene.construction_failed);assert(old_calls==4&&new_calls==3);puts("exact main callback: absence/success/fatal/reentry PASS");}
'''
(root/'callback.c').write_text(prefix+function('scene_construction_stop')+'\n'+function('army_node_world')+'\n'+main)
(root/'boundaries.json').write_text(json.dumps({'sha256':hashlib.sha256(s.encode()).hexdigest(),'checks':checks,'scope':'Static adjacency checks for six boundaries, not whole viewer execution. Exact helper/callback test uses minimal Viewer and dependency doubles.'},indent=2)+'\n')
print('Static boundary checks PASS:',len(checks))
