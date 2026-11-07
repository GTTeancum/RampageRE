#!/bin/sh
set -eu
flags='-O2'
case "${1:-}" in
 '') ;;
 --sanitize) flags='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer';export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1;export UBSAN_OPTIONS=halt_on_error=1 ;;
 *) echo 'Usage: run.sh [--sanitize]' >&2;exit 2;;
esac
T=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
R=${RAMPAGE_TEST_ROOT:-$(CDPATH= cd -- "$T/../.." && pwd)}
: "${TMPDIR:?Set TMPDIR to a writable workspace directory with free space}"
W=$(mktemp -d "$TMPDIR/animation-phase.XXXXXX")
echo "Retained: $W"
python3 "$T/extract.py" "$R" "$W/probe.c"
for mode in host xbox-low-memory; do
 defs='';if [ "$mode" = xbox-low-memory ];then defs='-DRAMPAGE_XBOX -DRAMPAGE_LOW_MEMORY';fi
 "${CC:-cc}" -pipe -std=c11 -fno-builtin $flags $defs -I"$R/src" -I"$T" "$W/probe.c" -lm -o "$W/$mode"
 status=0;"$W/$mode" >"$W/$mode.log" 2>&1 || status=$?
 cat "$W/$mode.log";[ "$status" -eq 0 ] || exit "$status"
done
