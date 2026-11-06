# Scoped Xbox runtime fixes, 2026-11-06

This working-tree integration preserves the prior startup-only investigation and
adds the reviewed, trace-free presentation and synchronized audio changes. It is
not a release, all-level test, physical-controller test or real-Xbox acceptance.
The repository's existing unrelated modifications are retained.

## Changes and safety boundaries

Apply these dependency patches only to private copies, in order:

1. `nxdk-pbkit-startup-ready-bounds.patch`, once, on nxdk
   `004fefbfe8fd93a5ea8ee9a95ff187b0f58ec8ed`.
2. `nxdk-pbkit-dpc-scanout-fallback.patch`: the direct DPC VBlank caller may
   correct a missed scanout address selection. The original ISR remains early;
   passive/recovery callers do not acquire fallback authority. No extra queue
   consumption or interrupt acknowledgement is added.
3. `nxdk-pbkit-reset-drain-lifecycle.patch`: includes the exact guard header as
   a new-file hunk. Serialized init/render/kill lifecycle gates retain the native
   ordering. GLES must drain a nonempty accepted batch before publishing reset,
   and retain the post-reset busy check. This is not a GPU completion ticket.
4. `nxdk-gles11-reset-drain.patch`, on the already patched exact-palette GLES
   source described in README. It calls PRE / native reset / POST in that order.

The guard uses five guest seconds and 1,048,576 polls, checked when scheduled.
It is not a hard wall-time deadline. Timeout/context failure is terminal, not a
retry or reset heuristic. Fatal text, I/O status blocks and unexpectedly pending
handles retain static lifetime. A terminal error or system shutdown never clears
its safety state. Successful native cleanup must return before a new serialized
instance may bind an owner. Native teardown is still unbounded; concurrent
teardown, partial-init recovery and kernel pending-I/O termination guarantees
are outside the claim. No tracing/HUD code or standalone-harness
`XVideoSetVideoEnable` call was added to the game or driver.

`src/audio_xbox_hal.c` now publishes descriptor payload, queue counts and LVI
under `KeSynchronizeExecution` with the existing interrupt object. The plain HAL
is exactly the reviewed candidate, SHA-256
`ed25a4cb604b1b85a9457b5101144bdde3a9a30f8665a7825f038da25e0b6867`.
It adds no queue recovery, reset or acknowledgement cleanup. The source-backed
host race model reproduces the old lost-completion schedule at all 32 ring
positions and clears it with synchronized publication. Existing callback/DPC
coalescing limits remain; this is not proof of all audio failure causes.

## Explicit dependency selection

`RAMPAGE_XBOX_PBKIT_LIBRARY` is a CMake FILEPATH override. Its default remains
`${NXDK_DIR}/lib/libpbkit.lib`, the original installed SDK archive. The override
changes only the pbkit link operand and rejects a nonexistent path. It does not
patch any dependency or select GLES for you. Select the matching private,
reset-drain-patched GLES tree with `NXDK_GLES11_DIR`; retain its reviewed private
texture layout, telemetry defines, GL prefix and exact-palette configuration.
A mismatched GLES/pbkit pair is unsupported.

The exact runtime-tested pair is pinned in `runtime-dependencies.json`:

- pbkit SHA-256: `40ee911f38721745cd5ed79b2e20389e6b9f09d0f590d094cd95a23415e0c46b`.
- GLES SHA-256: `a176431d9698db88efea4acf59ca2ff5ab8b697aaf0af2cdd26bab1fb6e26be2`.

The reviewed build reproduced native/startup/GLES controls except COFF time,
then replaced only `pbkit.obj` and `nv2a_helper.c.obj`, preserving all other
members and their order. LLVM 18.1.8, original virtual source identities,
`-pipe`, pinned headers and exact original GLES flags matter. A fresh ordinary
GLES build is not automatically that exact archive. The default CMake build does
not reproduce the runtime-tested configuration. No target rebuild or archive
copy was performed for this source integration.

`tools/verify_xbox_runtime.py` requires explicit baseline pbkit/GLES source
paths, both reviewed archive paths and a new output directory outside the
repository and dependency input directories. It refuses hash drift, wrong
ordered members, mismatched archives and existing output. It replays the four
patches in memory at exact line positions without offsets/fuzz, checks the final
source hashes and writes only the small replayed sources and a proof. The GLES
input is the pinned pre-reset helper after the earlier graphics changes, not
an arbitrary pristine upstream file. The helper never rebuilds, modifies shared
sources or invokes a historical relink script.

