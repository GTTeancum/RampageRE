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
- Apply `patches/nxdk-pbkit-room-hook.patch` after it. `pb_begin` calls a hook when less than 24 KiB is left before the tail; nxdk-gles11 restarts the push buffer from it. The last defence for blocks sent with no draw between them (a level's loading), which the per-draw check does not see.
- Apply `patches/nxdk-gles11-static-arrays.patch` after the pushbuffer-room patch. gl4es has no buffers on its ES 1.1 backend, so every draw's arrays were copied into the staging arena after scanning its indices. The renderer registers each unskinned scene part's arrays (`gliStaticArrayAdd`, `src/render_gl.c` `xbox_part`); they are copied once into contiguous memory and a draw under `gliStaticArraysHint(1)` reads them there. Nothing is looked up without the hint, so an address reused by other data cannot pick up a stale copy; released copies are freed with the GPU idle. Parts also draw with 16-bit indices (half the inline index bytes, no conversion in gl4es).
- Apply `patches/nxdk-gles11-frame-pacing.patch` last. pbkit shows a finished frame at a vertical blank by itself; `glFlipNV2A` waited for one more on top of that, about 8 ms of every frame slower than the display.
- Apply `patches/nxdk-gles11-deferred-restart.patch` last. Restarting the push buffer drains the GPU; made at the frame's end, the CPU stood still while the GPU drew the end of that frame (at 1280x720 about a quarter of the frame time). The restart is now made before the next frame's first draw, after the game's own work for that frame.
- Apply `patches/nxdk-gles11-staging-pack.patch` after them. The staging arena gave every draw its own 4 KiB page (for Xemu's page-granular dirty tracking); some hundreds of small draws a frame then filled the 2 MiB arena several times a frame, each refill a wait for an idle GPU. Draws now share pages unless `gliStagingPagePad(GL_TRUE)` is called.
- Apply `patches/nxdk-gles11-direct-draw.patch` last, and `patches/gl4es-xbox-predraw.patch` to GL4ES after its light-enable patch. Together they are a draw path that does not cross GL4ES or the backend's client arrays: `gliDirectDraw` names arrays that are already in GPU memory (`gliStaticArrayAlloc`, `gliStaticArrayAddress`) to the NV2A by address and sends the indices, with the GL state as it stands; the application asks `gl4es_xbox_predraw()` first, which does GL4ES's pending texture work and says when a draw needs GL4ES after all. The renderer's batches, registered parts and CPU-posed skins all draw this way. `gliSkinDraw` poses a skinned mesh on the GPU with a vertex program (source `shaders/skin.vs.cg`, compiled words embedded in `gles_xbox_direct.c`; rebuild with nxdk's `tools/cg/win/cgc.exe -profile vp20` then `tools/vp20compiler/vp20compiler.exe`): 26 bone matrices a draw as constants 96 and up, one directional-light set, fog by eye depth. The game no longer poses monsters, people or army units on the CPU (`Skin.gpu_skin`, `scene_skin_palette`); cloth and any part the program cannot draw (an environment or damage layer, a texture matrix) fall back to the CPU path.
- Game-side cuts for the console: `getenv` is a constant "not set" in the game's sources (`src/xbox_builtins.h`), instances out of sight have their bones posed one frame in four, and the sound mixer sums the voices in whole numbers (`src/sound.c`).
- `src/render_gl.c` on Xbox: the standing, plainly drawn parts of a block are gathered by what they are drawn with into world-space batches built straight into the static store (`bat_frame`), and each frame a batch sends the indices of the items in sight; an item that changed lately is drawn part by part. A run of parts sends only the state that differs from the part before (`xs_*`). The HUD's and the effects' quads are drawn as arrays (`qb_*`), the shadows from their vertex arrays. `scene_update` does not pose a skin the last frame's culling left out. The display is 640x480 pixels, interlaced or progressive as the console's settings and cable give it; with Widescreen set on the dashboard the picture is drawn for a 16:9 screen (the game is told a window 853 wide, the renderer maps it onto the 640 pixels). 1280x720 was drawn correctly but leaves a 64 MB console too little memory. The people and other small skins are posed on alternate frames.
- `src/xbox_fastmem.c` replaces `memcpy`/`memmove`/`memset`/`strlen`/`memcmp`. An nxdk whose PDCLib was built without optimisation ships byte-at-a-time versions that were a fifth of frame time; objects win over library members at link time, so the installed nxdk is untouched. The mixer's per-voice resampler (`src/sound.c`) keeps its position in fixed point on Xbox for the same reason (x87 float-to-int per sample).
- `src/xbox_builtins.h` is force-included in every Xbox source (the game, gl4es and nxdk-gles11). nxdk compiles with `-fno-builtin`, which made every small `memcpy` and every `sqrtf` a library call; the header names the compiler builtins again. The skinning loop (`src/city.c` `skin_item`) blends the bone matrices with SSE.
- The batches (`src/render_gl.c` `bat_frame`) hold their members part by part: a part that changes leaves its batch (its triangles are no longer sent, the batch is not built again for that) and comes back later, later each time it has changed; blended parts have batches of their own, drawn after the opaque world.
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
