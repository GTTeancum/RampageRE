#!/bin/sh
# Exact-source renderer/output-queue regressions; no assets, audio device or GPU.
set -eu
sanitize=0
case "${1:-}" in
    '') ;;
    --sanitize) sanitize=1 ;;
    --help|-h)
        echo "Usage: $0 [--sanitize]"
        echo 'Requires Python 3 and a host C compiler (CC, default cc).'
        echo 'KEEP_TEST_OUTPUT=1 retains only this run’s new temporary artifacts.'
        exit 0 ;;
    *) echo "Unknown argument: $1" >&2; exit 2 ;;
esac
[ "$#" -le 1 ] || { echo 'Only --sanitize is supported' >&2; exit 2; }

test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$test_dir/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rampage-render-refill.XXXXXX")
cleanup() {
    if [ "${KEEP_TEST_OUTPUT:-0}" = 1 ]; then
        printf 'Test artifacts retained: %s\n' "$work"
    else
        rm -rf -- "$work"
    fi
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

CC=${CC:-cc}
python3 "$test_dir/extract_sources.py" "$root/src" "$work"
flags='-O2'
if [ "$sanitize" -eq 1 ]; then
    flags='-O1 -fsanitize=address,undefined -fno-omit-frame-pointer'
    echo 'ASan+UBSan enabled. Leak detection defaults off for ptrace compatibility.'
    export ASAN_OPTIONS="detect_leaks=0:halt_on_error=1${ASAN_OPTIONS:+:$ASAN_OPTIONS}"
    export UBSAN_OPTIONS="halt_on_error=1${UBSAN_OPTIONS:+:$UBSAN_OPTIONS}"
fi
for test in helper queue; do
    defs=
    if [ "$test" = queue ]; then defs='-DRAMPAGE_XBOX -DRAMPAGE_LOW_MEMORY'; fi
    printf '\nTesting %s\n' "$test"
    # Exact sound.c includes unrelated, uncalled public entry points. Section
    # collection discards them instead of supplying mock implementations.
    # Flags and definitions intentionally expand to multiple compiler options.
    # shellcheck disable=SC2086
    "$CC" -std=gnu99 -g -Wall -Wextra $flags $defs \
        -ffunction-sections -fdata-sections -I"$root/src" -I"$test_dir" \
        "$work/$test.c" -Wl,--gc-sections -lm -o "$work/$test"
    status=0
    "$work/$test" >"$work/$test.log" 2>&1 || status=$?
    cat "$work/$test.log"
    [ "$status" -eq 0 ] || exit "$status"
done
printf '\nRenderer output-refill regression passed.\n'
