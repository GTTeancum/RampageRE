from pathlib import Path
import sys,re,json,hashlib
R=Path(__file__).resolve().parent;sys.path.insert(0,str(R));from boundaries import functions
src=Path(sys.argv[1]) if len(sys.argv)>1 else R/'narrow-v1/candidate/src';out=Path(sys.argv[2]) if len(sys.argv)>2 else R/'narrow-v1/callbacks';out.mkdir(exist_ok=True);parts=[];rows=[]
for mod in ['army','vehicle','rail','parade','edible','boss','game','streetobj','reveal']:
 s=(src/(mod+'.c')).read_text();parts.append('#include "'+mod+'.h"\n')
 for f in functions(s):
  if not f['name'].startswith('host_'):continue
  p=s[f['begin']:f['end']];parts.append(re.sub(r'\bhost_',mod+'_host_',p)+'\n');rows.append({'module':mod,'function':f['name'],'sha256':hashlib.sha256(p.encode()).hexdigest()})
s=(src/'people.c').read_text();end=s.index('/* ---------------------------------------------------------------- small things */');parts.append(s[:end]);fs={f['name']:f for f in functions(s)}
for name in ['emit','swarm_sound']:
 f=fs[name];p=s[f['begin']:f['end']];
 if name=='swarm_sound':parts.append('static const char *swarm_str(People *p, int o, const char *prefix, const char *key);\n')
 parts.append(p+'\n');rows.append({'module':'people','function':name,'sha256':hashlib.sha256(p.encode()).hexdigest()})
(out/'callbacks.inc').write_text('\n'.join(parts));(out/'extraction.json').write_text(json.dumps(rows,indent=2)+'\n')
