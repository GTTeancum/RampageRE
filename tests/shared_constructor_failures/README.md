# Shared constructor failure regression

Run from an installed checkout:

    sh tests/shared_constructor_failures/run.sh
    sh tests/shared_constructor_failures/run.sh --sanitize

The default source directory is `../../src` relative to this test directory. To test another checkout, set `SOURCE_DIR=/absolute/path/to/src`. There is no frozen-source fallback. Missing sources or changed expected function boundaries fail loudly. This isolated package has no sibling `src`, so its validation runs use an explicit `SOURCE_DIR`.

Requirements: Python 3, a C compiler supporting GNU11, and libm. `CC` selects the compiler. Sanitized mode additionally requires AddressSanitizer and UndefinedBehaviorSanitizer. No game assets, renderer, emulator, network or installation are needed.

## What runs

Each invocation builds six bounded harnesses in ordinary and `RAMPAGE_LOW_MEMORY` configurations:

- **Storage/Skin:** actual scene failure helper, vector growth, mesh cache/publication, instance/item publication and Skin creation/free. Injects every Skin allocation for zero/nonzero vertex counts, first/later vector growth failures, reader/cache/publication failure, overflow and sticky-entry rejection. Asserts existing owners/counts/capacities/bytes survive unsuccessful growth and tracked owners reach zero. A successful fixture hash is recorded; this suite does not contain a historical binary baseline.
- **Models:** actual `place_model` and `vehicle_look`, including all eight/nine synthetic constructor allocation sites, empty/existing look caches, node assignment, successful cache reuse, reader failures and stationary-clip cleanup. Reader/model lookup dependencies are deterministic doubles.
- **Readers:** includes current `tree.c`, `anim.c` and `cloth.c` directly. Exercises eight allocations, zero/free-safe failure outputs, empty particle ownership, summed-count overflow, supported fixture values and rejected cloth mapping/truncated-UV behavior. Cloth mapping OOM must be distinct from historical fallback.
- **Flags:** actual flag constructor, Wii part adapter and shared storage/Skin helpers. All 19 PS2 and 25 Wii synthetic allocation points, including item publication after mesh transfer, mapping rejection/fatal distinction, and cleanup.
- **Monster:** actual monster constructor with supplied borrowed bundles, zero-node tree and synthetic two-mesh geometry. All 18 synthetic allocation points, earlier successful publication before later failure, mapping fallback/fatal and mesh reader failure. Bundle loading and update are doubles.
- **Main guards:** ten source-adjacency checks cover the six public failure boundaries. Exact stop helper and army callback run against a minimal Viewer and lookup doubles to verify success, optional absence, fatal return-as-false and blocked reentry.

City/private look declarations and function bodies are extracted from current source at every run, not copied implementations. Headers are taken from the same source directory. The reader harness uses the actual reader implementation. Allocator ledgers independently assert zero remaining tracked owners in the exercised paths.

## Scope limits

These are bounded functions/slices with small fixtures and explicit doubles, not complete city loading or gameplay. Storage/model/flag/monster teardown mirrors relevant ownership in the harness; it is not actual `scene_free`. The main tests do not execute the complete viewer loop. Real assets, real scene teardown, native target behavior, performance, and exhaustive malformed-input safety need separate evidence.

The larger private validation also covers person/unit look replacement, metadata growth, rail late failure, per-sync stop and empty meshes. Those broader fixtures are deliberately not bundled here to keep the permanent suite small; this package does not claim their full coverage. Independent window/facade/face and other allocation families remain outside the constructor contract.

`--sanitize` explicitly uses `detect_leaks=0` because LeakSanitizer is unavailable in the current ptrace-based execution environment. This is an ASan/UBSan run plus tracked-owner accounting, never a successful LSan claim. Expected injected allocation failures may emit ordinary scene diagnostics on stderr; a nonzero exit fails the runner.

## Retained evidence

Every run creates a fresh `shared-constructor-{normal,sanitize}-*` directory under `SHARED_TEST_OUTPUT_ROOT`, otherwise `TMPDIR`, otherwise `/tmp`. Its path is printed before compilation. Set an existing writable output root if space is constrained. No outputs are deleted or reused.

Retained files include generated source/extracts, binaries, build/run stdout and stderr, exact command/exit receipts, before/after SHA256 pins for the checkout's C/header files, artifact hashes, and `complete.json`. The runner rejects source mutation during testing. Sanitizers halt on their first diagnostic. Test exit zero and `complete.json` together indicate the selected mode finished; interrupted or failed runs retain their partial evidence without a completion record.

To maintain the suite, update fixture assumptions when a deliberate API change requires it, but keep extraction from the live checkout and document any dependency doubles. Do not substitute a saved candidate after a source mismatch.

## City status compatibility

This suite also uses `tests/city_status_extract.py`. It extracts the complete
current private City declaration, MAX_BOOKS, and actual `city_load_failed` /
`scene_failed` definitions into `city-status.inc`; no status result is stubbed.
`city-status-manifest.json` records exact input, extractor, fragment and output
hashes plus source lines. Its allocation-free assertions check null handling,
healthy owners, City-to-Scene sticky propagation, retention after owner reset and
independence of a second City. These are scoped status/ownership fixtures, not a
Book parser, complete City loader or whole-game performance measurement.

All owner programs use the same actual City/status definitions. The callback fixture also rejects a poisoned Viewer City without making dependency calls. The city_build_left and army_update static checks require the exact immediate (Viewer *)v guard, with vehicle_step immediately after the latter. Other previously checked boundaries remain intact. Existing fault loops and output expectations are unchanged.


The failed-world frame guard is now checked at the exact top of the main loop,
before the Xbox memory tick and all capture/audio work. It requires the reviewed
!v.shell condition, the same Scene/exit predicate and immediate leave_world jump.
This replaces the former later guard adjacent to sound_follow_pause; it does not
permit arbitrary intervening work at the new boundary.
