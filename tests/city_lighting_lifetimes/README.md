# City lighting ownership regression

Run from the repository root, with an existing workspace directory for outputs:

```sh
TMPDIR=/path/to/workspace tests/city_lighting_lifetimes/run.sh
TMPDIR=/path/to/workspace tests/city_lighting_lifetimes/run.sh --sanitize
```

Requires Python 3 and a C99 host compiler (`CC`, default `cc`). Runs serially,
without assets, downloads, installation, emulator or Xbox HDD access. Each run
creates a fresh directory beneath `TMPDIR` (default: repository root), prints
its path immediately, and retains **all** outputs, including failures. No cleanup
or deletion is performed. LSan defaults off in sanitizer mode; explicit ownership
accounting always runs. Existing `ASAN_OPTIONS`/`UBSAN_OPTIONS` can override the
defaults. ASan requires ample virtual address space, not a console memory cap.
Assertions must remain enabled; the runner does not accept arbitrary CFLAGS.

## Live source, historical control, and evidence

`extract_sources.py` reads current `src/city.c` and `src/mesh.c` on every run.
There is no candidate source snapshot and no source override. Definitions must
be unique and brace-balanced after masking comments/literals. The final checked
lighting call and failure tail must match an explicit boundary; structural drift
fails extraction rather than silently exercising a guessed implementation.
Compiler warnings are errors. `source_manifest.json` records input SHA-256s,
function hashes and line ranges. Extracted definitions preserve diagnostic line
locations. Current CELL and STOREY macros and headers are used. The full current
city translation unit also receives a syntax-only check.

The only frozen production code is `baseline_functions.inc`: the five historical
pre-cleanup lighting constructors/dispatcher/final-call wrapper. Its SHA-256 is
pinned in the extractor. Both controls use current mesh destruction, Scene
destruction, scene publication, ground-zone/UV helpers and crack updater. Thus
this is a bounded old-constructor control, not a complete old executable. The
runner deliberately does not execute historical crashing fault probes: they are
not needed to pass the regression and would be platform-dependent. Current
fault paths are exhaustive for the allocation sequences of these fixtures.

Derived from the sealed 2026-11-06 lighting candidate harness and its independent
review. The reviewed city source hash was
`798623d0e71a6cec57bfffe0aec7b84a3f401a8f426a670d55922bdd4e831fa1`.
This hash is provenance, **not** a restriction on future current source testing.
The baseline fixture SHA-256 is
`fe632db4bf3fbea6800d4c84850dcac500bb990e210cdc51f85797238d365758`.
No private workspace input is needed to run this repository suite.

## Cases and exact comparisons

Four synthetic mesh shapes cover populated attributes, zero vertices/triangles
with non-null buffers, no parts, and zero counts with null position/index buffers.
Parts cover missing normals, UVs and colors. Two playable buildings and one
scenery building exercise dispatcher cleanup.

- 49 individual mesh-copy allocation failures: 16 + 16 + 1 + 16.
- 281 complete lighting-tail failures: 92 + 92 + 5 + 92.
- 12 failed overlay rebuilds over existing published state: six allocation
  positions for each of two shapes. These check that the existing mesh, mapping
  and metadata survive unchanged; successful repeat construction is not claimed.
- 11 successes: four copies, four lighting tails and three inapplicable-overlay
  no-ops. The no-ops must allocate nothing and preserve metadata.
- Actual `ground_cracks` executes five phases: hidden, first hit, second hit,
  dent, hidden again. A one-row synthetic Building grid is explicitly owned by
  the test. Triangle visibility and hit-alpha progression are asserted.

Six shared copy/scene outputs and the five-phase crack output are compared by
**full serialized bytes**, not just a hash. The serializer includes mesh/part
metadata, presence bits, positions, normals, UVs, colors, triangle indices and
crack mappings. It excludes addresses and struct padding. Output is host-native,
so comparisons are within the same run/ABI, not portable golden byte files.
`comparisons.json` records sizes and hashes for audit. Current-only zero/null
shape and no-op paths have assertions but no unsafe baseline execution.

