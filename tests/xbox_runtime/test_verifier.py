#!/usr/bin/env python3
"""Small no-build refusal and extracted CMake-selection regressions."""
import argparse
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from verify_xbox_runtime import archive_members, checked, digest, replay, require, verify


def main(args):
    out = args.output.resolve()
    require(not out.exists() and not out.is_relative_to(ROOT), 'Output must be new and outside repo')
    out.mkdir()
    results = []
    def refuses(name, action):
        try: action()
        except (ValueError, KeyError, OSError): results.append(name); return
        raise ValueError('Did not reject: ' + name)
    patch = '--- a/a.c\n+++ b/a.c\n@@ -2,1 +2,1 @@\n-old\n+new\n'
    state = {'a.c': 'one\nold\nthree\n'}; replay(state, patch)
    require(state['a.c'] == 'one\nnew\nthree\n', 'Valid exact replay failed'); results.append('exact-replay')
    for name, source, change in [
        ('offset', {'a.c':'zero\none\nold\nthree\n'}, patch),
        ('context', {'a.c':'one\nwrong\nthree\n'}, patch),
        ('new-position', {'a.c':'one\nold\nthree\n'}, patch.replace('+2,1','+3,1')),
        ('hunk-length', {'a.c':'one\nold\nthree\n'}, patch.replace('-2,1','-2,2')),
        ('traversal', {'../a.c':'one\nold\nthree\n'}, patch.replace('a/a.c','a/../a.c').replace('b/a.c','b/../a.c')),
        ('missing-source', {}, patch),
        ('already-applied', {'a.c':'one\nnew\nthree\n'}, patch),
        ('new-file-exists', {'a.c':''}, '--- /dev/null\n+++ b/a.c\n@@ -0,0 +1,1 @@\n+x\n'),
        ('empty-hunk', {'a.c':''}, '--- a/a.c\n+++ b/a.c\n')]:
        refuses(name, lambda source=source, change=change: replay(source, change))
    entry = json.loads((ROOT/'platform/xbox/runtime-dependencies.json').read_text())['archives']['pbkit']
    data = checked(args.pbkit_archive, entry['sha256'])
    require(archive_members(data) == entry['members'], 'Member comparison failed'); results.append('ordered-members')
    refuses('hash-mismatch', lambda: checked(args.pbkit_archive, '0'*64))
    def member(name, payload):
        header = f'{name:<16}{0:<12}{0:<6}{0:<6}{0:<8}{len(payload):<10}`\n'.encode()
        return header + payload + (b'\n' if len(payload) % 2 else b'')
    for label, table, offset in [('coff', b'first.obj\0second.obj\0', 10), ('gnu', b'first.obj/\nsecond.obj/\n', 11)]:
        sample = b'!<arch>\n' + member('//', table) + member('/0', b'one') + member('/'+str(offset), b'two')
        require(archive_members(sample) == [{'name':'first.obj','sha256':digest(b'one')},{'name':'second.obj','sha256':digest(b'two')}], 'Long-name parser: '+label)
        results.append(label+'-long-names')
    refuses('archive-magic', lambda: archive_members(b'not-ar'))
    refuses('archive-truncated', lambda: archive_members(data[:-1]))
    require(archive_members(data) != list(reversed(entry['members'])), 'Reordering missed'); results.append('member-order-mutation')
    selection = SimpleNamespace(pbkit_source=args.pbkit_source, gles_source=args.gles_source,
                                pbkit_archive=args.pbkit_archive, gles_archive=args.gles_archive, output=out/'bad-pair')
    bad = copy.copy(selection); bad.gles_archive = args.pbkit_archive
    refuses('mismatched-archive-pair', lambda: verify(bad)); require(not bad.output.exists(), 'Failure wrote output')
    bad = copy.copy(selection); bad.output = out
    refuses('existing-output', lambda: verify(bad))
    bad = copy.copy(selection); bad.output = ROOT/'forbidden-test-output'
    refuses('repository-output', lambda: verify(bad))
    bad = copy.copy(selection); bad.output = args.pbkit_source.resolve().parent/'forbidden-test-output'
    refuses('input-tree-output', lambda: verify(bad))
    cmake = (ROOT/'platform/xbox/CMakeLists.txt').read_text()
    start = cmake.index('set(NXDK_DIR "$ENV{NXDK_DIR}")')
    end = cmake.index('if(NOT DEFINED NXDK_GLES11_DIR', start)
    snippet = cmake[start:end]
    sdk = args.nxdk.resolve()
    original_link = 'target_link_libraries(rampage_xbox PRIVATE "${NXDK_DIR}/lib/libnxdk_automount_d.lib" GL GLESv1_CM "${NXDK_DIR}/lib/libSDL2.lib" "${NXDK_DIR}/lib/libpbkit.lib")'
    revised_link = original_link.replace('"${NXDK_DIR}/lib/libpbkit.lib"', '"${RAMPAGE_XBOX_PBKIT_LIBRARY}"')
    require(revised_link in cmake and 'add_subdirectory("${NXDK_GLES11_DIR}"' in cmake, 'Link selection scope changed')
    env = dict(os.environ, NXDK_DIR=str(sdk), TMPDIR=str(out), PYTHONDONTWRITEBYTECODE='1')
    for name, override, expected, success in [
        ('cmake-default', None, sdk/'lib/libpbkit.lib', True),
        ('cmake-explicit', args.pbkit_archive.resolve(), args.pbkit_archive.resolve(), True),
        ('cmake-nonexistent', out/'does-not-exist.lib', None, False)]:
        script = out/(name+'.cmake')
        text = snippet + 'get_property(kind CACHE RAMPAGE_XBOX_PBKIT_LIBRARY PROPERTY TYPE)\nif(NOT kind STREQUAL "FILEPATH")\n message(FATAL_ERROR "Wrong cache type")\nendif()\n'
        if expected:
            text += f'if(NOT RAMPAGE_XBOX_PBKIT_LIBRARY STREQUAL "{expected}")\n message(FATAL_ERROR "Wrong selected archive")\nendif()\n'
        script.write_text(text)
        command = [args.cmake] + ([f'-DRAMPAGE_XBOX_PBKIT_LIBRARY={override}'] if override else []) + ['-P', str(script)]
        result = subprocess.run(command, env=env, text=True, capture_output=True)
        (out/(name+'.log')).write_text(result.stdout+result.stderr)
        require((result.returncode == 0) == success, name)
        if not success: require('must name an existing pbkit archive' in result.stderr, 'Wrong rejection')
        results.append(name)
    (out/'results.json').write_text(json.dumps({'passed':results,'count':len(results),'cmake_scope':'Extracted selection block and exact link-operand source check only; not full configure/build.'},indent=2)+'\n')
    print('PASS:',len(results),'verifier/refusal/CMake checks')

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ['pbkit-source','gles-source','pbkit-archive','gles-archive','nxdk','output']:
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--cmake', default='cmake')
    main(parser.parse_args())
