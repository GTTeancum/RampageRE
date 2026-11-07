# Narrow City construction lifetime regression

This test covers the applied seven-site City allocation patch. Run
`tests/city_lifetimes/run.sh`, optionally with `--sanitize`, against the current
production source. `CITY_SOURCE` may select a private candidate for comparison.
Sources without the checked facade contract fail extraction rather than silently
testing a different implementation. Set `TMPDIR` to workspace storage. All
generated artifacts are retained.

The extractor reads the selected source and the current production mesh source.
It records their hashes and extracts:

- The actual transactional facade function and its caller's facade construction
  and growth block.
- The actual `city_build` initialization prefix through scene-map allocation and
  initialization, and its existing failure tail.
- The complete production `scene_free` and `mesh_free` destructors.

It does **not** emulate a full level load. The synthetic map unit uses the actual extracted City owner declaration.
Only its map and primitive rollback members are exercised. Unexpected destructors of other
resource classes fail the test. Full-source valid-asset scene comparison remains
separate private evidence; no game data is bundled here.

Coverage: 2,000 injected failures at all seven sites, 300 successful facades,
200 successful zero/seven-entry scene maps, partial allocation destruction,
failed-growth owner preservation through cleanup, zeroed failed Scene, and
safe repeated destructors. Allocation tail canaries and explicit counts detect
corruption, unowned/double frees, and live allocation retention. Successful
facade bytes have pinned pre-fix control hashes for three synthetic shapes,
including zero storeys. The private source-matched baseline/candidate proof
also compared every facade attribute/index byte directly over these 300 cases.

`--sanitize` enables ASan/UBSan. LeakSanitizer defaults off for restricted
runtimes; explicit ownership accounting always runs. ASan needs a large virtual
address reservation, so do not run that mode under a 128 MiB address-space cap.
The test's maximum requested live allocation is 2,416 bytes. This suite does not
cover unrelated City constructors, runtime look graphs, graphics, or main-loop
failure handling.

## City status compatibility

This suite also uses `tests/city_status_extract.py`. It extracts the complete
current private City declaration, MAX_BOOKS, and actual `city_load_failed` /
`scene_failed` definitions into `city-status.inc`; no status result is stubbed.
`city-status-manifest.json` records exact input, extractor, fragment and output
hashes plus source lines. Its allocation-free assertions check null handling,
healthy owners, City-to-Scene sticky propagation, retention after owner reset and
independence of a second City. These are scoped status/ownership fixtures, not a
Book parser, complete City loader or whole-game performance measurement.

The map wrapper now retains the actual City primitive-state snapshot and failure rollback. A prefailed owner case asserts zero allocation, a zero failed Scene and unchanged prior City state. The crowd-audio branch is outside the map prefix.
