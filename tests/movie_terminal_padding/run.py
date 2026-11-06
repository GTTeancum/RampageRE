#!/usr/bin/env python3
"""Retained exact-current-source regression; all compiler scratch stays in output."""
import argparse,hashlib,json,os,subprocess,sys
from pathlib import Path

sys.dont_write_bytecode=True
from extract_sources import extract

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True,help='New output directory; existing directories are refused')
    parser.add_argument('--sanitize',action='store_true',help='Also run exact audio-core ASan/UBSan tests')
    parser.add_argument('--movies',type=Path,help='Optional existing directory with privately owned midway.pss, digital.pss and intro.pss')
    args=parser.parse_args()
    here=Path(__file__).resolve().parent;root=here.parent.parent;source=root/'src/movie.c'
    out=args.output.resolve();out.mkdir(parents=False,exist_ok=False)
    tmp=out/'compiler-tmp';tmp.mkdir()
    env=dict(os.environ,TMPDIR=str(tmp))
    manifest=extract(source,out)
    tests=[here/n for n in ['run.py','extract_sources.py','wrapper.c','test_api.h','tests.c']]
    pins={str(p):digest(p) for p in [source,root/'src/movie.h',root/'src/plat.h',root/'src/data.h',*tests]}
    movies=[]
    if args.movies:
        movies=[(args.movies/n).resolve() for n in ['midway.pss','digital.pss','intro.pss']]
        if not all(p.is_file() for p in movies):raise RuntimeError('All three existing PSS files are required')
    commands=[]
    def run(label,command,environment=env):
        command=list(map(str,command));commands.append({'label':label,'command':command,'cwd':str(out),
            'TMPDIR':environment['TMPDIR'],'ASAN_OPTIONS':environment.get('ASAN_OPTIONS'),
            'UBSAN_OPTIONS':environment.get('UBSAN_OPTIONS')})
        with (out/(label+'.log')).open('xb') as log:
            result=subprocess.run(command,cwd=out,env=environment,stdout=log,stderr=subprocess.STDOUT)
        if label.endswith('-run') or result.returncode:print((out/(label+'.log')).read_text(),end='')
        if result.returncode:raise subprocess.CalledProcessError(result.returncode,command)
    try:
        for mode in ['normal']+(['sanitize'] if args.sanitize else []):
            wrappers=[]
            for name,variant in [('baseline','base'),('candidate','fixed')]:
                original=(out/'movie.baseline.c') if name=='baseline' else source
                selected=(out/f'audio.{name}.c') if mode=='sanitize' else original
                body=f'#define VARIANT {variant}\n#define MOVIE_SOURCE {json.dumps(str(selected))}\n'
                body+=f'#include {json.dumps(str(here/"wrapper.c"))}\n'
                path=out/f'{mode}.{name}.c'
                with path.open('x') as f:f.write(body)
                wrappers.append(path)
            binary=out/f'tests.{mode}'
            cmd=[os.environ.get('CC','cc'),'-pipe','-std=gnu11','-Os','-ffunction-sections','-fdata-sections',
                 '-D_FILE_OFFSET_BITS=64','-D_GNU_SOURCE','-I'+str(root/'src')]
            if mode=='sanitize':cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-DMOVIE_XBOX_FAST','-DAUDIO_ONLY']
            # The compiler's intermediate objects are normal transient scratch
            # under our workspace TMPDIR; sources, final binaries/logs remain.
            cmd+=[*wrappers,here/'tests.c','-Wl,--gc-sections','-lm','-o',binary]
            run(mode+'-compile',cmd)
            runenv=env if mode=='normal' else dict(env,ASAN_OPTIONS=env.get('ASAN_OPTIONS','detect_leaks=0:halt_on_error=1'),
                                                  UBSAN_OPTIONS=env.get('UBSAN_OPTIONS','halt_on_error=1'))
            run(mode+'-run',[binary,*movies],runenv)
        if any(digest(Path(p))!=h for p,h in pins.items()):raise RuntimeError('A tested source changed during the run')
        result={'passed':True,'current_source_sha256':manifest['current_source_sha256'],
                'control_sha256':manifest['control_sha256'],'source_pins':pins,
                'movies':[str(p) for p in movies],'sanitizers_run':args.sanitize,
                'sanitizer_boundary':'Exact audio core/demux; full-source normal run independently tests movie_open/close',
                'leak_check':'Not established; default sanitizer configuration disables leak detection'}
        with (out/'result.json').open('x') as f:json.dump(result,f,indent=2);f.write('\n')
    finally:
        with (out/'commands.json').open('x') as f:json.dump(commands,f,indent=2);f.write('\n')
    print('Retained test output:',out)

if __name__=='__main__':main()
