# City property-book failure contract

Current-source tests, not extracted substitute implementations. The test includes the selected complete `city.c` and links actual parsers, Scene constructors/destructors and People/Army managers. Platform file/sound calls are host stubs; allocation and input fault injection are test-only wrappers.

From the repository root:

```
python tests/city_book_contract/run.py --repo . --suite cache --out /your/workspace/fresh-cache-output
python tests/city_book_contract/run.py --repo . --suite all --out /your/workspace/fresh-full-output --vol /path/to/RAMPAGE.VOL --wii-book /path/to/Objects1_props.bdg
```

All output directories must be fresh. The runner retains binaries, logs, exact source pins and command receipts, uses a workspace-local TMPDIR, runs the selected ordinary host or ASan/UBSan mode (both by default when invoking Python directly), and checks its inputs stayed unchanged. It compiles with RAMPAGE_LOW_MEMORY. `--city` and `--managers` can select explicit source overlays, but prefer one immutable full source snapshot.

The synthetic cache suite needs no copyrighted game assets. Real direct-loader sweeps, PS2/Wii precedence, and full Scene tests require the owner's existing assets. `--suite scene` requires `--vol`. The full Scene cases are LasVegas/LV01 and NewYork/NY01, with early Objects1/building-book failures and a late actual camera-book allocation failure injected after real crowd creation. No media files are changed.

Checks include all direct loader allocations; stable self-referential Books; bounded true absence without success-cache poisoning; present read/parse/no-book failures; absence at full success capacity; parent inheritance; nested City selection; active resolver lifetime; explicit same-City recovery only after actual teardown; eight City runtime failure boundaries; and zero tracked CPU owners at completion. The late constructor hook is synthetic fault placement around real people_crowd, not a claim that unchanged media naturally loads camera there.

The suite intentionally requires the real city_load_failed, city_reset_load_failure, scene_failed and People failure callback APIs. It does not provide success-only status stubs. Failed Scene operations keep their owners alive until their caller performs actual teardown. Successful Groups remain borrowed from City until city_close.

ASan/UBSan use detect_leaks=0 due to the host runtime restriction. The explicit ledger observes allocations in the linked compilation units, not every process, renderer or audio allocation. These are not whole-process LeakSanitizer, GPU, emulator, or Xbox runtime tests.

`run.sh` dispatches the asset-free normal checked-open and cache tests by default, or only ASan/UBSan with `--sanitize` and accepts all run.py arguments. `REPO_ROOT` chooses the repository root, `SOURCE_DIR` chooses an immutable complete source directory independently, and `CITY_BOOK_TEST_OUTPUT` chooses a fresh retained output directory. For example:

```
SOURCE_DIR=/workspace/frozen/src REPO_ROOT=/workspace/repo CITY_BOOK_TEST_OUTPUT=/workspace/new-output tests/city_book_contract/run.sh
```

The final cache representation shares the existing 32 stable slots between negative metadata and successful owners. It never moves or evicts a success. The cache tests prove a 17-key negative working set fits, reset preserves holes and successful addresses, all 15 PS2/Wii reader-allocation fault points fail safely while evicting a negative slot, and all 32 successful borrowed Books remain stable across reset. A missing optional key still returns absence at full successful capacity; only a present new book makes capacity fatal.

## Checked-open outcome regression

`city_open_checked(vol, name, &failed)` reports failure before destroying its tentative City. NULL with failed=0 means absent required media; NULL with failed=1 means invalid input/key, an allocation failure (including initial City or final model-key allocation), or present-data read/parse/inheritance failure. Success clears the output to 0 and selects the new resolver. Every failed open preserves the previous resolver, including its sticky failure status. `city_open` is the legacy wrapper; it cannot distinguish these outcomes.

The asset-free default includes `open_probe.c`, which sweeps all 20 PS2 and 29 Wii synthetic open allocation points. It also checks missing City/Props, optional absent inherited parent, present short reads, bad headers and no-Book payloads, overlong keys, NULL output, legacy calls and prior ownership/resolver restoration. Adding `--vol` includes the actual LasVegas open allocation sweep. `--suite open` runs only this affected open proof. `--suite focused --vol /path/to/RAMPAGE.VOL` adds one real LasVegas Scene construction/early-failure/late-failure and explicit same-City recovery case, plus the existing eight small runtime-boundary probes, without replaying the unchanged full cache sweep or other cities. `--suite all` includes every case.

The runner uses `-g0` to bound retained binary storage; ASan/UBSan instrumentation and allocation ledger checks remain enabled. See the exact command receipts for each run.

### Standard aggregate mode selection

`tests/city_book_contract/run.sh` runs only normal host tests.
`tests/city_book_contract/run.sh --sanitize` runs only ASan/UBSan tests.
Both shell calls include the checked-open tests plus the asset-free cache suite, and each needs a fresh output directory. Optional `--suite`, `--vol` and `--wii-book` arguments are forwarded unchanged. `--suite open` limits a call to checked-open tests; `--suite focused --vol ...` adds focused real Scene checks.

Direct `run.py` defaults to both modes for the retained private recipes. `--mode normal`, `--mode sanitize`, or `--mode both` select explicitly; `--sanitize` is an alias for `--mode sanitize`. The shell passes `--mode normal` before user arguments, so a trailing `--sanitize` selects sanitizer only rather than running both twice.
