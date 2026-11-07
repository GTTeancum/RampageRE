#!/bin/sh
set -eu
ulimit -c 0
flags='-O1 -g'
case "${1:-}" in
  '') ;;
  --sanitize)
    flags="$flags -fsanitize=address,undefined -fno-omit-frame-pointer -no-pie"
    export ASAN_OPTIONS="detect_leaks=0:halt_on_error=1${ASAN_OPTIONS:+:$ASAN_OPTIONS}"
    export UBSAN_OPTIONS="halt_on_error=1${UBSAN_OPTIONS:+:$UBSAN_OPTIONS}" ;;
  *) echo 'Usage: run.sh [--sanitize]' >&2; exit 2 ;;
esac
[ "$#" -le 1 ] || exit 2
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$test_dir/../.." && pwd)
source_dir=${SOURCE_DIR:-"$root/src"}
work=$(mktemp -d "${TMPDIR:-/tmp}/rampage-city-runtime.XXXXXX")
trap 'printf "Test artifacts retained: %s\n" "$work"' EXIT
trap 'exit 1' HUP INT TERM
python3 "$test_dir/prepare.py" "$source_dir" "$work"
python3 "$test_dir/extract_main.py" "$work/src" "$work/main-extracted"
python3 "$test_dir/extract_callbacks.py" "$work/src" "$work/callbacks"
python3 "$test_dir/extract_conditions.py" "$work/src" "$work/conditions"
layout='-DRAMPAGE_LOW_MEMORY'
if [ "${RAMPAGE_TEST_LAYOUT:-low}" = ordinary ]; then layout=''; fi
# flags/layout deliberately expand as individual compiler options.
common="-std=gnu99 $layout $flags -ffunction-sections -fdata-sections"
wrap='-Wl,--gc-sections -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=vol_read'
"${CC:-cc}" $common -I"$work/src" -I"$test_dir" \
  -DCITY_SOURCE="\"$work/src/city.c\"" \
  "$test_dir/manager_probe.c" "$test_dir/ledger.c" "$test_dir/host_stubs.c" \
  "$work/src/army.c" "$work/src/vehicle.c" "$work/src/rail.c" "$work/src/parade.c" \
  "$work/src/edible.c" "$work/src/boss.c" "$work/src/game.c" "$work/src/streetobj.c" \
  "$work/src/people.c" "$work/src/reveal.c" "$work/src/prb.c" "$work/src/bundle.c" \
  "$work/src/vol.c" "$work/src/collide.c" "$work/src/wrand.c" "$work/src/anim.c" \
  $wrap -lm -o "$work/manager"
"$work/manager" > "$work/manager.log" 2>&1
cat "$work/manager.log"
"${CC:-cc}" $common -I"$work/src" -I"$work/main-extracted" -I"$test_dir" \
  -DGAME_SOURCE="\"$work/src/game.c\"" \
  "$test_dir/main_probe.c" "$test_dir/ledger.c" "$work/src/prb.c" "$work/src/wrand.c" \
  $wrap -lm -o "$work/main"
"$work/main" > "$work/main.log" 2>&1
cat "$work/main.log"
"${CC:-cc}" $common -I"$work/src" -I"$work/callbacks" \
  "$test_dir/callback_probe.c" -Wl,--gc-sections -o "$work/callbacks-test"
"$work/callbacks-test" > "$work/callbacks.log" 2>&1
cat "$work/callbacks.log"
"${CC:-cc}" $common -I"$work/src" -I"$work/main-extracted" -I"$work/conditions" \
  "$test_dir/conditions_probe.c" -lm -o "$work/conditions-test"
"$work/conditions-test" > "$work/conditions.log" 2>&1
cat "$work/conditions.log"
python3 "$test_dir/prepare.py" "$source_dir" "$work" --verify
