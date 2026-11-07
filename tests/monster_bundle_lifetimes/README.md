# Monster prerequisite bundle ownership

Run `tests/monster_bundle_lifetimes/run.sh`, optionally `--sanitize` for ASan/UBSan. Requires Python 3 and a host C compiler. Tests ordinary and low-memory layouts, retaining generated files and receipts under TMPDIR (repository root by default). REPO_ROOT, CITY_SOURCE and CC allow explicit overrides.

The runner extracts current production functions, never patches the tested source. Controlled file/bundle providers model missing animation, missing model and missing skeleton. A pointer/byte ledger checks every synthetic allocation and release. Prior owner counts 0–8 cover partial/full capacity. Genuine optional absence rolls back only newly acquired bundles and permits retry. A present-but-unloadable bundle or exhausted owner capacity is fatal: -2 is returned, construction_failed stays set, and published owners remain available to normal teardown. The fixture checks repeated optional failures, sticky fatal rejection without further allocation, old-owner identity preservation and later valid admission where capacity permits. ASan leak detection is disabled by default to match the headless audit environment; ASAN_OPTIONS can override this. Explicit zero-owner assertions remain enabled.

Success stops immediately before new_instance, returning a test sentinel. This does not test complete monster construction, real assets/readers, scene_free, Skin allocation, rendering or gameplay. No source, asset or existing evidence is modified or deleted.

## City status compatibility

This suite also uses `tests/city_status_extract.py`. It extracts the complete
current private City declaration, MAX_BOOKS, and actual `city_load_failed` /
`scene_failed` definitions into `city-status.inc`; no status result is stubbed.
`city-status-manifest.json` records exact input, extractor, fragment and output
hashes plus source lines. Its allocation-free assertions check null handling,
healthy owners, City-to-Scene sticky propagation, retention after owner reset and
independence of a second City. These are scoped status/ownership fixtures, not a
Book parser, complete City loader or whole-game performance measurement.

The earlier one-member City double is replaced by the actual extracted type. Existing prerequisite failure/retry cases and ownership totals are unchanged.