Expected current fixture totals are asserted: 342 failures and 11 successes.
Changing fixture coverage requires updating these expectations after review.
The reviewed run frees 21,260 constructor allocations, peak 32,991 requested
bytes; the bounded baseline has six successes, 299 allocations and peak 32,862
bytes. The extra synthetic peak is 129 bytes, not a real-level memory estimate.
Each crack run frees 112 allocations. Peak excludes allocator metadata/canaries.

## Explicit ownership ledger and boundaries

- `test_malloc`/`test_calloc`: register each returned block and requested length,
  including zero-byte requests, in a 1,024-entry ledger. Track live allocation
  count, requested live/peak bytes, total acquired and released blocks. Append
  64-byte tail canaries. Inject one allocation failure by call position.
- `test_realloc`: a failed request preserves the original owner. Successful
  resizing updates the same ledger entry; it is not a new ownership acquisition.
  The real allocator may resize in place or move; forced movement is not tested.
- `test_free`: reject unknown/double frees, verify tail canaries, remove the
  owner and decrement bytes/count. Null frees are allowed. Setup allocations
  and cleanup allocations use the same ledger. Real host OOM aborts the test.
- Source mesh: owned by the fixture; copying must leave its bytes unchanged.
  Failed copy leaves a zero destination and exactly the original live owners.
  Successful copied meshes are destroyed with actual `mesh_free`.
- Overlay staging: temporary buffers become MeshPart/SceneBuilding owners only
  on commit. Failure must leave published mesh/mapping metadata and live bytes
  unchanged. Actual `scene_free` owns the resulting Scene members.
- Scene fixture: preallocates capacity for 32 meshes, so `scene_add_mesh` is
  actual source but its checked fail-fast growth is outside this test. The separate
  `city_mesh_growth` suite tests allocation-failure and overflow containment.
  Final lighting failure uses actual city_build failure cleanup. The entire
  failed Scene must compare equal to zero; a repeated destructor must be safe.
- `shade_neighbours` and `light_building` are explicit numerical-lighting stubs;
  the latter counts calls. No lighting must occur after constructor failure.
  `building_free` is a no-op because fixtures own no internal Building resources;
  the crack fixture explicitly releases its separately allocated vertex row.
  Water/cloth/tree/clip/people/bundle destructors assert if unexpectedly invoked.
  The synthetic City uses the actual extracted owner declaration, not a loaded city.

This proves focused host ownership and success-output preservation, **not** full
level construction, real-asset Skin/geometry correctness, whole-process leak
freedom, graphics/audio lifetimes, runtime look graphs, all constructor OOM paths,
fragmentation, target performance, Xbox linking or emulator/hardware execution.
Tail canaries supplement ASan but do not detect all invalid accesses in normal
mode. Sanitizers observe host allocations; the explicit ledger is the leak proof
for the narrowly intercepted ownership paths, not a global leak scan.

## City status compatibility

This suite also uses `tests/city_status_extract.py`. It extracts the complete
current private City declaration, MAX_BOOKS, and actual `city_load_failed` /
`scene_failed` definitions into `city-status.inc`; no status result is stubbed.
`city-status-manifest.json` records exact input, extractor, fragment and output
hashes plus source lines. Its allocation-free assertions check null handling,
healthy owners, City-to-Scene sticky propagation, retention after owner reset and
independence of a second City. These are scoped status/ownership fixtures, not a
Book parser, complete City loader or whole-game performance measurement.

The lighting wrapper retains the new exact final status check and complete failure tail, including City primitive-state rollback. It extracts the actual rollback snapshot. Crowd publication is inapplicable to the lighting fixtures; audio doubles assert if called. All seven historical/current byte comparisons, 342 allocation failures and existing allocation totals remain unchanged.
