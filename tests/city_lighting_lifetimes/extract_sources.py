#!/usr/bin/env python3
"""Extract current definitions, preserving source locations; never patch the subject."""
import hashlib
import json
from pathlib import Path
import re
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from city_status_extract import write_support


BASELINE_SHA256 = 'fe632db4bf3fbea6800d4c84850dcac500bb990e210cdc51f85797238d365758'


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
    root, work = map(Path, sys.argv[1:])
    here = Path(__file__).resolve().parent
    city_path, mesh_path = root/'src/city.c', root/'src/mesh.c'
    city, mesh = city_path.read_text(), mesh_path.read_text()
    write_support(city_path, work)
    manifest = {'inputs': {}, 'extracts': []}
    # Record all source/header inputs used by syntax compilation and harness types.
    for path in sorted((root/'src').glob('*.h')) + [city_path, mesh_path]:
        manifest['inputs'][str(path.relative_to(root))] = sha(path.read_bytes())
    for path in sorted(here.iterdir()):
        if path.is_file():
            manifest['inputs'][str(path.relative_to(root))] = sha(path.read_bytes())

    def take(source, name, filename):
        code, start, end = extract(source, name)
        first = source.count('\n', 0, start) + 1
        manifest['extracts'].append(dict(file=filename, function=name,
                                        first_line=first, last_line=source.count('\n', 0, end)+1,
                                        sha256=sha(code.encode())))
        return f'#line {first} "{filename}"\n{code}\n'

    common = take(mesh, 'mesh_free', 'src/mesh.c')
    for name in ('scene_fail', 'skin_free', 'scene_vector_grow', 'scene_add_mesh', 'ground_zone', 'fixed_412'):
        common += take(city, name, 'src/city.c')
    constructors = ''.join(take(city, name, 'src/city.c') for name in
                           ('mesh_copy_lit', 'light_items_make', 'ground_cracks_make', 'light_buildings'))
    full, start, _ = extract(city, 'city_build')
    # Fail closed if the final-call boundary is absent, ambiguous or restructured.
    anchor = '''    if (light_buildings(s) != 0)
        goto failed;
    if (scene_failed(s)) goto failed;
    if (crowd_sound) {
        static const float ORIGIN[3] = { 0, 0, 0 };
        int se = sound_effect_start("Crowd_Panic", 1);
        if (se >= 0) sound_effect_set_point(se, ORIGIN);
    }
    return 0;
failed:
    c->create_crowd = saved_crowd;
    c->nrunners = saved_nrunners;
    memcpy(c->runners, saved_runners, sizeof saved_runners);
    scene_free(s);
    return -1;
}'''
    if full.count('light_buildings(s)') != 1 or not full.endswith(anchor):
        raise ValueError('city_build final lighting call/failure tail changed; review boundary')
    tail = full[-len(anchor):]
    offset = start + len(full) - len(anchor)
    manifest['extracts'].append(dict(file='src/city.c', function='city_build final tail',
                                    first_line=city.count('\n', 0, offset)+1,
                                    last_line=city.count('\n', 0, start+len(full))+1,
                                    sha256=sha(tail.encode())))
    # Retain the actual rollback snapshot prefix. This lighting-only wrapper has
    # no crowd transition; audio doubles below fail if that boundary is crossed.
    prefix_start = full.index('    int saved_crowd =')
    prefix_end = full.index('    memset(s, 0, sizeof *s);', prefix_start)
    snapshot = full[prefix_start:prefix_end]
    if snapshot.count('int crowd_sound = 0;') != 1:
        raise ValueError('City rollback snapshot changed; review lighting wrapper')
    manifest['extracts'].append(dict(file='src/city.c', function='city_build rollback snapshot',
                                    first_line=city.count('\n',0,start+prefix_start)+1,
                                    sha256=sha(snapshot.encode())))
    tail = ('static int build_tail(Scene *s) {\n'
            '    City fallback = {0}; City *c = s->owner ? s->owner : &fallback;\n'
            + snapshot + tail + '\n')
    destructor = take(city, 'scene_free', 'src/city.c')
    (work/'candidate.inc').write_text(common + constructors + tail + destructor)
    baseline = here/'baseline_functions.inc'
    if sha(baseline.read_bytes()) != BASELINE_SHA256:
        raise ValueError('historical baseline fixture hash mismatch')
    (work/'baseline.inc').write_text(common + baseline.read_text() + destructor)
    (work/'ground_cracks.inc').write_text(take(city, 'ground_cracks', 'src/city.c'))
    constants = []
    for name in ('CELL', 'STOREY'):
        matches = list(re.finditer(r'^#define '+name+r' [^\n]+$', city, re.M))
        if len(matches) != 1:
            raise ValueError(f'{name}: expected unique constant definition')
        constants.append(matches[0][0])
        line = city.count('\n', 0, matches[0].start()) + 1
        manifest['extracts'].append(dict(file='src/city.c', constant=name,
                                        first_line=line, last_line=line,
                                        sha256=sha(matches[0][0].encode())))
    (work/'constants.inc').write_text('\n'.join(constants)+'\n')
    (work/'source_manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
