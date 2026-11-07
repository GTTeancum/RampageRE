#!/bin/sh
set -eu
T=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
exec python3 "$T/run.py" "$@"
