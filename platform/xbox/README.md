# Original Xbox build (work in progress)

This directory is an experimental nxdk target, not a verified playable release.
It uses the native RampageRE code rather than the PS2Recomp reference runner.
Original game data is never included in the source repository or build output.

Dependencies validated so far:
- XboxDev/nxdk commit 004fefbfe8fd93a5ea8ee9a95ff187b0f58ec8ed, with SDL2 and pbkit built.
- Ryzee119/nxdk-gles11 commit 5a726e61f1af5c3e9682af2c833c9703cab5022e and its pinned gl4es dependency 81547d986798e876de8b434193920b606a72363f.
- Apply `patches/nxdk-gles11-default-state.patch` to that nxdk-gles11 checkout. The patch synchronizes GL's initial enable flags with NV2A/pbkit hardware state; without it indexed geometry can be culled despite GL culling being disabled.
- Apply `patches/nxdk-gles11-staging-pages.patch` after the default-state patch. It isolates staged draws by page and waits for GPU completion before reuse.
- Apply `patches/nxdk-gles11-lighting-normal.patch` next. It refreshes the inverse normal matrix when lighting is enabled after unlit draws; a six-cell Xemu probe verified directional lighting no longer renders black. Rebuild the GLES library and game after applying it.
- Apply `patches/nxdk-gles11-light-cache.patch` after the lighting-normal patch. It restores light parameters on re-enable, refreshes color-material products, and pairs each lighting push-buffer begin/end. Host regressions and a 600-frame Xemu six-cell transition probe passed. Rebuild the GLES library and game afterward.
- For exact P8 world textures, apply `patches/nxdk-gles11-texture-telemetry.patch` and then `patches/nxdk-gles11-exact-palette.patch` after the lighting patches. Rebuild the complete GLES library because its private texture layout changes. Configure the game with `-DRAMPAGE_XBOX_EXACT_PALETTE=ON`; the default is OFF. This stores eligible immutable world textures without quantization and retains RGBA fallback. HUD and movie uploads are excluded.
- Apply `patches/gl4es-light-enable-ownership.patch` to the pinned GL4ES checkout and rebuild its library. It prevents a duplicate light-enable allocation when `GL_ENABLE_BIT` and `GL_LIGHTING_BIT` are pushed together. Source-backed ownership/state tests and a four-return Xbox fixture pass; this is not an unlimited-session memory guarantee.
- Apply `patches/nxdk-pbkit-startup-ready-bounds.patch` once to a private nxdk source copy, then `patches/nxdk-pbkit-dpc-scanout-fallback.patch` and `patches/nxdk-pbkit-reset-drain-lifecycle.patch` in that order. The last patch includes its guard header. Preserve the installed toolchain and use an explicitly selected private archive.
- Apply `patches/nxdk-gles11-reset-drain.patch` after the preceding GLES patches. This dependency must be paired with the reviewed pbkit reset-drain implementation. The pbkit override alone does not patch or select GLES. See [runtime fixes and exact dependency pins](RUNTIME_FIXES.md).
- Apply `patches/nxdk-gles11-pushbuffer-room.patch` after the reset-drain patch. pbkit never checks for room in its 512 KiB push buffer; a draw-heavy frame wrote past it and the GPU stopped on "invalid data". Before each draw a nearly full frame now drains the GPU and restarts at the buffer head, as the end of every frame does.
- Apply `patches/nxdk-pbkit-kick-batching.patch` last among the pbkit patches. Every `pb_end` flushed the write-combine cache and updated the GPU's PUT register; a city frame makes thousands of small blocks and that alone was over a tenth of CPU time on the console. After initialisation `pb_end` now submits every 4 KiB, and everything that waits on the GPU (`pb_busy`, `pb_finished`, `pb_wait_for_vbl`, `pb_reset`, `pb_kill`) submits what is outstanding first.
- `src/xbox_fastmem.c` replaces `memcpy`/`memmove`/`memset`/`strlen`/`memcmp`. An nxdk whose PDCLib was built without optimisation ships byte-at-a-time versions that were a fifth of frame time; objects win over library members at link time, so the installed nxdk is untouched. The mixer's per-voice resampler (`src/sound.c`) keeps its position in fixed point on Xbox for the same reason (x87 float-to-int per sample).
- A supported LLVM version (18.1.8 used for initial compilation; nxdk warns against LLVM 19 through 20.1.2), CMake, and nxdk build prerequisites.

Source nxdk's environment, then configure with its toolchain file and set
`NXDK_GLES11_DIR` to the private patched checkout. Select its matching pbkit
archive with `-DRAMPAGE_XBOX_PBKIT_LIBRARY=/absolute/private/libpbkit.lib`.
The FILEPATH override defaults to the original installed SDK archive; ordinary
default builds do not reproduce the reviewed runtime-tested dependency pair.
For that pair retain the existing exact-palette ABI/defines and configure
`-DRAMPAGE_XBOX_EXACT_PALETTE=ON`. A full new GLES build is not automatically
byte-identical to the tested archive. The read-only verifier and precise
selection/reproduction limits are documented in [RUNTIME_FIXES.md](RUNTIME_FIXES.md).
An offline gl4es checkout can be
provided through CMake's `FETCHCONTENT_SOURCE_DIR_GL4ES` variable. Build the
`rampage_xbox` target; CMake produces `default.xbe`. An XISO must be packaged
separately from this executable and the user's own supported game data.

Current Xbox adaptations target a stock 64 MiB machine. Optional PC texture
packs and post-processing are disabled; original game textures are used.
The application includes the tested emulated-codec audio path, movie/audio clock,
output refill, lower-memory building rows, bounded idle PCM cache and narrow
window-occupant reuse. Low-memory scene vectors grow in smaller increments;
failed growth preserves the old allocation before aborting rather than
continuing with invalid storage. This is fail-fast containment, not graceful
out-of-memory recovery. A renderer output callback also services sound during
long upload/draw batches without advancing the game clock.

Current combined synthetic fixtures passed the four natural startup movies
(5,213 pictures, zero failed uploads), four ordinary LasVegas/LV01 menu-return
cycles, and the repeated 19-state four-character NY10 input sequence. Settled
non-PCM heap use across the four returns varied by 1,024 bytes, with a net
change of -952 bytes. The 180-second NY10 recording has no exact stereo-zero
gaps of at least 10 ms after its initial startup silence and no full-scale
samples; these measurements are not complete listening or AV-sync acceptance.

Later source-matched tests support the scoped trace-free DPC fallback,
reset-drain and synchronized audio-publication changes now integrated here.
The 660-second natural regression clears the historical terminal audio stall
and shows no substantive sampled movie rollback; the 270-second full-Wii test
passes all 37 scoped action/lifecycle gates. These are separate bounded tests,
not an all-frame or universal AV guarantee. The independently validated
movie-padding correction is covered separately, and the final plain executable
reaches the ordinary Continue dialog in a 45-second smoke test. See
[RUNTIME_FIXES.md](RUNTIME_FIXES.md) for exact artifacts, metrics and limits.
Physical controllers, all levels, full objectives and real-Xbox performance
remain unverified. Do not treat compilation or one fixture as complete
gameplay/AV acceptance.
