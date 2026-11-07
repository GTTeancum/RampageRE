#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=${REPO_ROOT:-$(CDPATH= cd -- "$here/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-$root}/rampage-city-mesh-growth.XXXXXX")
printf 'Test artifacts retained: %s\n' "$work"
flags='-O2'
case "${1:-}" in
 '') ;;
 --sanitize) flags='-O1 -fsanitize=address,undefined -fno-omit-frame-pointer'
 export ASAN_OPTIONS="detect_leaks=0:halt_on_error=1${ASAN_OPTIONS:+:$ASAN_OPTIONS}"
 export UBSAN_OPTIONS="halt_on_error=1${UBSAN_OPTIONS:+:$UBSAN_OPTIONS}" ;;
 *) echo "Usage: $0 [--sanitize]" >&2; exit 2 ;;
esac
python3 "$here/extract_sources.py" "$root" "${CITY_SOURCE:-$root/src/city.c}" "$work"
for mode in ordinary low_memory; do
 define=''; test "$mode" != low_memory || define='-DRAMPAGE_LOW_MEMORY'
 "${CC:-cc}" -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror $flags $define -I"$root/src" -I"$work" "$here/regression.c" -o "$work/$mode" >"$work/$mode.compile.log" 2>&1
 "${CC:-cc}" -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror $define -I"$root/src" -fsyntax-only "${CITY_SOURCE:-$root/src/city.c}" >"$work/$mode.syntax.log" 2>&1
 "$work/$mode" >"$work/$mode.log" 2>&1
 cat "$work/$mode.log"
done
