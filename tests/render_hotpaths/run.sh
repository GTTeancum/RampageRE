#!/bin/sh
set -eu
flags='-O2'
case "${1:-}" in
  '') ;;
  --sanitize) flags='-O1 -fsanitize=address,undefined -fno-omit-frame-pointer';
    export ASAN_OPTIONS="detect_leaks=0:halt_on_error=1${ASAN_OPTIONS:+:$ASAN_OPTIONS}";
    export UBSAN_OPTIONS="halt_on_error=1${UBSAN_OPTIONS:+:$UBSAN_OPTIONS}" ;;
  *) echo 'Usage: run.sh [--sanitize]' >&2; exit 2 ;;
esac
[ "$#" -le 1 ] || exit 2
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$test_dir/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rampage-render-hotpaths.XXXXXX")
cleanup() { if [ "${KEEP_TEST_OUTPUT:-0}" = 1 ]; then echo "Retained: $work"; else rm -rf -- "$work"; fi; }
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
python3 "$test_dir/extract.py" "$root" "$work/renderer.c"
python3 "$test_dir/extract_cloth.py" "$root" "$work/cloth.c"
python3 "$test_dir/extract_anim.py" "$root" "$work/anim.c"
for mode in host xbox-low-memory; do
  defs=
  if [ "$mode" = xbox-low-memory ]; then defs='-DRAMPAGE_XBOX -DRAMPAGE_LOW_MEMORY'; fi
  # flags and defs intentionally expand to separate arguments; -pipe avoids large assembly scratch files.
  "${CC:-cc}" -pipe -std=c11 -g $flags $defs -I"$root/src" -I"$test_dir" "$work/renderer.c" -lm -o "$work/$mode"
  status=0; "$work/$mode" > "$work/$mode.log" 2>&1 || status=$?
  cat "$work/$mode.log"; [ "$status" -eq 0 ] || exit "$status"
  "${CC:-cc}" -pipe -std=c11 -g $flags $defs -I"$root/src" -I"$test_dir" "$work/cloth.c" -lm -o "$work/cloth-$mode"
  status=0; "$work/cloth-$mode" > "$work/cloth-$mode.log" 2>&1 || status=$?
  cat "$work/cloth-$mode.log"; [ "$status" -eq 0 ] || exit "$status"
  "${CC:-cc}" -pipe -std=c11 -g -fno-builtin $flags $defs -I"$root/src" -I"$test_dir" "$work/anim.c" -lm -o "$work/anim-$mode"
  status=0; "$work/anim-$mode" > "$work/anim-$mode.log" 2>&1 || status=$?
  cat "$work/anim-$mode.log"; [ "$status" -eq 0 ] || exit "$status"
done
