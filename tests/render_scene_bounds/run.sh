#!/bin/sh
# Host-side regression of the current renderer's scene-bound cache lifecycle.
set -eu
sanitize=0
case "${1:-}" in
    '') ;;
    --sanitize) sanitize=1 ;;
    --help|-h)
        echo "Usage: $0 [--sanitize]"
        echo 'Requires Python 3 and a C compiler (CC, default cc).'
        echo 'KEEP_TEST_OUTPUT=1 retains temporary generated source, manifests and binaries.'
        exit 0 ;;
    *) echo "Unknown argument: $1" >&2; exit 2 ;;
esac
[ "$#" -le 1 ] || { echo "Only --sanitize is supported" >&2; exit 2; }

test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$test_dir/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rampage-scene-bounds.XXXXXX")
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
python3 "$test_dir/extract_renderer.py" "$root/src/render_gl.c" "$work/renderer.c"
if [ "$sanitize" -eq 1 ]; then
    echo 'ASan+UBSan enabled. Leak detection defaults off because LSan cannot run under ptrace.'
    echo 'To enable leak checking on a compatible host, set ASAN_OPTIONS=detect_leaks=1.'
    export ASAN_OPTIONS="detect_leaks=0:halt_on_error=1${ASAN_OPTIONS:+:$ASAN_OPTIONS}"
    export UBSAN_OPTIONS="halt_on_error=1${UBSAN_OPTIONS:+:$UBSAN_OPTIONS}"
fi
for mode in host xbox-low-memory gles-low-memory; do
    defs=
    case "$mode" in
        xbox-low-memory) defs='-DRAMPAGE_XBOX -DRAMPAGE_LOW_MEMORY' ;;
        gles-low-memory) defs='-DRAMPAGE_GLES -DRAMPAGE_LOW_MEMORY' ;;
    esac
    flags='-O2'
    if [ "$sanitize" -eq 1 ]; then flags='-O1 -fsanitize=address,undefined -fno-omit-frame-pointer'; fi
    printf '\nTesting %s\n' "$mode"
    # defs and flags intentionally contain separate compiler options.
    # shellcheck disable=SC2086
    "$CC" -std=gnu99 -g -Wall -Wextra -Werror $flags $defs -I"$root/src" -I"$test_dir" "$work/renderer.c" -lm -o "$work/$mode"
    # Preserve the program's failure status while also saving a log for inspection.
    status=0
    "$work/$mode" >"$work/$mode.log" 2>&1 || status=$?
    cat "$work/$mode.log"
    [ "$status" -eq 0 ] || exit "$status"
done
printf '\nScene-bound regression passed in all three host build configurations.\n'
