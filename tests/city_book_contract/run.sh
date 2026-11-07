#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=${REPO_ROOT:-"$(CDPATH= cd -- "$here/../.." && pwd)"}
src=${SOURCE_DIR:-"$root/src"}
out=${CITY_BOOK_TEST_OUTPUT:-"${TMPDIR:-$root}/city-book-contract-$(date +%s)-$$"}
# The aggregate calls this once normally and once with --sanitize.
# Each invocation runs only its requested mode; Python directly defaults to both.
# The asset-free cache suite also includes checked-open regression coverage.
# Explicit arguments may select --suite all and provide the owner's media paths.
exec python3 "$here/run.py" --repo "$root" --src "$src" --mode normal --suite cache --out "$out" "$@"
