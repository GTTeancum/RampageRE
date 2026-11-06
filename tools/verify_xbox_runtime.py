#!/usr/bin/env python3
"""Verify pinned runtime dependencies without editing or rebuilding them.

Replays patches in memory at exact line positions (no offset/fuzz), checks
matching archive bytes AND ordered members, and emits only fresh source/proof
files. This is not a dependency builder or a claim about default CMake output.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / 'platform/xbox/runtime-dependencies.json'


def require(ok, message):
    if not ok:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def checked(path, expected):
    require(path.is_file(), f'Missing input: {path}')
    require(path.stat().st_size <= 2 * 1024 * 1024, f'Oversized input: {path}')
    data = path.read_bytes()
    require(digest(data) == expected, f'SHA-256 mismatch: {path}')
    return data


def replay(files, patch):
    lines = patch.splitlines(True)
    i = 0
    while i < len(lines):
        require(lines[i].startswith('--- '), 'Expected old patch path')
        oldname = lines[i][4:].strip(); i += 1
        require(i < len(lines) and lines[i].startswith('+++ '), 'Expected new patch path')
        name = lines[i][4:].strip().removeprefix('b/'); i += 1
        require(name and not name.startswith('/') and '..' not in Path(name).parts, 'Unsafe patch path')
        if oldname == '/dev/null':
            require(name not in files, f'New patch file already exists: {name}')
            old = []
        else:
            require(oldname == 'a/' + name and name in files, 'Unexpected patch source')
            old = files[name].splitlines(True)
        out = []; cursor = 0; hunks = 0
        while i < len(lines) and lines[i].startswith('@@ '):
            m = re.fullmatch(r'@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@[^\n]*\n', lines[i])
            require(m is not None, 'Malformed hunk')
            oldlen, newlen = int(m[2] or 1), int(m[4] or 1)
            start = int(m[1]) - bool(oldlen)
            require(cursor <= start <= len(old), 'Hunk offset outside exact source')
            out.extend(old[cursor:start]); cursor = start
            require(len(out) == int(m[3]) - bool(newlen), 'New hunk position mismatch')
            i += 1; used = added = 0
            while i < len(lines) and not lines[i].startswith(('@@ ', '--- ')):
                line = lines[i]
                require(line[:1] in (' ', '+', '-'), 'Unsupported patch line')
                if line[0] in ' -':
                    require(cursor < len(old) and old[cursor] == line[1:], 'Exact source hunk mismatch')
                    cursor += 1; used += 1
                if line[0] in ' +':
                    out.append(line[1:]); added += 1
                i += 1
            require((used, added) == (oldlen, newlen), 'Hunk length mismatch')
            hunks += 1
        require(hunks > 0, 'Empty patch file')
        out.extend(old[cursor:]); files[name] = ''.join(out)


def archive_members(data):
    require(data[:8] == b'!<arch>\n', 'Not a regular archive')
    offset = 8; names = b''; members = []
    while offset < len(data):
        h = data[offset:offset + 60]
        require(len(h) == 60 and h[58:] == b'`\n', 'Invalid archive header')
        size = int(h[48:58]); require(size >= 0, 'Negative member size')
        payload = data[offset + 60:offset + 60 + size]
        require(len(payload) == size, 'Truncated member')
        name = h[:16].decode('ascii').rstrip()
        if name == '//':
            names = payload
        elif name != '/':
            if name.startswith('/'):
                start = int(name[1:]); require(start < len(names), 'Bad long-name offset')
                ends = [end for delimiter in (b'\n', b'\0') if (end := names.find(delimiter, start)) >= 0]
                require(bool(ends), 'Unterminated long name')
                end = min(ends)
                name = names[start:end].rstrip(b'/').decode('utf-8')
            else:
                name = name.removesuffix('/')
            members.append({'name': name, 'sha256': digest(payload)})
        offset += 60 + size + (size % 2)
    require(offset == len(data), 'Bad archive padding')
    return members


def verify(args):
    manifest = json.loads(MANIFEST.read_text())
    out = args.output.resolve()
    require(not out.exists(), f'Refusing existing output: {out}')
    require(not out.is_relative_to(ROOT), 'Keep generated output outside the repository')
    for path in [args.pbkit_source, args.gles_source, args.pbkit_archive, args.gles_archive]:
        require(not out.is_relative_to(path.resolve().parent), 'Keep output outside dependency inputs')
    sources = {
        'lib/pbkit/pbkit.c': checked(args.pbkit_source, manifest['baseline_sources']['lib/pbkit/pbkit.c']).decode(),
        'nv2a_helper.c': checked(args.gles_source, manifest['baseline_sources']['nv2a_helper.c']).decode(),
    }
    for patch, expected in manifest['patches'].items():
        replay(sources, checked(ROOT / patch, expected).decode())
    for name, expected in manifest['final_sources'].items():
        require(digest(sources[name].encode()) == expected, f'Final source mismatch: {name}')
    checked(ROOT / 'src/audio_xbox_hal.c', manifest['audio_hal_sha256'])
    archives = {}
    for label, path in [('pbkit', args.pbkit_archive), ('gles', args.gles_archive)]:
        entry = manifest['archives'][label]
        data = checked(path, entry['sha256'])
        actual = archive_members(data)
        require(actual == entry['members'], f'Ordered archive members differ: {label}')
        archives[label] = {'path': str(path.resolve()), 'sha256': digest(data), 'members': actual}
    out.mkdir()
    for name, source in sources.items():
        target = out / name; target.parent.mkdir(parents=True, exist_ok=True)
        with target.open('x') as handle: handle.write(source)
    report = {'exact_replay': True, 'sources': manifest['final_sources'], 'audio_hal_sha256': manifest['audio_hal_sha256'], 'archives': archives,
              'limits': 'Verification only. No compilation, archive replacement, default-build reproduction or new runtime test.'}
    with (out / 'verification.json').open('x') as handle:
        json.dump(report, handle, indent=2); handle.write('\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ['pbkit-source', 'gles-source', 'pbkit-archive', 'gles-archive', 'output']:
        parser.add_argument('--' + flag, required=True, type=Path)
    args = parser.parse_args()
    try:
        verify(args)
    except (ValueError, OSError, KeyError) as error:
        parser.exit(1, f'Refused: {error}\n')
    print('PASS: exact source replay and matching pinned archive pair; inputs unchanged.')


if __name__ == '__main__':
    main()
