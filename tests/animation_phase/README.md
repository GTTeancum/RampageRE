# Animation phase reuse regression

Run from the repository with TMPDIR set to a new writable workspace directory:

```
./tests/animation_phase/run.sh
./tests/animation_phase/run.sh --sanitize
```

All generated source, manifests, binaries and logs are retained. The runner refuses an unspecified TMPDIR to avoid a full system /tmp. `RAMPAGE_TEST_ROOT` optionally points to a private repository snapshot; normal use derives the repository root from this test directory.

The extractor uses current full anim.c and current complete pose_instance plus actual matrix/root/monster-slot helpers. Historical `reference_clip_pose.c` and `reference_pose_instance.c` are pinned by `reference_manifest.json`. Their only substitutions are function renames and directing the old instance to the old clip sampler. The matrix/root helper implementations are shared current implementations, intentionally not mocks. This suite protects phase reuse, not independent edits to those shared helpers.

Each host and Xbox+low-memory host-macro configuration runs 6,030 public-wrapper output/return/work-count comparisons and 11,520 full-pose cases. Translation/quaternion, local poses and complete hierarchy matrices use bitwise comparisons. Bone masks, absent layers, missing records, empty tracks, invalid bones, zero/negative durations, negative/segment/loop boundary times, root/turn/look transforms and blends are covered. Inputs are finite and well-formed; malformed clips and NaN/infinite inputs are not a new safety guarantee.

Every case asserts exact fmod call counts: one per eligible baseline bone versus one per used candidate body/face clip. The balanced synthetic totals 294,180 -> 7,620, not a measured gameplay ratio or speedup. ASan/UBSan are enabled in sanitizer mode, with LSan disabled. Host macro builds do not execute Xbox instructions. No whole-game leak or physical-frame-rate claim follows.
