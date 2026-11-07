#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$test_dir/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rampage-city-lifetimes.XXXXXX")
flags='-O2'
case "${1:-}" in
    '') ;;
    --sanitize)
        flags='-O1 -fsanitize=address,undefined -fno-omit-frame-pointer'
        export ASAN_OPTIONS="detect_leaks=0:halt_on_error=1${ASAN_OPTIONS:+:$ASAN_OPTIONS}"
        export UBSAN_OPTIONS="halt_on_error=1${UBSAN_OPTIONS:+:$UBSAN_OPTIONS}"
        ;;
    *) echo "Usage: $0 [--sanitize]" >&2; exit 2 ;;
esac
python3 "$test_dir/extract_sources.py" "${CITY_SOURCE:-$root/src/city.c}" "$root/src/mesh.c" "$work/city_extract.inc"
# flags intentionally expands to separate compiler arguments.
"${CC:-cc}" -std=c99 -D_DEFAULT_SOURCE -DRAMPAGE_LOW_MEMORY -Wall -Wextra -Werror $flags -I"$root/src" -I"$work" "$test_dir/regression.c" -o "$work/regression"
"$work/regression"
printf 'Test artifacts retained: %s\n' "$work"
