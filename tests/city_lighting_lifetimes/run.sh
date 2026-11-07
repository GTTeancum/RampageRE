#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../.." && pwd)
flags='-O2'
case "${1:-}" in
    '') ;;
    --sanitize)
        flags='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'
        export ASAN_OPTIONS="detect_leaks=0:halt_on_error=1${ASAN_OPTIONS:+:$ASAN_OPTIONS}"
        export UBSAN_OPTIONS="halt_on_error=1${UBSAN_OPTIONS:+:$UBSAN_OPTIONS}"
        ;;
    *) echo "Usage: $0 [--sanitize]" >&2; exit 2 ;;
esac
# Never delete outputs, including on failure. Default to repository-local workspace.
work=$(mktemp -d "${TMPDIR:-$root}/city-lighting-lifetimes.XXXXXX")
printf 'Artifacts retained: %s\n' "$work"
export TMPDIR="$work"
trap 'printf "Artifacts retained: %s\n" "$work"' EXIT
python3 "$here/extract_sources.py" "$root" "$work" > "$work/extraction.log" 2>&1
cd "$work"
"${CC:-cc}" --version > compiler.log
printf 'Compiler: '; head -1 compiler.log
printf 'Flags: %s\n' "$flags"
printf 'CC=%s\nFLAGS=%s\nASAN_OPTIONS=%s\nUBSAN_OPTIONS=%s\n' "${CC:-cc}" "$flags" "${ASAN_OPTIONS:-}" "${UBSAN_OPTIONS:-}" > configuration.log
for variant in candidate baseline; do
    extra=''
    if [ "$variant" = baseline ]; then extra='-DBASELINE -Wno-unused-label'; fi
    for unit in regression crack_update; do
        # flags and extra deliberately expand to separate compiler arguments.
        "${CC:-cc}" -std=c99 -D_DEFAULT_SOURCE -DRAMPAGE_LOW_MEMORY -Wall -Wextra -Werror $flags $extra -I"$root/src" -I"$work" "$here/$unit.c" -lm -o "$variant-$unit" > "$variant-$unit.compile.log" 2>&1
        "./$variant-$unit" > "$variant-$unit.log" 2>&1
        cat "$variant-$unit.log"
    done
done
"${CC:-cc}" -std=c99 -D_DEFAULT_SOURCE -DRAMPAGE_LOW_MEMORY -Wall -Wextra -Werror -fsyntax-only -I"$root/src" "$root/src/city.c" > syntax.log 2>&1
python3 "$here/verify_results.py" "$root" "$work" > verification.log
cat verification.log
printf 'PASS full current city.c syntax; inputs unchanged; seven exact byte comparisons\n'
