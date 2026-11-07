#!/bin/sh
set -eu
ulimit -c 0
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
source_dir=${SOURCE_DIR:-"$root/src"}
work=$(mktemp -d "${TMPDIR:-/tmp}/rampage-moves-lifetimes.XXXXXX")
trap 'printf "Test artifacts retained: %s\n" "$work"' EXIT
trap 'exit 1' HUP INT TERM
python3 "$test_dir/extract_sources.py" "$root" "$work" "$source_dir"
# SOURCE_DIR must include reviewed city.c/main.c/moves.c/moves.h; other inputs
# fall back to the repository. This does not install or generate a source fix.
"${CC:-cc}" -pipe -std=c11 -D_DEFAULT_SOURCE -DRAMPAGE_LOW_MEMORY -I"$source_dir" -I"$root/src" \
  -fsyntax-only "$source_dir/main.c" "$source_dir/moves.c"
# flags intentionally expands to separate compiler arguments.
"${CC:-cc}" -pipe -std=c11 -D_DEFAULT_SOURCE -DRAMPAGE_LOW_MEMORY -g $flags \
  -I"$source_dir" -I"$root/src" -I"$work" -I"$test_dir" \
  "$test_dir/regression.c" "$work/b-runner.c" "$work/c-runner.c" -lm -o "$work/regression"
# Ordinary-layout recovery is deliberately narrow: the existing fixture's
# healthy shell-retention assertions are specific to LOW_MEMORY.
"${CC:-cc}" -pipe -std=c11 -D_DEFAULT_SOURCE -DSTATUS_RECOVERY_ONLY -g $flags \
  -I"$source_dir" -I"$root/src" -I"$work" -I"$test_dir" \
  "$test_dir/regression.c" "$work/b-runner.c" "$work/c-runner.c" -lm -o "$work/recovery-ordinary"
"$work/recovery-ordinary" > "$work/recovery-ordinary.log" 2>&1
cat "$work/recovery-ordinary.log"
for layout in ordinary low-memory; do
  define=''; test "$layout" != low-memory || define='-DRAMPAGE_LOW_MEMORY'
  "${CC:-cc}" -pipe -std=c11 -D_DEFAULT_SOURCE -O1 $flags $define \
    -I"$source_dir" -I"$root/src" -I"$work" \
    "$test_dir/checked_open.c" -o "$work/checked-open-$layout"
  "$work/checked-open-$layout" > "$work/checked-open-$layout.log" 2>&1
  cat "$work/checked-open-$layout.log"
done
status=0; "$work/regression" > "$work/result.log" 2>&1 || status=$?
cat "$work/result.log"
exit "$status"
