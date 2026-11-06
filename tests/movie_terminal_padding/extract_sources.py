#!/usr/bin/env python3
"""Extract current movie audio functions and reconstruct the pre-fix control."""
from pathlib import Path
import hashlib,json

# The reference reverses only the reviewed terminal-frame change. Everything
# else comes from current movie.c, so unrelated future changes are not frozen.
REPLACEMENTS = [
('    int au_head, au_ch, au_rate, au_inter;\n',
 '    int au_head, au_ch, au_rate, au_inter;\n'
 '    int au_no_loop, au_ended[2];             /* per-movie, zeroed by movie_open */\n'),
('                m->au_inter = (int)rd32(p + 20);\n            }\n',
 '                m->au_inter = (int)rd32(p + 20);\n            }\n'
 '            /* Only the explicit non-looping SPU movie format is established.\n'
 '             * Leave short/unknown headers and looping streams as before. */\n'
 '            if (sz >= 24 && rd32(p + 8) == 0x10 &&\n'
 '                rd32(p + 24) == 0xFFFFFFFFu && rd32(p + 28) == 0xFFFFFFFFu)\n'
 '                m->au_no_loop = 1;\n'),
('        for (int k = 0; k < m->au_inter / 16; k++)\n'
 '            adpcm_frame(m->au + m->au_pos + (size_t)c * m->au_inter + 16 * k, m->au_pcm[c] + 28 * k,\n'
 '                        &m->au_hist[c][0], &m->au_hist[c][1]);\n',
 '        for (int k = 0; k < m->au_inter / 16; k++) {\n'
 '            const uint8_t *f = m->au + m->au_pos + (size_t)c * m->au_inter + 16 * k;\n'
 '            int16_t *out = m->au_pcm[c] + 28 * k;\n'
 '            if (m->au_ended[c]) {\n'
 '                /* Consume padding silently without changing block cadence. */\n'
 '                memset(out, 0, 28 * sizeof *out);\n'
 '            } else {\n'
 '                adpcm_frame(f, out, &m->au_hist[c][0], &m->au_hist[c][1]);\n'
 '                /* The end frame itself has 28 valid samples. Do not interpret\n'
 '                 * repeat/loop or reserved flag combinations as a stop. */\n'
 '                if (m->au_no_loop && f[1] == 1) m->au_ended[c] = 1;\n'
 '            }\n'
 '        }\n')]

def sha(text):return hashlib.sha256(text.encode()).hexdigest()

def extract(source,out):
    fixed=source.read_text();baseline=fixed
    for old,new in REPLACEMENTS:
        if baseline.count(new)!=1:raise RuntimeError('Terminal-fix source pattern changed; review before updating extraction')
        baseline=baseline.replace(new,old)
    with (out/'movie.baseline.c').open('x') as f:f.write(baseline)
    manifest={'current_source':str(source),'current_source_sha256':sha(fixed),
              'control_sha256':sha(baseline),'control_changes':'Only the three terminal-fix hunks reversed',
              'variants':{}}
    for name,s in [('baseline',baseline),('candidate',fixed)]:
        spans=[]
        def take(label,start,end,include_end=True):
            if s.count(start)!=1:raise RuntimeError('Missing or ambiguous source span: '+label)
            a=s.index(start);b=s.index(end,a)+(len(end) if include_end else 0);text=s[a:b]
            spans.append({'label':label,'first_line':s[:a].count('\n')+1,
                          'last_line':s[:b].count('\n')+1,'sha256':sha(text)})
            return text
        chunks=['#include <stdlib.h>\n#include <string.h>\n#include <math.h>\n#include "plat.h"\n#include "movie.h"\n',
                take('Frame/buffer limits/Movie','typedef struct { uint8_t *p[3]; } Frame;','    uint8_t *rgba;\n};'),
                take('getb/getn/grow/audio_data/demux','static int getb(Movie *m)','/* the first 00 00 01',False),
                take('movie_close','void movie_close(Movie *m)','    free(m);\n}'),
                take('FILT/adpcm_frame/audio_block/movie_audio','static const int FILT[5][2]','    return f;\n}')]
        core='\n'.join(chunks)+'\n'
        with (out/f'audio.{name}.c').open('x') as f:f.write(core)
        manifest['variants'][name]={'source_sha256':sha(s),'extracted_sha256':sha(core),'spans':spans}
    with (out/'extraction.json').open('x') as f:json.dump(manifest,f,indent=2);f.write('\n')
    return manifest
