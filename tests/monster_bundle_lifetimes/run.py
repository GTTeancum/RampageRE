#!/usr/bin/env python3
"""Extract current definitions, preserving source locations; never patch the subject."""
import hashlib
import json
from pathlib import Path
import re
import sys
import os
import subprocess
import tempfile
import argparse
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from city_status_extract import write_support




def sha(data):
    return hashlib.sha256(data).hexdigest()


def masked(source):
    # Mask comments and literals, retaining positions/newlines for brace balancing.
    token = re.compile(r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\'')
    return token.sub(lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]), source)


def extract(source, name):
    clean = masked(source)
    pattern = r'^(?:static\s+)?[\w *]+\b' + re.escape(name) + r'\([^;{}]*\)\s*\{'
    matches = list(re.finditer(pattern, clean, re.M))
    if len(matches) != 1:
        raise ValueError(f'{name}: expected one definition, found {len(matches)}')
    start, end = matches[0].span()
    depth = 1
    while depth and end < len(clean):
        depth += (clean[end] == '{') - (clean[end] == '}')
        end += 1
    if depth:
        raise ValueError(f'{name}: unterminated function')
    return source[start:end], start, end


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = Path(os.environ.get('REPO_ROOT', here.parents[1])).resolve()
    city = Path(os.environ.get('CITY_SOURCE', root/'src/city.c')).resolve()
    work = Path(tempfile.mkdtemp(prefix='rampage-monster-bundles.', dir=os.environ.get('TMPDIR', root)))
    print(f'Test artifacts retained: {work}', flush=True)
    write_support(city, work)
    source = city.read_text()
    failure_helper, helper_start, _ = extract(source, 'scene_fail')
    own, own_start, _ = extract(source, 'own_bundle')
    monster, mon_start, _ = extract(source, 'city_monster')
    boundary = '    int id = new_instance('
    if monster.count(boundary) != 1:
        raise ValueError('Expected unique pre-instance boundary; review extraction')
    prefix = monster[:monster.index(boundary)]
    # Preserve the actual rollback tail when present. Baseline without rollback
    # still extracts and fails the ownership assertions, providing a negative control.
    labels = list(re.finditer(r'^failed_bundles:\s*$', masked(monster), re.M))
    if len(labels) > 1:
        raise ValueError('Duplicate rollback label')
    tail = monster[labels[0].start():] if labels else '}'
    subject = (f'#line {source.count(chr(10), 0, helper_start)+1} \"{city}\"\n'+failure_helper+'\n'
               + f'#line {source.count(chr(10), 0, own_start)+1} "{city}"\n'+own+'\n'
               + f'#line {source.count(chr(10), 0, mon_start)+1} "{city}"\n'+prefix
               + '    return 777; /* Test-only prerequisite-success boundary. */\n'+tail+'\n')
    (work/'subject.inc').write_text(subject)
    inputs = [city] + sorted((root/'src').glob('*.h')) + sorted(p for p in here.iterdir() if p.is_file())
    pins = {str(p): sha(p.read_bytes()) for p in inputs}
    manifest = {'inputs': pins, 'subject_sha256': sha(subject.encode()),
                'boundary': 'Before new_instance; no successful runtime construction', 'runs': []}
    flags = ['-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else ['-O2']
    env = dict(os.environ, TMPDIR=str(work))
    if args.sanitize:
        env.setdefault('ASAN_OPTIONS', 'detect_leaks=0:halt_on_error=1')
        env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
    try:
        for layout in ('ordinary', 'low_memory'):
            cmd = [os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                   '-Wno-unused-parameter', '-Wno-unused-variable'] + flags
            if layout == 'low_memory': cmd += ['-DRAMPAGE_LOW_MEMORY']
            cmd += ['-I'+str(root/'src'), '-I'+str(work), str(here/'regression.c'), '-o', str(work/layout)]
            row = {'layout': layout, 'command': cmd}
            manifest['runs'].append(row)
            with (work/(layout+'.compile.log')).open('w') as log:
                result = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, env=env, timeout=60)
            row['compile_status'] = result.returncode
            if result.returncode: raise RuntimeError(f'Build failed: {layout}.compile.log')
            with (work/(layout+'.log')).open('w') as log:
                result = subprocess.run([str(work/layout)], stdout=log, stderr=subprocess.STDOUT, env=env, timeout=20)
            row['test_status'] = result.returncode
            print((work/(layout+'.log')).read_text(), end='', flush=True)
            if result.returncode: raise RuntimeError(f'Test failed: {layout}.log')
    finally:
        manifest['inputs_unchanged'] = all(sha(Path(p).read_bytes()) == h for p,h in pins.items())
        (work/'receipt.json').write_text(json.dumps(manifest, indent=2)+'\n')
    assert manifest['inputs_unchanged']

if __name__ == '__main__':
    main()
