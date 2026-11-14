"""Query a Ghidra dump made by tools/ghidra/DumpDecomp.java (decomp.c, calls.tsv, funcs.tsv, strings.tsv), with the
class names of tools/xbe_rtti.py applied to virtual functions.

Usage: python tools/re_query.py DUMP_DIR [--rtti RTTI.json] COMMAND ...
  show NAME|ADDRESS [...]      the decompiled C of functions
  callers NAME|ADDRESS         who calls it (direct calls; virtual calls are not resolved)
  callees NAME|ADDRESS         what it calls
  grep REGEX [MAXLINES]        functions whose C matches, with the matching lines
  strings REGEX                string references: function, string
  class NAME                   a class's bases and virtual tables with their functions' names and sizes
  vtslot OFFSET                classes whose primary table has a function at that byte offset (for (**(code **)(*p + OFFSET)))
"""
import json
import re
import sys


class Dump:
    def __init__(self, root, rtti=None):
        self.root = root
        self.text = open(f'{root}/decomp.c', encoding='utf-8', errors='replace').read()
        self.funcs, self.by_name = {}, {}
        for line in open(f'{root}/funcs.tsv', encoding='utf-8', errors='replace'):
            a, size, name = line.rstrip('\n').split('\t')
            self.funcs[int(a, 16)] = (int(size), name)
        self.rename = {}
        self.rtti = json.load(open(rtti)) if rtti else {'classes': {}, 'functions': {}}
        for a, n in self.rtti['functions'].items():
            a = int(a, 16)
            if a in self.funcs and self.funcs[a][1].startswith(('FUN_', 'thunk_FUN_')):
                self.rename[self.funcs[a][1]] = n.replace('::', '__') + f'_{a:x}'
        self.index = {}
        for m in re.finditer(r'^// ==== (\S+) @ ([0-9a-f]{8}) \((\d+)\)$', self.text, re.M):
            self.index[int(m.group(2), 16)] = m.start()
        self.order = sorted(self.index)
        for a, (size, name) in self.funcs.items():
            self.by_name[self.rename.get(name, name)] = a
            self.by_name[name] = a

    def nm(self, name):
        return self.rename.get(name, name)

    def addr(self, key):
        if key in self.by_name:
            return self.by_name[key]
        try:
            return int(key, 16)
        except ValueError:
            cands = [a for n, a in self.by_name.items() if key.lower() in n.lower()]
            if len(set(cands)) == 1:
                return cands[0]
            sys.exit(f'{key}: ' + (f'{len(set(cands))} matches: ' + ' '.join(sorted({self.nm(self.funcs[a][1]) for a in cands})[:40])
                                   if cands else 'not found'))

    def body(self, a):
        if a not in self.index:
            return f'// {a:08x}: not decompiled (library code or outside the dumped range)\n'
        i = self.order.index(a)
        end = self.index[self.order[i + 1]] if i + 1 < len(self.order) else len(self.text)
        t = self.text[self.index[a]:end]
        return re.sub(r'\b(?:thunk_)?FUN_[0-9a-f]{8}\b', lambda m: self.nm(m.group()), t)

    def calls(self):
        for line in open(f'{self.root}/calls.tsv', encoding='utf-8', errors='replace'):
            p = line.rstrip('\n').split('\t')
            yield int(p[0], 16), p[1], int(p[2], 16), p[3]


def main():
    args = sys.argv[1:]
    root = args.pop(0)
    rtti = None
    if args and args[0] == '--rtti':
        rtti = args[1]
        args = args[2:]
    d = Dump(root, rtti)
    cmd, rest = args[0], args[1:]
    if cmd == 'show':
        for k in rest:
            print(d.body(d.addr(k)))
    elif cmd in ('callers', 'callees'):
        a = d.addr(rest[0])
        seen = {}
        for ca, cn, ta, tn in d.calls():
            if cmd == 'callers' and ta == a:
                seen[ca] = seen.get(ca, 0) + 1
            elif cmd == 'callees' and ca == a:
                seen[ta] = seen.get(ta, 0) + 1
        for x, n in sorted(seen.items()):
            size, name = d.funcs.get(x, (0, '?'))
            print(f'{x:08x} {d.nm(name)} ({size} bytes) x{n}')
    elif cmd == 'grep':
        pat = re.compile(rest[0])
        maxl = int(rest[1]) if len(rest) > 1 else 3
        for a in d.order:
            b = d.body(a)
            hits = [l.strip() for l in b.split('\n')[1:] if pat.search(l)]
            if hits:
                size, name = d.funcs[a]
                print(f'{a:08x} {d.nm(name)} ({size}): ' + ' | '.join(h[:140] for h in hits[:maxl]) +
                      (f' ... +{len(hits) - maxl}' if len(hits) > maxl else ''))
    elif cmd == 'strings':
        pat = re.compile(rest[0], re.I)
        for line in open(f'{root}/strings.tsv', encoding='utf-8', errors='replace'):
            p = line.rstrip('\n').split('\t')
            if len(p) == 4 and pat.search(p[3]):
                print(f'{p[0]} {d.nm(p[1])}: {p[3][:160]}')
    elif cmd == 'class':
        for name, c in d.rtti['classes'].items():
            if name == rest[0]:
                print(name, 'bases:', ', '.join(c['bases']) or '-')
                for vt in c['vtables']:
                    print(f' table {vt["address"]} (object offset {vt["offset"]})')
                    for k, f in enumerate(vt['functions']):
                        a = int(f, 16)
                        size, fn = d.funcs.get(a, (0, '?'))
                        print(f'   +{4 * k:#05x} [{k:2d}] {a:08x} {d.nm(fn)} ({size})')
    elif cmd == 'vtslot':
        off = int(rest[0], 0) // 4
        for name, c in sorted(d.rtti['classes'].items()):
            for vt in c['vtables']:
                if vt['offset'] == 0 and off < len(vt['functions']):
                    a = int(vt['functions'][off], 16)
                    print(f'{name}: {a:08x} {d.nm(d.funcs.get(a, (0, "?"))[1])}')


if __name__ == '__main__':
    main()
