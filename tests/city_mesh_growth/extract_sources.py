#!/usr/bin/env python3
"""Extract current definitions, preserving source locations; never patch the subject."""
import hashlib
import json
from pathlib import Path
import re
import sys
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
    root, city_path, work = map(Path, sys.argv[1:])
    write_support(city_path, work)
    inputs = [city_path, root/'src/mesh.c'] + sorted((root/'src').glob('*.h'))
    inputs += [p for p in sorted(Path(__file__).resolve().parent.iterdir()) if p.is_file()]
    manifest = {'inputs': {str(p): sha(p.read_bytes()) for p in inputs}, 'extracts': []}
    output = ''
    for path, names in [(city_path, ('scene_fail', 'scene_vector_grow', 'scene_add_mesh')), (root/'src/mesh.c', ('mesh_free',))]:
        source = path.read_text()
        for name in names:
            code, start, end = extract(source, name)
            first = source.count('\n', 0, start)+1
            manifest['extracts'].append(dict(file=str(path), function=name, first_line=first, sha256=sha(code.encode())))
            output += f'#line {first} "{path}"\n{code}\n'
    (work/'subject.inc').write_text(output)
    (work/'source_manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
if __name__ == '__main__':
    main()
