#!/usr/bin/env python3
"""Extract current palette reuse and its update loop; reconstruct always-build oracle."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--source-root', type=Path, default=Path(os.environ.get('RAMPAGE_TEST_ROOT', HERE.parents[1])))
parser.add_argument('--city-source', type=Path, help='Private candidate override; default: selected source-root/src/city.c')
args = parser.parse_args()
root = args.source_root.resolve()
city = (args.city_source or root / 'src/city.c').resolve()
if not os.environ.get('TMPDIR'):
    parser.error('Set TMPDIR to a writable workspace directory; all outputs are retained.')
work = Path(tempfile.mkdtemp(prefix='skin-palette-reuse.', dir=os.environ['TMPDIR']))
print(f'Retained: {work}', flush=True)
source = city.read_text()
spans = []

def require(condition, message):
    if not condition:
        raise SystemExit(f'Extractor rejected current source: {message}. Review extraction for the new implementation.')

def function(name):
    matches = list(re.finditer(r'^(?:static )?[^\n]*\b' + name + r'\([^;\n]*\)\n\{.*?^\}', source, re.M | re.S))
    require(len(matches) == 1, f'{name}: expected one complete function')
    match = matches[0]
    text = match.group()
    spans.append({'name': name, 'line': source.count('\n', 0, match.start()) + 1,
                  'sha256': hashlib.sha256(text.encode()).hexdigest()})
    return text + '\n'

skin = function('skin_item')
require('DrawItem *it, int rebuild_palette)' in skin, 'skin_item must expose the reviewed rebuild flag')
# Remove only the outer cache gate for the oracle, keeping current arithmetic and cloth body.
gate = '    if (rebuild_palette) {'
require(skin.count(gate) == 1, 'expected one palette rebuild gate')
a = skin.index(gate)
b = skin.index('{', a)
depth, end = 1, b + 1
while depth and end < len(skin):
    depth += (skin[end] == '{') - (skin[end] == '}')
    end += 1
require(depth == 0, 'unclosed palette gate')
body = skin[b + 1:end - 1]
require('palette[j] = mat_mul(in->node[j], ib);' in body and 'if (in->mirrored)' in body,
        'gate must enclose the original inverse-bind and mirror computations')
require('const Mesh *m =' not in body, 'gate must not enclose vertex work')
reference = (skin[:a] + body + skin[end:]).replace(', int rebuild_palette)', ')', 1)
reference = reference.replace('skin_item(', 'reference_skin(', 1)
candidate = skin.replace('skin_item(', 'candidate_skin(', 1)
# Instrument palette builds, not unrelated mat_mul work in item/cloth transforms.
loop = 'for (uint32_t j = 0; j < nb; j++) {'
for label, text in [('reference', reference), ('candidate', candidate)]:
    require(loop in text, 'missing palette multiplication loop')
    text = text.replace(loop, f'{label}_builds++; {label}_mults += nb;\n    ' + loop, 1)
    if label == 'reference':
        reference = text
    else:
        candidate = text

update = function('update')
key = '    const Instance *last_skin_instance = NULL;'
require(update.count(key) == 1, 'expected update-local NULL cache key')
pass_body = update[update.index(key):update.rfind('}')]
require(pass_body.count('skin_item(s, in, it, in != last_skin_instance);') == 1,
        'expected pointer-change rebuild predicate')
require(pass_body.count('last_skin_instance = in;') == 1, 'expected one cache-key assignment')
require('pose_instance(' not in pass_body and 'monster_ik(' not in pass_body,
        'pose/IK must finish before palette reuse starts')
require('for (uint32_t i = 0; i < s->nitems; i++)' in pass_body,
        'expected current DrawItem traversal')
candidate_pass = 'static void candidate_pass(Scene *s, int all) {\n' + pass_body.replace('skin_item(', 'candidate_skin(') + '}\n'
reference_pass_body = pass_body.replace(key, '').replace('skin_item(s, in, it, in != last_skin_instance);', 'reference_skin(s, in, it);').replace('            last_skin_instance = in;', '')
reference_pass = 'static void reference_pass(Scene *s, int all) {\n' + reference_pass_body + '}\n'
code = '''#include "city.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned long reference_builds, reference_mults, candidate_builds, candidate_mults;
/* Sway is unrelated to palette reuse. These synthetic scenes contain no monsters. */
static float monster_sway(const Scene *s, int k) { (void)s; (void)k; abort(); }
'''
code += ''.join(function(name) for name in ['mat_identity', 'mat_mul', 'scene_monster_slot'])
code += reference + candidate + reference_pass + candidate_pass
code += '\n#include "cases.c"\n'
(work / 'probe.c').write_text(code)
inputs = [city, root / 'src/city.h', root / 'src/data.h', root / 'src/cloth.c', root / 'src/wrand.c', HERE / 'cases.c', Path(__file__)]
manifest = {'sources': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},
            'spans': spans, 'generated_sha256': hashlib.sha256(code.encode()).hexdigest(),
            'reference': 'Current skin_item with only rebuild_palette gate removed; current skin loop with cache-key logic removed.',
            'sanitized': args.sanitize}
(work / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
env = dict(os.environ)
flags = ['-O2']
if args.sanitize:
    flags = ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    env['ASAN_OPTIONS'] = 'detect_leaks=0:halt_on_error=1'
    env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
receipts = []
for mode, defines in [('host', []), ('xbox-low-memory', ['-DRAMPAGE_XBOX', '-DRAMPAGE_LOW_MEMORY'])]:
    cmd = shlex.split(os.environ.get('CC', 'cc')) + ['-pipe', '-std=c99', '-D_DEFAULT_SOURCE', '-ffunction-sections', '-fdata-sections'] + flags + defines
    cmd += ['-I', str(root / 'src'), '-I', str(HERE), str(work / 'probe.c'), str(root / 'src/cloth.c'), str(root / 'src/wrand.c'), '-Wl,--gc-sections', '-lm', '-o', str(work / mode)]
    with (work / (mode + '-build.log')).open('x') as out:
        result = subprocess.run(cmd, env=env, stdout=out, stderr=subprocess.STDOUT)
    receipts.append({'mode': mode, 'command': cmd, 'build_returncode': result.returncode})
    if result.returncode:
        raise SystemExit(f'Build failed: {work / (mode + "-build.log")}')
    with (work / (mode + '.log')).open('x') as out:
        result = subprocess.run([str(work / mode)], env=env, stdout=out, stderr=subprocess.STDOUT)
    print((work / (mode + '.log')).read_text(), end='', flush=True)
    receipts[-1]['run_returncode'] = result.returncode
    (work / 'receipts.json').write_text(json.dumps(receipts, indent=2) + '\n')
    if result.returncode:
        raise SystemExit(result.returncode)
require(all(hashlib.sha256(Path(p).read_bytes()).hexdigest() == h for p, h in manifest['sources'].items()),
        'input changed during regression')
(work / 'complete.json').write_text(json.dumps({'passed': True, 'inputs_unchanged': True, 'modes': len(receipts)}) + '\n')
