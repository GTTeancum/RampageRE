"""Generate actual City status owner/helpers for isolated current-source harnesses.

The complete private City declaration is extracted, not approximated. Its members
are stack-backed test owners only: this does not execute the Book reader/cache.
No fallback definitions or unconditional status doubles are accepted.
"""
from pathlib import Path
import hashlib
import json
import re


def masked(source):
    token = re.compile(r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\'')
    return token.sub(lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]), source)


def function(source, name):
    clean = masked(source)
    matches = list(re.finditer(r'^(?:static\s+)?[\w *]+\b' + re.escape(name) + r'\([^;{}]*\)\s*\{', clean, re.M))
    if len(matches) != 1:
        raise ValueError(f'{name}: expected unique definition, found {len(matches)}')
    start, end = matches[0].span()
    depth = 1
    while depth and end < len(clean):
        depth += (clean[end] == '{') - (clean[end] == '}')
        end += 1
    if depth:
        raise ValueError(f'{name}: unterminated definition')
    return source[start:end], start, end


def write_support(city_path, output):
    city_path, output = Path(city_path).resolve(), Path(output)
    source = city_path.read_text()
    anchor = 'typedef struct { char name[64]; Bundle b; Book book; int ok; } LoadedBook;\nstruct City {'
    if source.count(anchor) != 1:
        raise ValueError('City owner declaration changed; review extraction')
    start = source.index(anchor)
    end = source.index('\n};', start) + 3
    constants = list(re.finditer(r'^#define MAX_BOOKS [^\n]+$', source, re.M))
    if len(constants) != 1:
        raise ValueError('Expected unique MAX_BOOKS definition')
    pieces = [('MAX_BOOKS', constants[0][0], constants[0].start(), constants[0].end()),
              ('City owner declaration', source[start:end], start, end)]
    for name in ('city_load_failed', 'scene_failed'):
        code, start, end = function(source, name)
        pieces.append((name, code, start, end))
    result = '#ifndef TEST_CITY_STATUS_INC\n#define TEST_CITY_STATUS_INC\n'
    records = []
    for name, code, start, end in pieces:
        line = source.count('\n', 0, start) + 1
        result += f'#line {line} "{city_path}"\n{code}\n'
        records.append(dict(name=name, first_line=line, last_line=source.count('\n',0,end)+1,
                            sha256=hashlib.sha256(code.encode()).hexdigest()))
    result += '''
/* This scoped check allocates/frees nothing. It verifies the real helpers share
 * the borrowed owner latch, retain failure after the owner's reset, and neither
 * copy the latch to a different City nor clear unrelated Scene state. */
static void assert_city_status_contract(void)
{
    City owner = {0}, other = {0};
    Scene scene = {0}, independent = {0};
    assert(!city_load_failed(NULL));
    assert(scene_failed(NULL));
    assert(!scene_failed(&scene));
    scene.owner = &owner;
    independent.owner = &other;
    scene.nmon = 2;
    owner.load_failed = 1;
    assert(city_load_failed(&owner));
    assert(scene_failed(&scene) && scene.construction_failed);
    assert(scene.owner == &owner && scene.nmon == 2);
    assert(!scene_failed(&independent));
    owner.load_failed = 0;
    assert(!city_load_failed(&owner) && scene_failed(&scene));
}
#endif
'''
    (output/'city-status.inc').write_text(result)
    manifest = dict(source=str(city_path), source_sha256=hashlib.sha256(city_path.read_bytes()).hexdigest(),
                    extractor=str(Path(__file__).resolve()), extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                    extracts=records, output_sha256=hashlib.sha256(result.encode()).hexdigest(),
                    scope='Actual City declaration and status helpers, not a full Book/cache implementation; borrowed-owner propagation assertions execute in each harness.')
    (output/'city-status-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
