#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo=${REPO_ROOT:-"$here/../.."}
repo=$(CDPATH= cd -- "$repo" && pwd)
sanitize=0
candidate=
for arg in "$@"; do
    case "$arg" in
        --sanitize) sanitize=1 ;;
        --candidate) candidate=--candidate ;;
        *) echo "Usage: $0 [--sanitize] [--candidate] (optional REPO_ROOT and TMPDIR environment variables)" >&2; exit 2 ;;
    esac
done
base=${TMPDIR:-"$repo/.test-results"}
mkdir -p "$base"
base=$(CDPATH= cd -- "$base" && pwd)
work=$(mktemp -d "$base/army-hold-array.XXXXXX")
echo "Retained test output: $work"
TMPDIR=$work
export TMPDIR
unset RAMPAGE_TRACE
python3 "$here/extract.py" "$repo" "$work" $candidate
cp "$here/test.c" "$work/test.c"
cc=${CC:-cc}
"$cc" --version > "$work/compiler.txt"
sha256sum "$here/extract.py" "$here/test.c" "$here/run.sh" > "$work/harness_hashes.txt"
flags='-std=c11 -Wall -Wextra -Werror -Warray-bounds=2 -Wstringop-overflow=2'
"$cc" $flags -O2 "$work/test.c" -lm -o "$work/normal" 2> "$work/normal-build.log"
"$work/normal" > "$work/normal.log"
cat "$work/normal.log"
if [ "$sanitize" -eq 1 ]; then
    "$cc" $flags -O2 -g -fno-omit-frame-pointer -fsanitize=address,undefined,bounds-strict -fno-sanitize-recover=all "$work/test.c" -lm -o "$work/sanitized" 2> "$work/sanitized-build.log"
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$work/sanitized" > "$work/sanitized.log" 2> "$work/sanitized-stderr.log"
    cat "$work/sanitized.log"
fi
"$cc" $flags -O2 -S "$work/test.c" -o "$work/optimized.s"
