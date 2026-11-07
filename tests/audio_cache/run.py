#!/usr/bin/env python3
"""Source-derived Xbox PCM ownership and exact Wii allocation regression."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

OLD = '    if (count > INT32_MAX || sample_alloc(s, sizeof(int16_t) * (size_t)count) != 0)'
NEW = '''    /* Nibble addresses include each frame's two header nibbles, which the
     * decoder below skips. Allocate only the PCM payload, including partial
     * first/last frames; keep 64-bit endpoints for start + count. */
    uint64_t end = (uint64_t)start + count;
    uint64_t before = (uint64_t)start / 16 * 14 + (start % 16 > 2 ? start % 16 - 2 : 0);
    uint64_t after = end / 16 * 14 + (end % 16 > 2 ? end % 16 - 2 : 0);
    if (count > INT32_MAX || sample_alloc(s, sizeof(int16_t) * (size_t)(after - before)) != 0)'''

def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='New evidence directory; existing paths are refused')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    src = here.parent.parent / 'src'
    out = args.output.resolve()
    out.mkdir(parents=False, exist_ok=False)
    tmp = out / 'compiler-tmp'
    tmp.mkdir()
    source = src / 'sound.c'
    text = source.read_text()
    assert text.count(NEW) == 1, 'Sizing hunk changed; review the control reconstruction before updating this test'
    manifest = {'source': str(source), 'source_sha256': digest(source), 'substitutions': []}
    def replace(t, name, pattern, replacement):
        matches = list(re.finditer(pattern, t, re.M | re.S))
        assert len(matches) == 1, (name, len(matches))
        m = matches[0]
        manifest['substitutions'].append({'name': name, 'original': m.group(), 'replacement': replacement})
        return t[:m.start()] + replacement + t[m.end():]
    text = replace(text, 'platform declarations', r'^#include "plat.h"', '#include "sound_platform_stub.h"')
    text = replace(text, 'SDL declarations', r'^#include <SDL.h>', '#include "sound_sdl_stub.h"')
    at = text.index("/* SDL2's audio queue")
    tail = replace(text[at:], 'unexercised SDL device startup', r'^int sound_start\(void\)\n\{.*?^\}', 'int sound_start(void) { return -1; }')
    tail = replace(tail, 'unexercised SDL device shutdown', r'^void sound_end\(void\)\n\{.*?^\}', 'void sound_end(void) {}')
    current = text[:at] + tail
    (out / 'sound.current.inc').write_text(current)
    (out / 'sound.base.inc').write_text(current.replace(NEW, OLD))
    manifest['control_change'] = {'current': NEW, 'control': OLD}
    inputs = [source, src / 'sound.h', src / 'data.h', src / 'xbox_audio_codec.h', src / 'xbox_mix_headroom.h']
    inputs += [here / n for n in ['run.py', 'tracker.h', 'cache.c', 'variant.c', 'paired.c']]
    for n in ['sound_platform_stub.h', 'sound_sdl_stub.h']:
        p = here.parent / 'render_output_refill' / n
        inputs.append(p)
        (out / n).write_bytes(p.read_bytes())
    manifest['input_hashes'] = {str(p): digest(p) for p in inputs}
    (out / 'cache.current.c').write_text('#define SOUND_SOURCE "sound.current.inc"\n#include "cache.c"\n')
    names = set(re.findall(r'^(?:void|int|double|float|size_t|const char \*)\s*(\w+)\(', current, re.M))
    names |= {'main', 'vol_find', 'vol_read', 'vol_read_wii', 'vol_read_range', 'SDL_PauseAudioDevice',
              'SDL_GetQueuedAudioSize', 'SDL_QueueAudio', 'SDL_ClearQueuedAudio', 'SDL_GetError', 'rampage_audio_pending_frames'}
    for variant, selected in [('base', 'base'), ('candidate', 'current')]:
        code = '\n'.join(f'#define {n} {variant}_{n}' for n in sorted(names))
        code += f'\n#define SOUND_SOURCE "sound.{selected}.inc"\n#define API(n) {variant}_##n\n#include "variant.c"\n'
        (out / (variant + '.c')).write_text(code)
    commands = []
    env = dict(os.environ, TMPDIR=str(tmp))
    def run(label, cmd, environment):
        cmd = list(map(str, cmd))
        commands.append({'label': label, 'command': cmd, 'TMPDIR': environment['TMPDIR'],
                         'ASAN_OPTIONS': environment.get('ASAN_OPTIONS'), 'UBSAN_OPTIONS': environment.get('UBSAN_OPTIONS')})
        with (out / (label + '.log')).open('x') as log:
            result = subprocess.run(cmd, cwd=out, env=environment, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode or label.endswith('-run'):
            print((out / (label + '.log')).read_text(), end='')
        if result.returncode:
            raise subprocess.CalledProcessError(result.returncode, cmd)
    try:
        for mode in ['normal'] + (['sanitize'] if args.sanitize else []):
            flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if mode == 'sanitize' else []
            runenv = dict(env, ASAN_OPTIONS=env.get('ASAN_OPTIONS', 'detect_leaks=0:halt_on_error=1'),
                          UBSAN_OPTIONS=env.get('UBSAN_OPTIONS', 'halt_on_error=1')) if mode == 'sanitize' else env
            for test, files in [('cache', [out / 'cache.current.c']),
                                ('paired', [out / 'base.c', out / 'candidate.c', here / 'paired.c'])]:
                binary = out / (test + '.' + mode)
                cmd = [os.environ.get('CC', 'cc'), '-pipe', '-std=gnu11', '-D_GNU_SOURCE', '-O1',
                       '-ffunction-sections', '-fdata-sections', *flags, '-I' + str(out), '-I' + str(here),
                       '-I' + str(src), *files, '-Wl,--gc-sections', '-lm', '-o', binary]
                run(test + '-' + mode + '-compile', cmd, env)
                run(test + '-' + mode + '-run', [binary], runenv)
        assert all(digest(Path(p)) == h for p, h in manifest['input_hashes'].items()), 'Input changed during tests'
        manifest['passed'] = True
        manifest['allocation_scope'] = 'Exact requested live allocations in tested sound code; excludes SDL, libc, kernel, GPU and allocator retention'
    finally:
        manifest['generated_hashes'] = {str(p.name): digest(p) for p in out.iterdir() if p.is_file()}
        (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    print('Retained test evidence:', out)

if __name__ == '__main__':
    main()
