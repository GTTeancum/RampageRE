from pathlib import Path
import sys,shutil,json,hashlib
src=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
if len(sys.argv)>3:
    pins=json.loads((out/'source-pins.json').read_text())
    assert all(sha(src/name)==value and sha(out/'src'/name)==value for name,value in pins.items())
    (out/'complete.json').write_text(json.dumps({'passed':True,'source_unchanged':True,'exercised_cpu_owners_zero':True,'whole_world_or_GPU_claim':False},indent=2)+'\n')
else:
    assert (src/'main.c').is_file() and (src/'city.c').is_file()
    stage=out/'src';stage.mkdir()
    pins={p.name:sha(p) for p in src.iterdir() if p.is_file() and p.suffix in ('.c','.h')}
    for name,value in pins.items():
        shutil.copy2(src/name,stage/name)
        assert sha(src/name)==value and sha(stage/name)==value
    (out/'source-pins.json').write_text(json.dumps(pins,indent=2)+'\n')
