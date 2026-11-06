#!/usr/bin/env python3
"""Source-match frozen host regressions before reusing their sealed binaries.

Requires the external review inputs named by host-pins.json. No emulator,
compiler, archive copy or historical build script is invoked. Models do not
emulate GPU execution, native allocation/cleanup, or OS scheduling.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from verify_xbox_runtime import checked, digest, require, replay


def function(source, name):
    match = re.search(r'^\w[^\n]*\b' + name + r'\s*\([^;]*?\)\s*\{', source, re.M)
    require(match is not None, f'Function missing: {name}')
    depth = 1
    for end in range(match.end(), len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if not depth:
            return source[match.start():end + 1]
    raise ValueError(f'Unbalanced function: {name}')


def instrument(source):
    replacements = [
        ('pb_vbl_counter++;', 'pb_vbl_counter++; hook_counter();'),
        ('next=pb_BackBufferNxtVBL;', 'next=pb_BackBufferNxtVBL; validate_slot(next);'),
        ('next=pb_BackBufferNxt;', 'next=pb_BackBufferNxt; validate_slot(next);'),
        ('pb_BackBufferbReady[next]=0;', 'hook_before_clear(); pb_BackBufferbReady[next]=0; hook_consume(next);'),
        ('pb_BackBufferbReady[next]=1;', 'pb_BackBufferbReady[next]=1; hook_complete(next);'),
        ('pb_BackBufferNxtVBL=next;', 'pb_BackBufferNxtVBL=next; hook_advance();'),
        ('pb_FBAddr[pb_BackBufferIndex[next]]', 'read_fb(pb_BackBufferIndex[next])'),
        ('pb_FBAddr[buffer_index]', 'read_fb(buffer_index)'),
        ('pb_FBAddr[pb_BackBufferIndex[pb_BackBufferNxtVBL]]', 'read_fb(pb_BackBufferIndex[pb_BackBufferNxtVBL])')]
    for old, new in replacements: source = source.replace(old, new)
    lines = []
    for line in source.splitlines(True):
        code, marker, comment = line.partition('//')
        code = re.sub(r'VIDEOREG(8?)\((\w+)\)\|=([^;\n]+);', lambda m: f'write_reg({m[2]}, read_reg({m[2]})|({m[3]}));', code)
        code = re.sub(r'VIDEOREG(8?)\((\w+)\)=(?!=)([^;\n]+);', lambda m: f'write_reg({m[2]}, ({m[3]}));', code)
        lines.append(code + marker + comment)
    return ''.join(lines)


def hal_extract(source, candidate):
    code = source[source.index('static volatile unsigned int analogBufferCount;'):source.index('//DPCs avoid crashes')]
    for name in ['DPC', 'ISR', 'XAudioPlay', 'XAudioPause']:
        code += function(source, name) + '\n'
    if candidate:
        code += source[source.index('typedef struct AUDIO_SUBMISSION'):source.index('/* KeSynchronizeExecution')]
        code += function(source, 'PublishSamples') + '\n'
    code += function(source, 'XAudioProvideSamples') + '\n'
    code = re.sub(r'pb\[(0x[0-9A-Fa-f]+)\]\s*=\s*([^;]+);', r'mmio_write(\1, \2);', code)
    code = re.sub(r'pb\[(0x[0-9A-Fa-f]+)\]', r'mmio_read(\1)', code)
    out = []; points = []; publishing = False
    for line in code.splitlines():
        if 'static BOOLEAN __stdcall PublishSamples(' in line or 'void XAudioProvideSamples(' in line:
            publishing = True
        out.append(line)
        if publishing and any(x in line for x in ('pac97device->pcm', 'BufferCount++', 'pac97device->nextDescriptor =', 'mmio_write(')):
            points.append(line.strip()); out.append(f'    test_step({len(points)});')
        if publishing and line == '}': publishing = False
    return '\n'.join(out) + '\n', points


def run(args):
    out = args.output.resolve()
    require(not out.exists() and not out.is_relative_to(ROOT), 'Output must be new and outside repository')
    pins = json.loads((Path(__file__).parent / 'host-pins.json').read_text())
    bases = {'graphics': args.graphics.resolve(), 'audio': args.audio.resolve(), 'nxdk': args.nxdk.resolve()}
    require(all(not out.is_relative_to(base) for base in bases.values()), 'Keep output outside reference dependencies')
    for group, entries in pins['inputs'].items():
        for name, expected in entries.items(): checked(bases[group] / name, expected)
    dependencies = json.loads((ROOT / 'platform/xbox/runtime-dependencies.json').read_text())
    final = {}
    for name, expected in dependencies['final_sources'].items():
        final[name] = checked(args.verified_sources / name, expected).decode()
    pb, guard, gles = (final[x] for x in ('lib/pbkit/pbkit.c', 'lib/pbkit/pb_reset_guard.h', 'nv2a_helper.c'))
    hal = checked(ROOT / 'src/audio_xbox_hal.c', dependencies['audio_hal_sha256']).decode()
    out.mkdir()
    comparisons = {}

    def same(group, name, text):
        reference = bases[group] / 'host-tests' / name
        require(reference.read_text() == text, f'Fresh extraction differs: {group}/{name}')
        target = out / group / name; target.parent.mkdir(parents=True, exist_ok=True)
        with target.open('x') as stream: stream.write(text)
        comparisons[group + '/' + name] = digest(text.encode())

    same('graphics', 'actual_guard_host.h', re.sub(r'^#include <(?:winapi/fileapi.h|winapi/handleapi.h|nxdk/mount.h)>\n', '', guard, flags=re.M))
    raw = function(gles, 'glFlipNV2A')
    same('graphics', 'glFlipNV2A-extracted.c', raw + '\n')
    helpers = (bases['graphics'] / 'host-tests/caller_helpers.h').read_text()
    prefix = helpers[:helpers.index('void tested_glFlipNV2A')]
    same('graphics', 'caller_helpers.h', prefix + raw.replace('glFlipNV2A()', 'tested_glFlipNV2A()').replace('pb_reset_drain_guard(', 'observed_guard(') + '\n')
    for name, filename in [('pb_init', 'pb_init-extracted.c'), ('pb_kill', 'pb_kill-extracted.c'), ('pb_shutdown_notification_routine', 'shutdown-extracted.c')]:
        same('graphics', filename, function(pb, name) + '\n')
    init, kill = function(pb, 'pb_init'), function(pb, 'pb_kill')
    gates = (bases['graphics'] / 'host-tests/lifecycle_gates.h').read_text()
    snippets = [
        (init, '    if (pb_running) return -8;\n    if (!rg_lifecycle_init_begin()) return -8;'),
        (init, '    if (!rg_lifecycle_init_complete()) return -8;\n    return 0;'),
        (kill, '    rg_lifecycle_kill_begin();\n    HalRegisterShutdownNotification(&pb_shutdown_registration, FALSE);'),
        (kill, '    NtClose(pb_VBlankEvent);\n    rg_lifecycle_kill_complete();')]
    for source, snippet in snippets:
        require(source.count(snippet) == 1 and gates.count(snippet) == 1, 'Lifecycle gate mismatch')
    require(init.index(snippets[0][1]) < init.index('//reset global vars'), 'Init entry order')
    require(init.index(snippets[1][1]) > init.index('HalRegisterShutdownNotification(&pb_shutdown_registration, TRUE);'), 'Init completion order')
    require(kill.index(snippets[2][1]) < kill.index('if (pb_Put)') and kill.index(snippets[3][1]) > kill.index('pb_running=0;'), 'Kill boundary order')
    require('    rg_lifecycle_system_shutdown();\n\tpb_kill();' in function(pb, 'pb_shutdown_notification_routine'), 'Shutdown order')
    clean = pb.replace('/* Private trace-free reset safety, after native state declarations. */\n#include "pb_reset_guard.h"\n\n', '')
    for line in ['    if (!rg_lifecycle_init_begin()) return -8;\n', '    if (!rg_lifecycle_init_complete()) return -8;\n', '    rg_lifecycle_kill_begin();\n', '    rg_lifecycle_kill_complete();\n', '    rg_lifecycle_system_shutdown();\n']:
        require(clean.count(line) == 1, 'Lifecycle hook count'); clean = clean.replace(line, '')
    require(clean == (bases['graphics'] / 'inputs/pbkit.startup-fallback.c').read_text(), 'Unexpected native lifecycle delta')
    same('graphics', 'lifecycle_gates.h', gates)
    names = ['pb_vbl_handler', 'pb_subprog', 'DPC', 'ISR', 'pb_wait_until_gr_not_busy']
    for label, source in [('baseline', (bases['graphics'] / 'inputs/pbkit.baseline.c').read_text()), ('candidate', pb)]:
        code = '\n'.join(function(source, name) + '\n' for name in names)
        same('graphics', f'fallback/{label}.extracted.inc', code)
        prefix = ''.join(f'#define {name} {label}_{name}\n' for name in names)
        suffix = ''.join(f'#undef {name}\n' for name in names)
        same('graphics', f'fallback/{label}.host.inc', prefix + instrument(code) + suffix)
    pieces = []
    for name in ['pb_start', 'pb_jump_to_head', 'pb_busy', 'pb_reset', 'pb_end']:
        code = re.sub(r'/\*.*?\*/|//[^\n]*', '', function(pb, name), flags=re.S)
        code = code.replace('*(pb_DmaUserAddr+0x40/4)=((DWORD)pb_Put)&0x03FFFFFF;', 'model_publish(model_virtual(pb_Put)&0x03FFFFFF);').replace('*(pb_DmaUserAddr+0x44/4)', 'model_get()')
        code = code.replace('(DWORD)pb_Put', 'model_virtual(pb_Put)').replace('(DWORD)pb_Head', 'model_virtual(pb_Head)')
        code = code.replace('pGetAddr=(uint32_t *)((model_get())|0x80000000);', 'pGetAddr=model_decode(model_get());')
        pieces.append(code)
    same('graphics', 'reset-model/extracted.inc', '\n\n'.join(pieces) + '\n')
    loop = next(line for line in init.splitlines() if 'for(i=' in line and 'pb_BackBufferbReady[i]=0;' in line)
    require('pb_BackBufferbReady[3]' in pb, 'Readiness extent changed')
    startup = (bases['graphics'] / 'host-tests/startup/fixed.c').read_text()
    startup = re.sub(r'^    for\(i=.*pb_BackBufferbReady\[i\]=0;$', lambda _: loop, startup, flags=re.M)
    same('graphics', 'startup/fixed.c', startup)
    baseline = (bases['graphics'] / 'inputs/pbkit.baseline.c').read_text()
    old_loop = next(line for line in function(baseline, 'pb_init').splitlines() if 'for(i=' in line and 'pb_BackBufferbReady[i]=0;' in line)
    old_test = (bases['graphics'] / 'host-tests/startup/baseline.c').read_text()
    same('graphics', 'startup/baseline.c', re.sub(r'^    for\(i=.*pb_BackBufferbReady\[i\]=0;$', lambda _: old_loop, old_test, flags=re.M))
    formatter = (bases['graphics'] / 'host-tests/formatter/formatter_test.c').read_text()
    prefix = formatter[:formatter.index('/* actual_pdclib_print')]
    suffix = formatter[formatter.index('\nint main(void)'):]
    fmt = re.search(r'snprintf\(rg_failure_line,sizeof rg_failure_line,("(?:[^"\\]|\\.)*")', guard)[1]
    require('const char*f=' + fmt + ';' in suffix, 'Formatter not testing actual guard format')
    require('"20261106-tracefree-v1"' in guard and '"20261106-tracefree-v1"' in suffix, 'Build marker mismatch')
    pieces = []
    for name in ['_PDCLIB/_PDCLIB_print.c', 'stdio/vsnprintf.c', 'stdio/snprintf.c']:
        code = (bases['nxdk'] / 'lib/pdclib/functions' / name).read_text().split('#ifdef TEST')[0]
        code = '\n'.join(line for line in code.splitlines() if not line.startswith('#include'))
        code = re.sub(r'\bvsnprintf\b', 'actual_pdclib_vsnprintf', code)
        code = re.sub(r'\bsnprintf\b', 'actual_pdclib_snprintf', code).replace('_PDCLIB_print', 'actual_pdclib_print')
        pieces.append(code)
    same('graphics', 'formatter/formatter_test.c', prefix + '\n'.join(pieces) + suffix)
    ac97 = (bases['audio'] / 'reference/ac97.c').read_text()
    same('audio', 'xemu-extracted.inc', '\n'.join(function(ac97, name) + '\n' for name in ['fetch_bd', 'update_sr', 'voice_set_active', 'reset_bm_regs', 'nabm_writeb', 'transfer_audio']))
    constants = ac97[ac97.index('#define SR_FIFOE'):ac97.index('/* Buffer Descriptor */')] + ac97[ac97.index('#define BD_IOC'):ac97.index('#define REC_MASK')]
    constants = constants.replace('(1 << 31)', '(1u << 31)') + '\nenum { BUP_SET=1, BUP_LAST=2 };\n'
    constants += ac97[ac97.index('#define MKREGS'):ac97.index('typedef struct AC97DeviceState')]
    same('audio', 'xemu-constants.inc', constants)
    for arm, source in [('baseline', (bases['audio'] / 'inputs/audio_xbox_hal.plain.c').read_text()), ('candidate', hal)]:
        code, points = hal_extract(source, arm == 'candidate')
        same('audio', f'hal-{arm}.inc', code)
        require(points == json.loads((bases['audio'] / f'host-tests/{arm}-schedule-points.json').read_text()), 'HAL schedule points changed')
    # Only after every source and stub is matched may frozen binaries execute.
    results = []; env = dict(os.environ, TMPDIR=str(out), PYTHONDONTWRITEBYTECODE='1', ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    for test in pins['runs']:
        binary = bases[test['group']] / test['path']
        result = subprocess.run([str(binary)], capture_output=True, text=True, env=env)
        log = result.stdout + result.stderr
        with (out / (test['name'] + '.log')).open('x') as stream: stream.write(log)
        if test.get('negative'):
            require(result.returncode != 0 and 'index 3 out of bounds' in log, 'Startup negative control did not fail as expected')
        else:
            require(result.returncode == 0 and test['marker'] in log, f"Regression failed: {test['name']}: {log}")
        results.append({'name': test['name'], 'returncode': result.returncode, 'binary_sha256': digest(binary.read_bytes()), 'passed': True})
        print('PASS:', test['name'])
    report = {'fresh_extractions_equal_frozen_inputs': comparisons, 'runs': results, 'limits': __doc__}
    with (out / 'results.json').open('x') as stream: json.dump(report, stream, indent=2); stream.write('\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['graphics', 'audio', 'nxdk', 'verified-sources', 'output']:
        parser.add_argument('--' + name, type=Path, required=True)
    try: run(parser.parse_args())
    except (ValueError, OSError, KeyError) as error: parser.exit(1, f'Refused: {error}\n')
