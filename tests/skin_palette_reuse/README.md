# Update-local skin palette reuse regression

Run with a workspace TMPDIR. All generated sources, manifests, build logs and outputs are retained:

```
TMPDIR=/path/to/workspace/tmp sh tests/skin_palette_reuse/run.sh
TMPDIR=/path/to/workspace/tmp sh tests/skin_palette_reuse/run.sh --sanitize
```

Both commands test host and Xbox+low-memory preprocessor configurations. These are host compiler tests, not native Xbox ABI/performance acceptance. Sanitizers use `detect_leaks=0`; no whole-process leak claim is made.

`run.py` extracts the selected current `skin_item`, matrix helpers, `scene_monster_slot`, and the final item loop from `update`. It does not store a copied candidate implementation. The reference is reconstructed by removing only the palette rebuild gate, resetting every palette on every skin call, and removing only cache-key handling from the item loop. All vertex math, mirroring order and item/world handling remain current production code. Exact per-pass operation counts are asserted alongside bitwise position, normal and world-matrix comparisons. Source pattern changes fail extraction rather than silently testing a stale implementation.

The fixture exercises 384 item-loop calls and 3,264 skin visits per build:

- Tree sizes 0, 1, 2, 255, 256 and 257; valid/invalid mirror links; bone-index bounds; present/absent normals; positive, zero and negative weights.
- Two simultaneously live Scenes with separate Instances and outputs, visited in order 0,1,1,0. Instance addresses persist while node matrices change between calls.
- Actual production traversal through A, unskinned B, hidden C, A, B, B, A, A, B, A. With `all=0`, hidden C is skipped; with `all=1`, it is skinned and must invalidate A's cache.
- All four mirror combinations for A and B, plus same-address reset boundaries and instance switches.

Expected aggregate palette build counts are 3,264 -> 2,304, and palette matrix multiplication counts are 418,880 -> 295,680. These are synthetic workload assertions, not gameplay speed estimates.

The synthetic fixture starts after posing/IK and contains no cloth. The complete current cloth body remains in the extracted function and links against current cloth code, but is not exercised here. Actual-asset normal/mirrored/IK/cloth byte comparisons are separate retained integration evidence. The test's unreachable monster-sway stub aborts to catch accidental fixture expansion into that unrelated path.

Optional `--source-root PATH` (or `RAMPAGE_TEST_ROOT`) selects another source tree; `--city-source PATH` selects a private city.c candidate while using the selected tree's headers and cloth implementation. Each run records input and extracted-span hashes. No source file or previous artifact is overwritten or deleted.

Initial integrated runs passed in both normal and ASan/UBSan modes, each in host and Xbox+low-memory host-macro configurations: 384 passes, 3,264 skin visits, palette builds 3,264 -> 2,304 and palette multiplies 418,880 -> 295,680, with bitwise positions/normals/worlds equal. These are host checks, not native Xbox execution. The worker's earlier packaging-time shell launch failed before execution; the integrated runs used available workspace scratch and retained separate receipts.
