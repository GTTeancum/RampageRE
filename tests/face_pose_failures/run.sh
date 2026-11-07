#!/bin/sh
set -eu
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
case "${1:-}" in '') mode=normal;; --sanitize) mode=sanitize;; *) echo 'Usage: run.sh [--sanitize]' >&2; exit 2;; esac
[ "$#" -le 1 ] || exit 2
exec python3 "$D/run.py" "${SOURCE_DIR:-$D/../../src}" "$mode"
