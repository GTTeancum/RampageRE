#!/usr/bin/env python3
"""Extract tested source, without installing a candidate or reading Git/private files.

The reference specimens restore only old window ownership, main init placement,
and missing frame guards. Other logic comes from the same current source; this
is a differential ownership regression, not an independent game implementation.
"""
from pathlib import Path
import hashlib
import json
import re
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from city_status_extract import write_support


root, out = map(lambda p: Path(p).resolve(), sys.argv[1:3])
override = Path(sys.argv[3]).resolve() if len(sys.argv) > 3 else root / 'src'
test_dir = Path(__file__).resolve().parent
inputs = {}


def read(name):
    path = override / name
    if not path.is_file():
        path = root / 'src' / name
    inputs[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
    return path.read_text()


def function(source, name):
    match = re.search(r'^(?:static )?[^\n]*\b' + name + r'\([^;]*?\)\n\{', source, re.M)
    assert match, name
    start = match.start()
    opening = source.index('{', start)
    return source[start:closing_brace(source, opening) + 1]


def closing_brace(source, opening):
    depth, mode, i = 0, 'code', opening
    while i < len(source):
        c, pair = source[i], source[i:i + 2]
        if mode == 'line':
            if c == '\n':
                mode = 'code'
        elif mode == 'comment':
            if pair == '*/':
                mode = 'code'
                i += 1
        elif mode in ['"', "'"]:
            if c == '\\':
                i += 1
            elif c == mode:
                mode = 'code'
        elif pair == '//':
            mode = 'line'
            i += 1
        elif pair == '/*':
            mode = 'comment'
            i += 1
        elif c in ['"', "'"]:
            mode = c
        elif c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise AssertionError('unclosed brace')


def once(source, old, new):
    assert source.count(old) == 1, (old, source.count(old))
    return source.replace(old, new)


def write(name, content):
    (out / name).write_text(content)


if not (override/'city.c').is_file():
    raise SystemExit('SOURCE_DIR must include city.c for actual City status helpers')
main, moves, header, render, city = map(read, ['main.c', 'moves.c', 'moves.h', 'render_gl.c', 'city.c'])
write_support(override/'city.c', out)
if not re.search(r'^int moves_start\(', header, re.M):
    raise SystemExit('moves_lifetimes requires the int-return moves_start reservation API. '
                     'Before source promotion, set SOURCE_DIR to the reviewed candidate source directory.')

write('main-types.h', main[:main.index('/* gCommon.getPlayType()')])
for name in ['scene_construction_stop', 'world_load_failed', 'start_moves', 'extra_add', 'world_release', 'mon_in', 'mon_shake',
             'have_monster', 'mon_hp_max', 'mon_props', 'popup_free']:
    write(name.replace('_', '-') + '.inc' if name == 'popup_free' else name + '.inc',
          function(main, name) + '\n')

# Reference normal startup restores the pre-reservation initialization point.
# Failure handling is tested only against actual extracted source, never this reference.
reference = function(main, 'start_moves')
start = reference.index('    if (moves_start(&mo->run,')
end = closing_brace(reference, reference.index('{', start)) + 1
reference = reference[:start] + reference[end + 1:]
reference = once(reference, '    mo->is_ai = 0;\n',
                 '    mo->is_ai = 0;\n    b_start(&mo->run, &mo->graph, moves_find(&mo->graph, "Ground_Idle"));\n')
# Keep the current int return contract and sticky Scene guards. This reference
# only restores legacy reservation timing on healthy paths; its result is ignored.
reference = reference.replace('static int start_moves(', 'static int reference_start_moves(')
reference = reference.replace('moves_stop(', 'b_stop(')
write('baseline-start.c', reference + '\n')

build = function(main, 'build')
start = build.index('    for (int k = 0; k < v->nmon + (v->extra >= 0); k++)')
end = build.index('    if (v->extra >= 0 &&', start)
write('build-monsters.inc', 'static void build_monsters(Viewer *v)\n{\n' + build[start:end] + '}\n')
start = build.index('            if (rm >= 0 && extra_add(')
end = build.index('            if (getenv("RAMPAGE_TRACE"))', start)
write('build-extra.inc', 'static void build_extra(Viewer *v, int rm)\n{\n' +
      build[start:end] + '    game_created++;\n}\n')

shell_start = function(main, 'shell_start_game')
start = shell_start.index('{') + 1
end = shell_start.index('    const ShellGame *g = shell_game(v->shell);', start)
initial_guard = shell_start[start:end]
if initial_guard.strip() != 'if (scene_construction_stop((Viewer *)v)) return;':
    raise ValueError('Shell Start entry guard changed; review extraction')
write('shell-start-guard.inc', 'static void shell_start_probe(Viewer *v)\n{' +
      initial_guard + '    shell_start_count++; /* Test-only admitted boundary. */\n}\n')
shell = function(main, 'shell_enter')
write('shell-release.inc', 'static void shell_release(Viewer *v)\n' +
      shell[shell.index('{'):shell.index('    v->shell = shell_open')] + '}\n')

start = main.index('        game_next_block(&v);                         /* the game moved on to the next block */')
end = main.index('        if (w.wheel)', start)
frame = main[start:end]


def frame_wrapper(name, body):
    return ('static int ' + name + '(Viewer *vp, Window w)\n{\n'
            '    Viewer v=*vp; int passed=0;\n    do {\n' + body +
            '        passed=1;\n    } while(0);\n    *vp=v;return passed;\n}\n')


write('frame-guards.inc', frame_wrapper('frame_guards', frame))
unguarded = re.sub(r'^\s*if \(v\.exit_game\) continue;\n', '', frame, flags=re.M)
unguarded = unguarded.replace('i < ncities && !v.exit_game', 'i < ncities')
write('base-frame-guards.inc', frame_wrapper('unguarded_frame_guards', unguarded))

start = main.index('        if (v.exit_game) {                           /* back to the shell (--shell), or the viewer ends */')
end = main.index('        game_next_block(&v);', start)
write('exit-dispatch.inc', 'static int exit_dispatch(Viewer *vp, Window *wp)\n{\n'
      '    Viewer v=*vp; Window w=*wp; int passed=0, shot=0, settled=0; const char *script=NULL;\n'
      '    do {\n' + main[start:end] + '        passed=1;\n    } while(0);\n'
      '    *vp=v;*wp=w;return passed;\n}\n')
start = main.index('    } else if (open_city(&v, city, block, -1) != 0) {')
end = main.index('    } else if (v.ap)', start)
write('direct-start.inc', 'static int direct_start_check(int open_result, int exit_game)\n{\n'
      '    struct {int exit_game;} v={exit_game}; int exit_status=0;\n    if (open_result != 0) {\n' +
      main[start:end].split('\n', 1)[1] + '    }\n    return exit_status;\n}\n')
op = function(main, 'open_city')
if 'City *c = city_open_checked(&v->vol, CITIES[index], &load_failed);' not in op:
    raise ValueError('Expected checked-open staging contract')
write('checked-open.inc', op + '\n')
viewer = function(main, 'viewer_main')
last_status = viewer[viewer.index('    int ap_status = v.ap ? ap_close(v.ap, NULL) : 0;'):]
if not last_status.endswith('    return exit_status ? exit_status : ap_status;\n}'):
    raise ValueError('Final process status boundary changed')
write('final-status.inc', 'static int final_status(Viewer *vp, int exit_status)\n{\n    Viewer v=*vp;\n' + last_status + '\n')
write('open-return.inc', 'static int open_return(Viewer *v)\n{\n' + op[op.index('    build(v);'):] + '\n')
start = main.index('    popup_free(&v);\n    rnd_free(v.rnd);')
end = main.index('    sound_end();', start)
write('ordinary-teardown.inc', 'static void ordinary_teardown(Viewer *vp)\n{\n'
      '    Viewer v=*vp;\n' + main[start:end] + '    *vp=v;\n}\n')

types = re.search(r'^typedef struct \{ uint32_t name; float reg\[4\]; GLuint id; float scale\[2\]; \} RegionTex;'
                  r'.*?^struct Renderer \{.*?^\};', render, re.M | re.S).group()
buffers = re.search(r'^typedef void \(APIENTRY \*BufGen\).*?^static struct \{ int tried; BufGen gen; BufDel del; '
                    r'BufBind bind; BufData data; \} g_buf;', render, re.M | re.S).group()
write('renderer-types.inc', types + '\n' + buffers + '\n')
renderer_functions = ''
for name in ['drop_textures', 'drop_buffers', 'rnd_scene', 'rnd_free']:
    renderer_functions += function(render, name).replace('void rnd_scene(', 'static void renderer_scene(').replace(
        'void rnd_free(', 'static void renderer_free(') + '\n'
write('renderer-teardown.inc', renderer_functions)

runner = moves[moves.index('/* ---------------------------------------------------------------- input'):]
legacy_file = test_dir / 'legacy_windows.c'
inputs[str(legacy_file)] = hashlib.sha256(legacy_file.read_bytes()).hexdigest()
legacy = legacy_file.read_text()
reference_runner = runner
for name in ['enter', 'moves_start', 'moves_stop']:
    reference_runner = once(reference_runner, function(reference_runner, name), function(legacy, name))
api = ['#include "moves.h"']
for prefix, source, domain in [('b', reference_runner, 1), ('c', runner, 2)]:
    source = re.sub(r'\bmoves_(\w+)\b', lambda m: prefix + '_' + m[1], source)
    preamble = ('#include <math.h>\n#include <stdlib.h>\n#include <string.h>\n'
                '#include "plat.h"\n#include "moves.h"\n#include "probe.h"\n')
    preamble += (f'#define calloc(n,s) audit_calloc({domain}, (n), (s))\n'
                 f'#define free(p) audit_free({domain}, (p))\n')
    write(prefix + '-runner.c', preamble + source)
    for declaration in re.findall(r'^(?:void|int) [bc]_\w+\([^;]*?\)\n\{', source, re.M):
        api.append(declaration[:-2] + ';')
write('runner-api.h', '\n'.join(api) + '\n')

headers = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in (root / 'src').glob('*.h')}
manifest = {'inputs': inputs, 'repository_headers': headers, 'source_directory': str(override),
            'oracle': 'Current logic with only legacy ownership/init placement and absent frame guards restored.',
            'fragments': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir() if p.is_file()}}
write('source-manifest.json', json.dumps(manifest, indent=2) + '\n')
