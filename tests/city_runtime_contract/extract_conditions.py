from pathlib import Path
import sys,json,hashlib
S=Path(sys.argv[1]);O=Path(sys.argv[2]);O.mkdir(exist_ok=True);s=(S/'main.c').read_text();rows=[]
def part(start,end,name,prefix,suffix):
 assert s.count(start)==1,(start,s.count(start));a=s.index(start);b=s.index(end,a);x=s[a:b];(O/(name+'.inc')).write_text(prefix+x+suffix);rows.append({'name':name,'begin_line':s.count('\n',0,a)+1,'sha256':hashlib.sha256(x.encode()).hexdigest()})
part('            case BE_FORCE_DAMAGE:', '            case BE_CAMERA_RMH:', 'boss_force','static void boss_force_probe(Viewer *v,const BossEvent *events,int n) { for(int i=0;i<n;i++){const BossEvent *e=&events[i];switch(e->type){','default: next_effect++;break;}}}\n')
part('            case RE_REACTION:', '            case RE_SET_VEL:', 'rail_reaction','static void rail_reaction_probe(Viewer *v,Mon *mo,const RailEvent *events,int n) { for(int i=0;i<n;i++){const RailEvent *e=&events[i];switch(e->type){','default: next_audio++;break;}}}\n')
part('        if (_stricmp(word, "spawn") == 0)', '        if (_stricmp(word, "strike")', 'script_spawn','static const char *script_spawn_probe(Viewer*v,Mon*mo,const char*p){const char *word="spawn";do{','}while(0); next_script++;return NULL;}\n')
part('        if (_stricmp(word, "unit") == 0)', '        if (_stricmp(word, "soldier")', 'script_unit','static const char *script_unit_probe(Viewer*v,Mon*mo,const char*p){const char *word="unit";do{','}while(0); next_script++;return NULL;}\n')
part('    Group tt;\n', '    if (!v->game_built)', 'camera','static void camera_probe(Viewer*v){','renderer++;}\n')
part('                if (v.veh)                           /* and the traffic drives on', '            }\n            if (cam.use_rows)', 'free_camera','static void free_camera_probe(Viewer v){Camera cam={0};float dt=0.01f;do{','next_audio++;renderer++;}while(0);}\n')
part('        if (!v.shell && (scene_construction_stop(&v) || v.exit_game))', '#ifdef RAMPAGE_XBOX', 'next_frame','static void next_frame_probe(Viewer v){','next_audio++;next_script++;renderer++;return;leave_world: cleanup_boundary++;}\n')
(O/'extraction.json').write_text(json.dumps({'source_sha256':hashlib.sha256(s.encode()).hexdigest(),'fragments':rows,'scope':'Actual source fragments in synthetic drivers; downstream effect counters stand in for queued event/audio/render publication. No full Viewer link.'},indent=2)+'\n')