## Source-matched host checks

See `tests/xbox_runtime/README.md` for the fresh-extraction workflow. All relevant
source fragments and pinned stubs are checked before reusing sealed host test
binaries. This integration passes:

- 34 guard, 53 caller, 29 lifecycle, 140 fallback, 20 reset-model and 25 formatter
  cases through the previously compiled ASan/UBSan harnesses.
- Five startup sentinel cases each at O0/O2, plus the original out-of-bounds
  negative control.
- 66,504 HAL model cases per baseline/candidate arm at O0/O2 under ASan/UBSan.
- 24 verifier/refusal checks, including extracted CMake default/override/missing
  archive execution. This last check is not a full configure or target build.

The models use explicit scheduling assumptions and stub native resource/MMIO
bodies. They do not emulate hardware or replace runtime evidence.

## Completed runtime evidence, kept separate

All runs below are bounded software-emulator tests using preserved private
artifacts. They are not one all-encompassing test of the final binary.

- **660.166-second natural audio/graphics regression**
  (`audio_dma_natural_v1`): four complete startup films (5,213 pictures, no failed
  uploads) and four ordinary menu-return cycles. Settled heap minus PCM changes
  by -832 bytes, range 928 bytes. All 314 sampled DMA rows advance; ten samples
  are empty before servicing and refill immediately, with none empty afterward.
  Historical terminal silence is absent. Independent source-image comparison
  finds no substantive sampled rollback. It does not cover every frame;
  Pipeworks lacks independent source ordinals. This run predates the separate
  movie-padding fix.
- **270.36-second full-Wii integration** (`wii_full_integrated_v1`): exact
  Dallas/DA01_Outskirts/Boris, 129 Wii bundles, one synthetic player and no bots.
  All 37 action/lifecycle gates and 33 negative mutations pass. Legal fresh-button
  recovery settles at guest 92.607 s; independent walks measure 20.039 and 25.105
  units, DONE at 96.674 s. Later neutral defeat is expected combat, not a second
  respawn pass. All 127 DMA samples advance; all 119 after SDL 20 s are nonempty
  before and after servicing. Sampled free RAM bottoms at 8,700 KiB. Forty-eight
  rounded title-FPS values have median 17, range 3–21; they establish no speedup
  or smooth-performance claim. Capture integrity and sampled pixels pass, not
  every intervening presentation. This run also predates the movie-padding fix.
- **Separate movie-padding regression**: the later 230.33-second four-film
  waveform check and decoder tests belong to
  [XBOX_MOVIE_TERMINAL_PADDING.md](../../docs/XBOX_MOVIE_TERMINAL_PADDING.md).
  It must not be represented as a repeat of the late-stall or four-menu-cycle test.
- **Final plain smoke** (`plain_final_smoke_v1`): 45.162 seconds, one intact
  640×480 readback confirms the normal autosave Continue dialog. XBE SHA-256
  `3b51299feaa78c52fad91a75b47c9e4895ec3f3f7c9e2f5f67aac2c74654c481`.
  This final non-fixture executable includes all three groups of fixes. No
  movie playback, WAV capture, gameplay or input fixture was used in this smoke.

The separate two-lifetime presentation probe required an explicit video-output
re-enable in its standalone harness. Its RGBRGB/CMYCMY result does not justify
adding that call to the game or driver, or establish universal recovery.

Evidence is preserved outside the repository at
`rampage-runtime-evidence-20261106/`. Immutable report identities:

- `analysis_audio_dma_independent_v1/REPORT.md`:
  `23c17754e1a911acc157ff48f33a6a8217e72a366dd16a29d04d84f7ee6b0506`.
- `analysis_audio_dma_frames_v1/RESULT.md`:
  `f0e6fc7839433de81e5f07880cff2b1337ea69c0cac56b71780d0a39a01753ac`.
- `analysis_wii_integrated_independent_v1/REPORT.md`:
  `24f53ddcfbc355e0c4eca317be03d983ce42f212300596f7e99a31a7ebef20d3`.
- `analysis_runtime_integration_applied_v1/` records this exact source replay,
  host checks, CMake/refusal tests and preserved working-tree baseline.

Physical controllers, real Xbox/SPDIF, all levels/objectives, long sessions,
complete listening/AV synchronization and universal presentation stability
remain unverified. No proprietary assets are copied or published here.
