#!/usr/bin/env python3
"""Extract the current main.c ownership branches, without copying their logic."""
from pathlib import Path
import sys,hashlib,json
src=Path(sys.argv[1]);out=Path(sys.argv[2]);s=src.read_text()
def between(a,b):
 assert s.count(a)==1,a
 start=s.index(a);stop=s.index(b,start)
 return s[start:stop]
gui=between('        uint32_t gsize = 0, csize = 0;', '        v.gui_world.scope =')
sd=between('        uint32_t size = 0;\n        uint8_t *sd = n ?', '        if (v.wii_roster.n)')
out.write_text('static void startup_gui(void)\n{\n'+gui+'}\nstatic void startup_wii(int n)\n{\n'+sd+'}\n')
out.with_suffix('.json').write_text(json.dumps({'source':str(src.resolve()),'sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'functions':['startup_gui','startup_wii']},indent=2)+'\n')
