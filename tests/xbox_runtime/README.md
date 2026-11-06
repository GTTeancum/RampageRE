# Scoped runtime dependency regressions

These tests reuse the sealed small host binaries only after freshly extracting
current/replayed source and proving byte identity with every relevant frozen
source, stub and binary. They create new evidence, never modify the reference
runs, compile target objects, copy archives or invoke an emulator. The external
review fixtures are required; no game assets are needed.

From the workspace containing the repository and preserved review directories:

```sh
export PYTHONDONTWRITEBYTECODE=1
export TMPDIR=/absolute/new-workspace-scratch
G="$PWD/rampage-validated-build/pbkit-trace-free-target-20261106"
A="$PWD/rampage-validated-build/audio-dma-stall-target-20261106"
N="$PWD/rampage-xbox-toolchain/nxdk"
O=/absolute/new-workspace-evidence
python RampageRE-Xbox/tools/verify_xbox_runtime.py \
  --pbkit-source "$G/inputs/pbkit.baseline.c" \
  --gles-source "$G/inputs/nv2a_helper.baseline.c" \
  --pbkit-archive "$G/build-v1/archives/libpbkit.lib" \
  --gles-archive "$G/build-v1/archives/libGLESv1_CM.lib" \
  --output "$O/dependencies"
python RampageRE-Xbox/tests/xbox_runtime/run.py \
  --graphics "$G" --audio "$A" --nxdk "$N" \
  --verified-sources "$O/dependencies" --output "$O/host-tests"
python RampageRE-Xbox/tests/xbox_runtime/test_verifier.py \
  --pbkit-source "$G/inputs/pbkit.baseline.c" \
  --gles-source "$G/inputs/nv2a_helper.baseline.c" \
  --pbkit-archive "$G/build-v1/archives/libpbkit.lib" \
  --gles-archive "$G/build-v1/archives/libGLESv1_CM.lib" \
  --nxdk "$N" --output "$O/helper-tests"
```

Create the parent output directory first. Each selected output itself must not
already exist and must stay outside the repository/reference dependency trees.
Use `--cmake /absolute/path/to/cmake` if it is not on PATH; any needed host shared
library path must also be set. The preserved toolchain's host CMake uses its
`host/usr/lib/x86_64-linux-gnu` library directory. No files are written to /tmp.

`host-pins.json` records the reviewed sources/stubs and existing binaries,
validated against the prior graphics/audio freeze manifests. `run.py` freshly
extracts guard, glFlip, native lifecycle bodies/boundaries, DPC/ISR handlers,
reset functions, startup loops, PDCLib formatter and HAL/Xemu DMA functions.
Known platform/MMIO observation substitutions are explicit. Every extraction
must match before any binary runs. No stale copied snippet is accepted as a
fresh source check. The startup negative binary must report the known index-3
bounds failure; the HAL baseline arms must reproduce their expected stalls.

Coverage: 34 guard, 53 caller, 29 lifecycle, 140 fallback, 20 reset-model,
25 formatter, five startup cases at both O0/O2, and 66,504 cases per HAL arm
at both O0/O2. Guard/caller/lifecycle/fallback/reset/formatter and HAL binaries
use their sealed ASan/UBSan builds. CMake tests run only the exact extracted
selection block, check the link operand, and cover default/explicit/missing
archive selections. This is not full configure/build acceptance.

The baseline/candidate models are conditional on their scheduling assumptions.
Native resource bodies and kernel behavior are stubbed; they do not prove GPU
causality, real hardware correctness, unlimited runtime or error recovery.
See [runtime scope](../../platform/xbox/RUNTIME_FIXES.md) for the separate
completed runtime evidence and default-build limitations.
