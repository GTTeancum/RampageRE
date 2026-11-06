# Xbox movie terminal-padding correction

## Change

The movie decoder previously ignored SPU frame flags. Three preserved PSS files end each channel with a valid flag-1 frame, followed by padding which decoded as a 28-sample +28,672 plateau. The existing movie output gain turned that into a captured +21,363 stereo pulse. This was distinct from the separately corrected DMA stall.

The movie-only correction recognizes exact flag 1 only when the SShd header explicitly identifies type-0x10 SPU ADPCM, has at least 24 bytes, and marks both loop offsets 0xFFFFFFFF. It decodes all 28 valid terminal samples first, then silences later slots in that channel. Channel state belongs to the zero-initialized Movie lifetime. Compressed consumption, block cadence, return counts, resampling, demux and video EOF are unchanged. Unknown, short, looping or headerless formats and other frame flags retain their prior behavior. Sound effects and music are untouched.

## Host and build evidence

The focused repository suite is `tests/movie_terminal_padding`. Fresh integrated-source runs passed 679 normal and 679 ASan/UBSan cases, including complete Midway, Digital and Intro PCM comparisons. All prior samples remain identical, and total output counts are unchanged. Only the final 28 stereo frames per file change to silence. Sanitizer coverage is extracted audio/demux, not video initialization; leak detection was disabled.

The natural-fixture and plain objects were each built from their separate baseline source and exact original compilation context. Each baseline object reproduced its preserved original apart from COFF timestamp. Each candidate replaced only the movie object, preserving synchronized HAL, frozen graphics archives and every other game object. Plain source is byte-identical to natural source after removing six existing fixture-only diagnostic blocks.

Integrated production source SHA256: `565f3600479041a64e0a5604646144658159290203fbf01b14b4cb051a9abfd3`.

Final plain XBE: 3,489,792 bytes, SHA256 `3b51299feaa78c52fad91a75b47c9e4895ec3f3f7c9e2f5f67aac2c74654c481`. Its retained PE is separate provenance, not disposable scratch. No assets are distributed with these changes.

## Scoped runtime result, 2026-11-06

An independently audited 230-second natural-fixture recording completed all four startup films, including Pipeworks. Picture/audio-frame totals were unchanged and every film reported failed=0. For each film, a locally aligned comparison preserved the preceding three seconds and following 996 stereo frames byte-for-byte, changing exactly the intended 28 stereo padding frames from (+21,363,+21,363) to zero. Each film's nonzero scalar-sample counter fell by exactly 56. Pipeworks has direct captured-output evidence here; its compressed source was not independently decoded.

All 106 sampled DMA telemetry rows progressed and audio continued into initial gameplay. Two post-startup empty-before-refill queue observations refilled immediately; this is not proof against transient underruns. Intro reported substantial dropped video frames, so counts do not establish presentation of every frame or full AV-sync quality.

The final plain build separately completed a 45.162-second smoke run. A correct 640x480, sentinel-clean capture at 30.001 seconds visibly showed the ordinary Continue/autosave dialog, and `rampage.err` was empty. This smoke verifies startup/dialog rendering, not another full four-film waveform run.

The short movie run did **not** repeat all four gameplay/menu-return cycles, the historical approximately 448-second late-stall interval, long-session heap behavior, temporal graphics recurrence, all gameplay, physical controllers or real Xbox/SPDIF hardware. Earlier complete DMA regression belongs to its own preserved binary/run, although the HAL remained unchanged here. This correction is not universal gameplay or audiovisual acceptance.
