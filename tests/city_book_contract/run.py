#!/usr/bin/env python3
"""Copy-ready contract tests. --repo supplies current source; --city/--managers select private overlays."""
from pathlib import Path
import argparse,subprocess,json,hashlib,os,time,resource
ap=argparse.ArgumentParser();ap.add_argument('--repo',type=Path,required=True);ap.add_argument('--src',type=Path);ap.add_argument('--city',type=Path);ap.add_argument('--managers',type=Path);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--vol',type=Path);ap.add_argument('--wii-book',type=Path);ap.add_argument('--mode',choices=['normal','sanitize','both'],default='both');ap.add_argument('--sanitize',dest='mode',action='store_const',const='sanitize',default=argparse.SUPPRESS);ap.add_argument('--suite',choices=['cache','open','scene','focused','all'],default='all');args=ap.parse_args()
T=Path(__file__).resolve().parent;R=args.repo.resolve();S=args.src.resolve() if args.src else R/'src';C=args.city.resolve() if args.city else S/'city.c';M=args.managers.resolve() if args.managers else S;out=args.out.resolve();out.mkdir();(out/'tmp').mkdir();receipts=[]
pins={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for root in {S,M,C.parent,T} for p in root.glob("*") if p.suffix in [".c",".h",".py",".sh",".md"]}
(out/"source-before.json").write_text(json.dumps(pins,indent=2)+"\n")
env={**os.environ,'TMPDIR':str(out/'tmp'),'ASAN_OPTIONS':'detect_leaks=0:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'}
def lim():resource.setrlimit(resource.RLIMIT_CORE,(0,0))
def run(cmd,label):
 t=time.monotonic()
 with (out/(label+'.out')).open('x') as o,(out/(label+'.err')).open('x') as e:r=subprocess.run(list(map(str,cmd)),stdout=o,stderr=e,env=env,timeout=240,preexec_fn=lim)
 receipts.append({'label':label,'command':list(map(str,cmd)),'returncode':r.returncode,'seconds':time.monotonic()-t});(out/'receipts.json').write_text(json.dumps(receipts,indent=2)+'\n');assert not r.returncode,(label,r.returncode);print(label,'PASS',flush=True)
modes=['normal','sanitize'] if args.mode=='both' else [args.mode]
for mode in modes:
 flags=['cc','-std=c99','-D_DEFAULT_SOURCE','-D_FILE_OFFSET_BITS=64','-DRAMPAGE_LOW_MEMORY','-O1','-g0','-ffunction-sections','-fdata-sections','-I'+str(C.parent),'-I'+str(M),'-I'+str(S),'-I'+str(T),'-DCITY_SOURCE="'+str(C)+'"']
 if mode=='sanitize':flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
 link=['-Wl,--gc-sections','-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=vol_read,--wrap=vol_find','-lm']
 if args.suite in ['cache','open','focused','all']:
  exe=out/('open-'+mode);cmd=flags+[T/'open_probe.c',T/'ledger.c',T/'host_stubs.c']+[S/(n+'.c') for n in ['bundle','prb','vol']]+link+['-o',exe];run(cmd,'build-open-'+mode);run([exe]+([args.vol] if args.vol else []),'run-open-'+mode)
 if args.suite in ['cache','all']:
  exe=out/('cache-'+mode);cmd=flags+[T/'cache_probe.c',T/'ledger.c',T/'host_stubs.c']+[S/(n+'.c') for n in ['bundle','prb','vol']]+link+['-o',exe];run(cmd,'build-cache-'+mode);run([exe]+([args.vol] if args.vol else [])+([args.wii_book] if args.wii_book else []),'run-cache-'+mode)
 if args.suite in ['scene','focused','all']:
  assert args.vol,'Scene tests require --vol'
  names='vol building people cloth water tree anim prb shadow look wrand ik boss parade army vehicle rail edible collide bundle mesh'.split();sources=[(M/(n+'.c') if (M/(n+'.c')).exists() else S/(n+'.c')) for n in names]
  exe=out/('scene-'+mode);run(flags+[T/'scene_probe.c',T/'ledger.c',T/'host_stubs.c']+sources+link+['-Wl,--wrap=people_crowd','-o',exe],'build-scene-'+mode)
  cases=[('LasVegas','Objects1_Props.bdp')] if args.suite=='focused' else [('LasVegas','Objects1_Props.bdp'),('LasVegas','LasVegas_buildings.bdp'),('NewYork','Objects1_Props.bdp')]
  for city,book in cases:run([exe,args.vol,city,book],'run-scene-'+mode+'-'+city+'-'+book)
assert all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==h for p,h in pins.items()), "source changed during tests"
(out/'complete.json').write_text(json.dumps({'passed':True,'modes':modes,'suite':args.suite,'inputs':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [C,*T.glob('*.c'),*T.glob('*.h')]},'whole_process_lsan':False},indent=2)+'\n')
