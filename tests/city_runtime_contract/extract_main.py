from pathlib import Path
import sys,hashlib,json
sys.path.insert(0,str(Path(__file__).resolve().parent))
from boundaries import functions
R=Path(__file__).resolve().parent;src=Path(sys.argv[1]) if len(sys.argv)>1 else R/'runs-v1/src';out=Path(sys.argv[2]) if len(sys.argv)>2 else R/'runs-v1/main-extracted';out.mkdir(exist_ok=True)
s=(src/'main.c').read_text();fs={f['name']:f for f in functions(s)}
(out/'types.h').write_text(s[:s.index('static int scene_construction_stop(Viewer *v);')])
records=[]
for name in ['scene_construction_stop','world_load_failed','building_script']:
 f=fs[name];part=s[f['begin']:f['end']];(out/(name+'.inc')).write_text(part+'\n');records.append({'function':name,'sha256':hashlib.sha256(part.encode()).hexdigest()})
a=s.index('            case GE_RESULTS:');b=s.index('            case GE_REVIVE:',a)
part=s[a:b];(out/'results.inc').write_text('static void results_probe(Viewer *v,const GameEvent *e) {\n switch(e->type) {\n'+part+'\n default: break;\n }\n}\n')
records.append({'fragment':'game_step GE_RESULTS','sha256':hashlib.sha256(part.encode()).hexdigest()})
(out/'extraction.json').write_text(json.dumps(records,indent=2)+'\n')
