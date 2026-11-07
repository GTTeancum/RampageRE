#!/usr/bin/env python3
"""Extract the narrow City ownership paths, not copies of their implementations."""
from pathlib import Path
import hashlib, json, re, sys, textwrap
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from city_status_extract import write_support

city, mesh, output = map(Path, sys.argv[1:])
write_support(city, output.parent)
raw = city.read_text()
# Private fault-injection candidates may carry source-location directives.
s = re.sub(r'^#line .*\n', '', raw, flags=re.M)
m = mesh.read_text()
def section(text, first, last):
    if text.count(first) != 1:
        raise SystemExit(f'Missing/ambiguous expected source boundary: {first!r}')
    start = text.index(first)
    return text[start:text.index(last, start)]
if 'static int facade(' not in s:
    raise SystemExit('The checked City facade contract is absent. Apply the pending narrow patch, or set CITY_SOURCE to its private candidate.')
facade = section(s, 'static int facade(', '/* the sheet\'s ColumnDef| outlines')
growth = section(s, '        Mesh fm = { 0 };', '        Group sg = template_of(c, sheet);')
build = section(s, 'int city_build(', 'void city_build_left(')
prefix = build[:build.index('    char key[128];')]
failure = build[build.index('failed:'):]
cleanup = section(s, 'static void skin_free(', 'static void *scene_vector_grow') + s[s.index('void scene_free(Scene *s)'):]
free_mesh = section(m, 'void mesh_free(Mesh *m)', 'uint32_t material_texture(')
# The wrapper uses the production facade call, growth, and failed local cleanup.
wrapper = '''static int facade_build(Mesh *out, int width, int depth, int nst)\n{\n    int sheet_name = 7;\n    float z[2] = { 6, 9 };\n    Record tiles[2][64] = { 0 };\n    uint32_t ntiles[2] = { (uint32_t)(width + 2 * depth), (uint32_t)(width + 2 * depth) };\n    for (int j = 0; j < 2; j++)\n        for (int k = 0; k < 64; k++) tiles[j][k].value = (uint32_t)(k % 4);\n'''+textwrap.indent(textwrap.dedent(growth), '    ')+'''    *out = fm;\n    return 0;\n}\n'''
# Stop after the actual scene-map initialization; construction of other resources
# is deliberately outside this unit. On failure use the actual failed tail and
# complete production scene_free, rather than a fake destructor.
prefix = prefix.replace('int city_build(', 'static int map_build(')
prefix += '    (void)block_name; (void)night; (void)crowd_sound;\n    return 0;\n' + failure
output.write_text(free_mesh + '\n' + facade + '\n' + wrapper + '\n' + prefix + '\n' + cleanup)
output.with_suffix('.json').write_text(json.dumps({
    'city_source': str(city.resolve()), 'city_sha256': hashlib.sha256(city.read_bytes()).hexdigest(),
    'mesh_source': str(mesh.resolve()), 'mesh_sha256': hashlib.sha256(mesh.read_bytes()).hexdigest(),
    'extracts': ['facade', 'building facade construction/growth', 'city_build map prefix and failed tail', 'scene_free', 'mesh_free'],
    'limitations': ['Synthetic source-extracted units, not complete city_build execution. Full-source real-asset comparison is separate private evidence.']
}, indent=2) + '\n')
