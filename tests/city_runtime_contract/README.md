# City runtime contract regressions

Install this directory under `tests/city_runtime_contract` with the matching reviewed City/main/manager source changes. Run `sh tests/city_runtime_contract/run.sh` or `sh tests/city_runtime_contract/run.sh --sanitize`. `SOURCE_DIR` selects an alternate source tree; `TMPDIR` must point to a writable workspace. All generated snapshots, executables and logs are retained. The default layout is LOW_MEMORY; `RAMPAGE_TEST_LAYOUT=ordinary` selects ordinary CPU layouts.

The runner freezes actual source and headers and pins hashes before compilation. It rejects source drift afterward. There is no emulator, graphics context, device audio, target link, network access, or repository write.

## Scope

* Full current City, PRB, Bundle, Vol and manager source: eleven synthetic callback families inject an actual allocation failure while resolving a present parent book. Each requires the sticky failure, zero later host callbacks, suppressed outward event delivery, actual manager destructor ownership accounting, and same-City recovery only after manager destruction and explicit reset. Healthy callback/event assertions prevent a disabled-everything implementation from passing.
* All 97 public failure-entry contracts execute against actual manager functions with their documented count/index/kind/boolean/string/statistics semantics. `sentinel-audit.json` explains each expected return. Each manager is destroyed, and the explicit allocation ledger must return to zero.
* Exact selected main source fragments exercise GE_RESULTS and building-script continuation with an injected host failure. They require no profile/HUD publication or subsequent swarm/effect dispatch. These are consumer tests, not a full main loop or actual world-release test.
* Exact additional main fragments test fatal calls returning apparently valid indices or false predicates: Boss force damage, Rail reaction, script vehicle/unit spawns, optional camera data, free-camera updates and the next-frame stop. Synthetic drivers assert no later queued effect/audio/render publication and preserve healthy/optional-absence outcomes. These do not link the whole Viewer.
* Exact typed callback wrappers from nine Host families plus People require healthy callback invocation, suppression after argument evaluation fails, and false/empty status when a callback writes an output and then fails. A suppressed People event must not be annotated through an invalid last-event index.

The source-level main lifecycle tests with teardown doubles live separately in the compatible moves-lifetimes suite. They must not be described as actual whole-world destructor accounting. Likewise, this suite's zero-owner claims cover only exercised wrapped CPU allocations. ASan/UBSan are enabled in sanitizer mode; LeakSanitizer is explicitly disabled because of the host runtime restriction.

No copyrighted media is copied or required: a valid tiny present parent bundle is constructed in the test. Optional real-volume City/Scene fixtures belong to the separate City-book contract suite; this runner does not silently locate or load user media. Final acceptance still requires the combined source revision, healthy real-scene equivalence, target build checks and appropriate runtime validation. No timing, FPS or whole-process leak result is inferred here.
