# Runtime geometry ownership regressions

Run from the installed repository:

```
sh tests/runtime_geometry_failures/run.sh
sh tests/runtime_geometry_failures/run.sh --sanitize
```

Or use an explicit source and fresh output directory:

```
python3 tests/runtime_geometry_failures/run.py --src /path/to/src --output /path/to/new-output --sanitize
```

Both commands test ordinary and RAMPAGE_LOW_MEMORY layouts. Sources are extracted only from the supplied/current source root. No old implementation or frozen fallback is included. The extractor fails if functions/anchors are missing or ambiguous. All compilation/execution outputs are retained; set TMPDIR to a private scratch location. No target build or emulator is run.

The suite runs 70 allocation-failure scenarios per invocation, including initial and later slot realloc and order failures, every retained facade/window buffer allocation, including both glass material states, populated order/obstacle realloc and obstacle box replacement. It requires zero scoped live owners/bytes after cleanup and refuses further allocations after sticky failure. Successful fixtures match SHA-256 4ae3c70ec99b9345d984cf6e299cfab000d91e17aced4dfbce5a447049ad016a and exact scoped allocation counts (ordinary 201 calls/191 owners; LOW_MEMORY 204/194). The output reference comes from accepted successful behavior; allocation counts reflect the reviewed intentional omission of discarded normals/UVs, with no fallback implementation. A legitimate successful-output or allocation-policy change must explicitly review/update those expectations.

The focused harness uses actual extracted city.c functions, building.c, and mesh_free. Group lookup and RNG use deterministic doubles. The obstacle append block is copied verbatim into a minimal wrapper. Geometry fixtures are synthetic. Ten main/city stop-boundary checks are static; main.c is not compiled by this runner. ASan/UBSan uses detect_leaks=0; explicit ledgers are not LSan.

## Optional full-source real-volume extension

```
python3 tests/runtime_geometry_failures/run_fullsource.py \
  --src /path/to/src --volume /path/to/RAMPAGE.VOL \
  --output /path/to/new-real-output --low-memory --sanitize
```

Omit either layout/sanitizer switch to select ordinary/normal. This optional test needs the supported PS2 volume containing LasVegas/LV01_Casino1. It compiles actual city.c and its dependency translation units with an allocation-site ledger and existing host I/O/audio doubles. Allocation lines are derived from the supplied source, never hard-coded. All scene cleanup uses actual scene_free.

Seventeen failures are followed by successful rebuild/recovery: five initial facade-reset allocations, initial obstacle append and box allocation, five runtime facade-reset allocations, three runtime window-reset allocations, order growth with an existing owner, and box replacement. After each failed/successful scene the ledger must return to its warmed City baseline; after City/Vol shutdown it must reach zero owners/bytes. The window fixture installs one synthetic valid window on a real loaded building to select the runtime allocation path. Runtime helpers are called directly from the actual included city translation unit, so this does not replace main-loop control-flow or gameplay testing.

The selected real block contains only one obstacle and produces zero occupant slots in this standalone setup. Later obstacle growth and slot-failure ownership are covered in the focused tests, not claimed as natural real-asset occurrences. The extension does not cover all assets, rendering/GPU lifetime, audio backend, sustained gameplay, FPS, or target memory budgets.

## City status compatibility

This suite also uses `tests/city_status_extract.py`. It extracts the complete
current private City declaration, MAX_BOOKS, and actual `city_load_failed` /
`scene_failed` definitions into `city-status.inc`; no status result is stubbed.
`city-status-manifest.json` records exact input, extractor, fragment and output
hashes plus source lines. Its allocation-free assertions check null handling,
healthy owners, City-to-Scene sticky propagation, retention after owner reset and
independence of a second City. These are scoped status/ownership fixtures, not a
Book parser, complete City loader or whole-game performance measurement.

City static boundary checks now require scene_failed(s). Main checks require the exact immediate casted Viewer guard for scene_occupant_slots and scene_people_spawn, and the unchanged v guard for the other two calls. No intervening operation is permitted. The 70 fault cases, geometry digest, allocation counts and peak-byte expectations are unchanged.
