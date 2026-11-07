#!/bin/sh
set -eu
ulimit -c 0
flags=-O2
case "${1:-}" in '') ;; --sanitize) flags='-O1 -fsanitize=address,undefined -fno-omit-frame-pointer';export ASAN_OPTIONS="detect_leaks=0:halt_on_error=1${ASAN_OPTIONS:+:$ASAN_OPTIONS}";export UBSAN_OPTIONS="halt_on_error=1${UBSAN_OPTIONS:+:$UBSAN_OPTIONS}" ;; *) exit 2;; esac
T=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
R=${REPO_ROOT:-$(CDPATH= cd -- "$T/../.." && pwd)}
W=$(mktemp -d "${TMPDIR:-/tmp}/renderer-cache-failures.XXXXXX")
echo "Retained: $W"
python3 "$T/extract.py" "$R" "$W" "${SOURCE_DIR:-$R/src}"
for mode in host xbox-low-memory; do
 defs=;[ "$mode" != xbox-low-memory ] || defs='-DRAMPAGE_XBOX -DRAMPAGE_LOW_MEMORY'
 "${CC:-cc}" -pipe -std=c11 -g $flags $defs -I"$W/headers" -I"$T" "$W/extracted.c" -lm -o "$W/$mode"
 for test in healthy "${REGRESSION_MODE:-failures}"; do
  status=0; RAMPAGE_XBOX_VBO=1 "$W/$mode" "--$test" >"$W/$mode-$test.log" 2>&1 || status=$?
  cat "$W/$mode-$test.log";[ "$status" -eq 0 ] || exit "$status"
 done
done
# Optional real-data fixtures stay outside the permanent test package. Their
# mini-bundle containers must have the texture at entry 0 (see README).
if [ -n "${TEXTURE_CORPUS_DIR:-}" ]; then
 bundle=${SOURCE_DIR:-$R/src}/bundle.c
 [ -f "$bundle" ] || bundle=$R/src/bundle.c
 "${CC:-cc}" -pipe -std=c11 -D_DEFAULT_SOURCE -D_FILE_OFFSET_BITS=64 -g $flags -ffunction-sections -fdata-sections -I"$W/headers" -I"$R/src" -c "$bundle" -o "$W/actual-bundle.o"
 "${CC:-cc}" -pipe -std=c11 -g $flags -DACTUAL_TEXTURES -I"$W/headers" -I"$T" "$W/extracted.c" "$W/actual-bundle.o" -Wl,--gc-sections -lm -o "$W/actual"
 status=0
 python3 - "$W/actual" "$TEXTURE_CORPUS_DIR" >"$W/actual.log" 2>&1 <<'PY' || status=$?
from pathlib import Path
import subprocess,sys
root=Path(sys.argv[2]).resolve()
files=sorted([*root.glob('*.bdp'),*root.glob('*.bdg')])
if not files:raise SystemExit('No .bdp/.bdg texture fixtures in '+str(root))
raise SystemExit(subprocess.call([sys.argv[1],'--failures',*map(str,files)]))
PY
 cat "$W/actual.log";[ "$status" -eq 0 ] || exit "$status"
fi
