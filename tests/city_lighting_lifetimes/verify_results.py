#!/usr/bin/env python3
"""Compare whole serialized payloads (not hashes alone) and verify input stability."""
import hashlib
import json
from pathlib import Path
import sys

root, work = map(Path, sys.argv[1:])
comparisons = []
for kind, shapes in [('copy', range(3)), ('scene', range(3)), ('crack', range(1))]:
    for shape in shapes:
        a = (work/f'baseline_{kind}_{shape}.bin').read_bytes()
        b = (work/f'candidate_{kind}_{shape}.bin').read_bytes()
        assert a == b, (kind, shape, 'serialized bytes differ')
        comparisons.append(dict(kind=kind, shape=shape, equal_bytes=len(a),
                                sha256=hashlib.sha256(a).hexdigest()))
for path, digest in json.loads((work/'source_manifest.json').read_text())['inputs'].items():
    assert hashlib.sha256((root/path).read_bytes()).hexdigest() == digest, path
(work/'comparisons.json').write_text(json.dumps(comparisons, indent=2)+'\n')
print(json.dumps(comparisons, indent=2))
