#!/usr/bin/env python3
import argparse, hashlib, json, re
from pathlib import Path
p=argparse.ArgumentParser(); p.add_argument('repo',type=Path); p.add_argument('output',type=Path); p.add_argument('--candidate',action='store_true',help='Apply the exact pending fix only to generated test source')
a=p.parse_args(); out=a.output; out.mkdir(parents=True,exist_ok=True)
sources={f:(a.repo/'src'/f).read_text() for f in ['army.c','army.h','anim.c']}
(out/'input_hashes.txt').write_text(''.join(hashlib.sha256((a.repo/'src'/f).read_bytes()).hexdigest()+'  '+f+'\n' for f in sources))
def fn(s,sig):
    start=s.index(sig); pos=s.index('{',start); depth=1; end=pos+1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}'); end+=1
    return s[start:end]+'\n'
# Extract the real callback contract rather than declaring a permanently healthy stub.
header = sources['army.h']
host_start = header.index('typedef struct {', header.index('/* ---- what the module asks of its host */'))
host_end = header.index('} ArmyHost;', host_start) + len('} ArmyHost;')
host_decl = header[host_start:host_end] + '\n'
host_stop = fn(sources['army.c'], 'static int host_stopped(')
assert 'int (*failed)(void *ctx);' in host_decl
assert 'ArmyHost host;' in fn(sources['army.c'], 'struct Army {')
caller=fn(sources['army.c'],'void army_hold(')
# Preserve a two-dimensional caller only as text for generating a defined flat-index reference.
original_caller = caller
changes = [('    float axis[3][3];\n    memcpy(axis[major], hand + 8, sizeof axis[0]);\n    memcpy(axis[i1], hand + 4, sizeof axis[0]);\n    axis[i2][0] = axis[major][1] * axis[i1][2] - axis[major][2] * axis[i1][1];\n    axis[i2][1] = axis[major][2] * axis[i1][0] - axis[major][0] * axis[i1][2];\n    axis[i2][2] = axis[major][0] * axis[i1][1] - axis[major][1] * axis[i1][0];', '    float axis[9];\n    float *am = axis + 3 * major, *a1 = axis + 3 * i1, *a2 = axis + 3 * i2;\n    memcpy(am, hand + 8, 3 * sizeof(float));\n    memcpy(a1, hand + 4, 3 * sizeof(float));\n    a2[0] = am[1] * a1[2] - am[2] * a1[1];\n    a2[1] = am[2] * a1[0] - am[0] * a1[2];\n    a2[2] = am[0] * a1[1] - am[1] * a1[0];'), ('    army_hold_blend(&u->hold, axis[0], pos, dt);', '    army_hold_blend(&u->hold, axis, pos, dt);'), ('        memcpy(u->world + 4 * c, axis[c], sizeof axis[c]);', '        memcpy(u->world + 4 * c, axis + 3 * c, 3 * sizeof(float));')]
if a.candidate:
    for before, after in changes:
        assert caller.count(before) == 1, 'Candidate mode requires original call shape'
        caller = caller.replace(before, after)
else:
    for before, after in changes:
        assert caller.count(after) == 1 and before not in caller, 'Production must contain reviewed flat-array shape'
        original_caller = original_caller.replace(after, before)
ref=original_caller.replace('void army_hold(', 'void reference_hold(')
ref=ref.replace('float axis[3][3];','float flat[9];')
ref=re.sub(r'sizeof axis\[[^\]]+\]', '3 * sizeof(float)',ref)
ref=re.sub(r'axis\[([^\]]+)\]\[([^\]]+)\]',r'flat[3 * (\1) + (\2)]',ref)
ref=re.sub(r'axis\[([^\]]+)\]',r'(flat + 3 * (\1))',ref)
assert 'axis' not in ref
head='''#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
'''
head+=next(l for l in sources['army.h'].splitlines() if l.startswith('typedef struct { float blend'))+'\n'
head+=fn(sources['anim.c'],'void quat_slerp(')
for sig in ['static void rot_quat(', 'static void quat_rot(', 'static void quat_mul(', 'void army_hold_blend_start(', 'void army_hold_blend(']: head+=fn(sources['army.c'],sig)
# Minimal template stubs, exercising caller control flow without full game services.
head+='''
typedef struct { int at; float v[3]; } Group;
typedef float Record;
typedef struct { int major, count, present; float off[2][3]; } Template;
typedef struct ArmyUnit ArmyUnit;
''' + host_decl + '''
/* Only fields used by army_hold are modeled; this is not the production Army/Unit ABI. */
struct ArmyUnit { uint32_t pre[4]; int phase; Template *tmpl; ArmyHoldBlend hold; float world[16], pos[3]; uint32_t post[4]; };
typedef struct { ArmyHost host; int n; ArmyUnit *u; } Army;
static unsigned template_reads;
enum { AU_HELD = 2 };
static float tnum(ArmyUnit *u, const char *key, float fallback) { template_reads++; (void)key; (void)fallback; return (float)u->tmpl->major; }
static int item_count;
static Group group_sub(Template *t,const char *name) { template_reads++; Group g={0,{0,0,0}}; g.at=t->present; memcpy(g.v,t->off[!strcmp(name,"MirroredHoldOffset")],sizeof g.v); item_count=t->count; return g; }
static int group_items(Group g,Record r[3],int cap) { template_reads++; (void)cap; memcpy(r,g.v,sizeof g.v); return item_count; }
static float record_num(Record r) { template_reads++; return r; }
'''
(out/'source.h').write_text(head+host_stop+caller+ref)
# Keep exact extraction pins alongside the retained generated source and input hashes.
segments = {'ArmyHost': ('army.h', host_decl.rstrip('\n')),
            'host_stopped': ('army.c', host_stop.rstrip('\n')),
            'army_hold': ('army.c', fn(sources['army.c'], 'void army_hold(').rstrip('\n'))}
manifest = {}
for name, (file, body) in segments.items():
    offset = sources[file].index(body)
    manifest[name] = {'source': file, 'line': sources[file][:offset].count('\n') + 1,
                      'sha256': hashlib.sha256(body.encode()).hexdigest()}
manifest['generated_source_sha256'] = hashlib.sha256((out/'source.h').read_bytes()).hexdigest()
(out/'extraction-pins.json').write_text(json.dumps(manifest, indent=2) + '\n')
