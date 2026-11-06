#!/usr/bin/env python3
"""Extract current production code; replace only host-inaccessible boundaries.

No callback, throttle, queue, mixer, event-state or Renderer model is copied into
this directory. Exact fragments and all host substitutions are recorded with
source line ranges and hashes. Unexpected source layouts fail explicitly.
"""
import hashlib
import json
import re
import sys
from pathlib import Path

if len(sys.argv) != 3:
    raise SystemExit("usage: extract_sources.py SOURCE_DIRECTORY OUTPUT_DIRECTORY")
src = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)
manifest = {"sources": {}, "extracted_spans": [], "host_substitutions": []}


def digest(text):
    return hashlib.sha256(text.encode()).hexdigest()


def read(name):
    path = src / name
    text = path.read_text()
    manifest["sources"][name] = {"path": str(path), "sha256": digest(text)}
    return text


def one(name, text, pattern):
    matches = list(re.finditer(pattern, text, re.M | re.S))
    if len(matches) != 1:
        raise SystemExit(f"Cannot identify {name}: expected one match, found {len(matches)}. "
                         "Review source changes before updating this focused test.")
    return matches[0]


def record(name, filename, text, match):
    code = match.group(0)
    line = text.count("\n", 0, match.start()) + 1
    return {"name": name, "source": filename, "start_line": line,
            "end_line": line + code.count("\n"), "sha256": digest(code)}


renderer = read("render_gl.c")
read("render_gl.h")
read("sound.h")
read("xbox_mix_headroom.h")
read("xbox_audio_codec.h")
patterns = [
    ("Renderer and private types",
     r"^typedef struct \{ uint32_t name; float reg\[4\]; GLuint id; float scale\[2\]; \} RegionTex;.*?^struct Renderer \{.*?^\};"),
    ("rnd_create", r"^Renderer \*rnd_create\(void\) \{[^\n]*\}"),
    ("rnd_set_output_pump", r"^void rnd_set_output_pump\(Renderer \*r, void \(\*pump\)\(void\)\)\n\{.*?^\}"),
    ("pump_output", r"^static void pump_output\(Renderer \*r\)\n\{.*?^\}"),
]
parts = ["""#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "render_gl.h"
/* The extracted private renderer types require only this GL boundary typedef. */
typedef unsigned int GLuint;
"""]
for name, pattern in patterns:
    match = one(name, renderer, pattern)
    info = record(name, "render_gl.c", renderer, match)
    manifest["extracted_spans"].append(info)
    parts.append('#line %d %s\n%s\n' %
                 (info["start_line"], json.dumps(str(src / "render_gl.c")), match.group(0)))
(out / "renderer.inc").write_text("\n".join(parts))
(out / "helper.c").write_text('#include "renderer.inc"\n#include "helper_regression.c"\n')

sound = read("sound.c")
# Remove only OS declarations and the unopened device's startup/shutdown bodies.
# Queue feeding, the ready/open guard, mixing and event functions stay exact.
substitutions = [
    ("platform declarations", r'^#include "plat.h"', '#include "sound_platform_stub.h"'),
    ("SDL declarations", r'^#include <SDL.h>', '#include "sound_sdl_stub.h"'),
]
changes = []
for name, pattern, replacement in substitutions:
    match = one(name, sound, pattern)
    changes.append((match.start(), match.end(), replacement))
    info = record(name, "sound.c", sound, match)
    info["replacement"] = replacement
    manifest["host_substitutions"].append(info)
# The Windows waveOut branch has its own functions; select only SDL's branch.
sdl_at = one("SDL queue branch", sound, r"^/\* SDL2's audio queue[^\n]*").start()
for name, signature, replacement in [
    ("SDL startup", "int sound_start", "int sound_start(void) { return -1; }"),
    ("SDL shutdown", "void sound_end", "void sound_end(void) {}"),
]:
    pattern = r"^" + signature + r"\(void\)\n\{.*?^\}"
    matches = [m for m in re.finditer(pattern, sound, re.M | re.S) if m.start() > sdl_at]
    if len(matches) != 1:
        raise SystemExit(f"Cannot identify {name}: expected one SDL definition.")
    match = matches[0]
    changes.append((match.start(), match.end(), replacement))
    info = record(name, "sound.c", sound, match)
    info["replacement"] = replacement
    manifest["host_substitutions"].append(info)

cursor = 0
pieces = []
for start, end, replacement in sorted(changes):
    if start < cursor:
        raise SystemExit("Overlapping host-boundary substitutions")
    if start > cursor:
        line = sound.count("\n", 0, cursor) + 1
        pieces.append('#line %d %s\n%s' %
                      (line, json.dumps(str(src / "sound.c")), sound[cursor:start]))
    pieces.append(replacement + "\n")
    cursor = end
line = sound.count("\n", 0, cursor) + 1
pieces.append('#line %d %s\n%s' %
              (line, json.dumps(str(src / "sound.c")), sound[cursor:]))
pieces.append('\n#include "renderer.inc"\n#include "queue_regression.c"\n')
(out / "queue.c").write_text("".join(pieces))
manifest["generated"] = {
    name: hashlib.sha256((out / name).read_bytes()).hexdigest()
    for name in ["renderer.inc", "helper.c", "queue.c"]
}
(out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
for name in ["render_gl.c", "sound.c"]:
    print(name, "SHA-256:", manifest["sources"][name]["sha256"])
