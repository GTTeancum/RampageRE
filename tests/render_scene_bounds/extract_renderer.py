#!/usr/bin/env python3
"""Compile exact renderer implementation fragments, not a reimplemented model.

Only GL typedefs and the two GL lifecycle calls used by these fragments are
stubbed, so a GPU and desktop GL headers are unnecessary. Public project types
come from the current checkout headers. Each extracted span is recorded with its
source line range and SHA-256; source bodies are never edited by this script.
"""
import hashlib
import json
import re
import sys
from pathlib import Path

if len(sys.argv) != 3:
    raise SystemExit("usage: extract_renderer.py SOURCE OUTPUT")
source = Path(sys.argv[1]).resolve()
output = Path(sys.argv[2]).resolve()
s = source.read_text()
spans = []

def take(name, pattern):
    matches = list(re.finditer(pattern, s, re.M | re.S))
    if len(matches) != 1:
        raise SystemExit(f"Cannot extract {name}: expected one definition, found {len(matches)}. "
                         "Review the renderer changes and update this focused test.")
    m = matches[0]
    code = m.group(0)
    line = s.count('\n', 0, m.start()) + 1
    spans.append({'name': name, 'start_line': line,
                  'end_line': line + code.count('\n'),
                  'sha256': hashlib.sha256(code.encode()).hexdigest()})
    return '#line %d %s\n%s\n' % (line, json.dumps(str(source)), code)

preamble = r'''#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render_gl.h"
/* GL-only boundary: no renderer algorithm or project structure is replaced. */
typedef unsigned int GLuint;
typedef unsigned int GLenum;
typedef int GLsizei;
#define APIENTRY
static unsigned test_finish_calls, test_delete_calls, test_realloc_calls;
static void glDeleteTextures(GLsizei n, const GLuint *ids)
{
    (void)n; (void)ids; ++test_delete_calls;
}
#ifdef RAMPAGE_XBOX
static void glFinish(void) { ++test_finish_calls; }
#endif
static void *test_realloc(void *p, size_t n)
{
    ++test_realloc_calls;
    return realloc(p, n);
}
#define realloc test_realloc
'''
fragments = [take('Renderer and private types', r'^typedef struct \{ uint32_t name; float reg\[4\]; GLuint id; float scale\[2\]; \} RegionTex;.*?^struct Renderer \{.*?^\};'),
             take('Buffer function types and g_buf', r'^typedef void \(APIENTRY \*BufGen\).*?^static struct \{ int tried; BufGen gen; BufDel del; BufBind bind; BufData data; \} g_buf;'),
             take('rnd_create', r'^Renderer \*rnd_create\(void\) \{[^\n]*\}'),
             take('drop_textures', r'^static void drop_textures\(Renderer \*r\)\n\{.*?^\}'),
             take('drop_buffers', r'^static void drop_buffers\(Renderer \*r\)\n\{.*?^\}'),
             take('rnd_scene', r'^void rnd_scene\(Renderer \*r, const Scene \*s\)\n\{.*?^\}'),
             take('rnd_free', r'^void rnd_free\(Renderer \*r\)\n\{.*?^\}'),
             take('item_seen', r'^static int item_seen\(Renderer \*r, const DrawItem \*it, const float mv\[16\], float tx, float ty\)\n\{.*?^\}')]
output.write_text(preamble + '\n'.join(fragments) + '\n#undef realloc\n#include "regression.c"\n')
output.with_suffix('.json').write_text(json.dumps({'source': str(source), 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(), 'spans': spans}, indent=2)+'\n')

print("Renderer source SHA-256:", hashlib.sha256(source.read_bytes()).hexdigest())
