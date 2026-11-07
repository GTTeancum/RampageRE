"""Generate source-extracted bounded probes and retain reproducible evidence per run."""
from pathlib import Path
import hashlib, json, os, re, shutil, subprocess, sys, tempfile, time
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from city_status_extract import write_support


HERE = Path(__file__).resolve().parent
SOURCE = Path(sys.argv[1]).resolve()
MODE = sys.argv[2]
assert MODE in ('normal', 'sanitize')
for name in ('city.c', 'city.h', 'tree.c', 'anim.c', 'cloth.c', 'main.c'):
    if not (SOURCE / name).is_file():
        raise SystemExit(f'Missing current source: {SOURCE / name}; set SOURCE_DIR to the repository src directory')

OUTPUT = Path(tempfile.mkdtemp(prefix=f'shared-constructor-{MODE}-',
    dir=os.environ.get('SHARED_TEST_OUTPUT_ROOT', os.environ.get('TMPDIR', '/tmp'))))
(OUTPUT / 'tmp').mkdir()
print(f'Retained test artifacts: {OUTPUT}', flush=True)

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def source_pins():
    return {str(p): sha(p) for p in sorted(SOURCE.rglob('*'))
            if p.is_file() and p.suffix in ('.c', '.h')}

pins = source_pins()
(OUTPUT / 'source-before.json').write_text(json.dumps(pins, indent=2) + '\n')
env = dict(os.environ, TMPDIR=str(OUTPUT / 'tmp'))
if MODE == 'sanitize':
    # LSan is unsupported in the execution environment; all harnesses count owners independently.
    env.update(ASAN_OPTIONS='detect_leaks=0:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
receipts = []

def run(command, label):
    start = time.monotonic()
    with (OUTPUT / (label + '.out')).open('x') as out, (OUTPUT / (label + '.err')).open('x') as err:
        result = subprocess.run(command, stdout=out, stderr=err, env=env, timeout=120)
    receipts.append(dict(label=label, command=command, status=result.returncode,
        seconds=time.monotonic()-start, stdout_sha256=sha(OUTPUT/(label+'.out')),
        stderr_sha256=sha(OUTPUT/(label+'.err'))))
    (OUTPUT/'receipts.json').write_text(json.dumps(receipts, indent=2)+'\n')
    if result.returncode:
        raise SystemExit(f'{label} failed ({result.returncode}); inspect {OUTPUT / (label + ".err")}')

write_support(SOURCE/'city.c', OUTPUT)
city = (SOURCE / 'city.c').read_text()
def function(name):
    matches = list(re.finditer(r'^(?:static )?[^\n;]*\b'+re.escape(name)+r'\([^;]*?\)\n\{', city, re.M))
    if len(matches) != 1:
        raise SystemExit(f'Expected one function definition for {name}, found {len(matches)}')
    begin = matches[0].start()
    end = city.index('{', begin) + 1
    depth = 1
    while depth:
        depth += (city[end] == '{') - (city[end] == '}')
        end += 1
    return city[begin:end]

storage_names = ['scene_fail','skin_free','scene_vector_grow','scene_add_mesh',
                 'scene_mesh_of','new_instance','push_item','new_skin']
(OUTPUT / 'storage.inc').write_text('\n\n'.join(function(n) for n in storage_names)+'\n')
person = re.search(r'^struct PersonLook .*;', city, re.M)
assert person
model_names = ['mat_identity','find_entry','find_anim','place_model','vehicle_look']
(OUTPUT / 'models.inc').write_text(person[0]+'\n'+
    '\n\n'.join(function(n) for n in model_names)+'\n')
for name in ('storage.c', 'models.c', 'readers.c'):
    shutil.copyfile(HERE/name, OUTPUT/name)
run([sys.executable,str(HERE/'generate_flags.py'),str(SOURCE),str(OUTPUT)], 'generate-flags')
run([sys.executable,str(HERE/'generate_guards.py'),str(SOURCE),str(OUTPUT)], 'generate-guards')

flags = ['-std=gnu11', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra', '-Werror', '-O2']
if MODE == 'sanitize':
    flags = flags[:-1] + ['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
for config in ('ordinary', 'low-memory'):
    defs = ['-DRAMPAGE_LOW_MEMORY'] if config == 'low-memory' else []
    for name in ('storage','models','readers','flags','monster','callback'):
        executable = OUTPUT / f'{name}-{config}'
        command = [os.environ.get('CC', 'cc'), *flags, *defs, '-I'+str(SOURCE),
                   str(OUTPUT/(name+'.c')), '-lm', '-o', str(executable)]
        run(command, 'build-'+executable.name)
        run([str(executable)], 'run-'+executable.name)
        print(f'{MODE} {config} {name}: PASS', flush=True)

assert source_pins() == pins, 'Source changed during the run; results do not describe a stable checkout'
(OUTPUT / 'source-after.json').write_text(json.dumps(pins, indent=2)+'\n')
(OUTPUT / 'artifacts.json').write_text(json.dumps({p.name:sha(p) for p in sorted(OUTPUT.iterdir()) if p.is_file()}, indent=2)+'\n')
(OUTPUT / 'complete.json').write_text(json.dumps(dict(passed=True,mode=MODE,source=str(SOURCE),
    configurations=['ordinary','low-memory'],harnesses=6,source_unchanged=True,
    leak_check='Explicit tracked-owner accounting; no LeakSanitizer claim'),indent=2)+'\n')
print(f'PASS. Outputs, commands and source/artifact hashes retained at {OUTPUT}', flush=True)
