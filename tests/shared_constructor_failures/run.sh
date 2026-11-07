#!/bin/sh
set -eu
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
case "${1:-}" in
    '') mode=normal ;;
    --sanitize) mode=sanitize ;;
    *) echo "Usage: $0 [--sanitize]" >&2; exit 2 ;;
esac
[ "$#" -le 1 ] || { echo "Usage: $0 [--sanitize]" >&2; exit 2; }
SOURCE_DIR=${SOURCE_DIR:-"$D/../../src"}
exec python3 "$D/run.py" "$SOURCE_DIR" "$mode"
