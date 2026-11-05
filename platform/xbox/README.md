# Original Xbox build (work in progress)

This directory is an experimental nxdk target, not a verified playable release.
It uses the native RampageRE code rather than the PS2Recomp reference runner.
Original game data is never included in the source repository or build output.

Dependencies validated so far:
- XboxDev/nxdk commit 004fefbfe8fd93a5ea8ee9a95ff187b0f58ec8ed, with SDL2 and pbkit built.
- Ryzee119/nxdk-gles11 commit 5a726e61f1af5c3e9682af2c833c9703cab5022e and its pinned gl4es dependency 81547d986798e876de8b434193920b606a72363f.
- Apply `patches/nxdk-gles11-default-state.patch` to that nxdk-gles11 checkout. The patch synchronizes GL's initial enable flags with NV2A/pbkit hardware state; without it indexed geometry can be culled despite GL culling being disabled.
- A supported LLVM version (18.1.8 used for initial compilation; nxdk warns against LLVM 19 through 20.1.2), CMake, and nxdk build prerequisites.

Source nxdk's environment, then configure with its toolchain file and set
`NXDK_GLES11_DIR` to the patched checkout. An offline gl4es checkout can be
provided through CMake's `FETCHCONTENT_SOURCE_DIR_GL4ES` variable. Build the
`rampage_xbox` target; CMake produces `default.xbe`. An XISO must be packaged
separately from this executable and the user's own supported game data.

Current Xbox adaptations target a stock 64 MiB machine. Optional PC texture
packs and post-processing are disabled; original game textures are used.
FMV, menu, game input, audio, memory and visual behavior still require
integrated testing; do not treat successful compilation as gameplay proof.
