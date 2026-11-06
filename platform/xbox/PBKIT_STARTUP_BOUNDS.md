# Optional pbkit startup bounds correction

`patches/nxdk-pbkit-startup-ready-bounds.patch` applies to XboxDev/nxdk commit
`004fefbfe8fd93a5ea8ee9a95ff187b0f58ec8ed`. It changes only the startup clear in
`lib/pbkit/pbkit.c`: use the actual length of the three-element readiness array
instead of clearing five entries. This is independent of the presentation/DPC
investigation.

## Dependency build

Apply the patch to a **private nxdk checkout**, not a shared installed toolchain.
From that private checkout, first run `git apply --check` with this patch's
absolute path, then apply it. Recompile `pbkit.obj` with that dependency's pinned
compiler/settings, rebuild its private pbkit archive, and relink the game against
that archive. Recompiling game sources alone does not incorporate the correction.
Preserve the other five pbkit archive members and all other dependencies.

The validated experiment used LLVM 18.1.8 and the nxdk i386/Pentium III wrapper.
Recompiling the unpatched pbkit member reproduced its original bytes except the
COFF timestamp. The corrected member changed one instruction immediate, 5 to 3,
plus the expected section-checksum metadata. The plain combined game's 52 object
files were reused. Keep temporary files and outputs in a new private directory.

This patch artifact is not automatically applied. CMake options, production
source, dependency selection, and existing defaults are unchanged.

## Validation scope

- Source-extracted host bounds/sentinel regression: original loop fails at
  index 3; corrected loop passes at `-O0` and `-O2`.
- The isolated target candidate preserves all XBE section layouts and changes
  exactly one section-payload byte. Candidate SHA-256:
  `8d4cd12512eb4a4687ed1372636758416154775059f349a61392989736291d4d`.
- A 60.169-second Xemu 0.8.139 cold boot reached the ordinary autosave warning
  and Continue prompt. Twelve sparse frame readbacks passed integrity checks;
  application-error and guest-error logs were empty.
- A separate public-API harness completed two successful `pb_init()` calls and
  two returning `pb_kill()` calls, including one same-process reinitialization.
  Durable guest markers establish this result; both kills restored the sampled
  15,908 available pages from 14,575 after initialization, and every memory
  query succeeded. Harness XBE SHA-256:
  `f5610f6e242530c9a68a2bf67a2ad29d93849fdd681bf36b4f28454b3ebd793e`.
  Recovered log SHA-256:
  `7692d2a48a2b46838cf57031e4a8156eb642728057cd4450f4a9f0cdaef5105c`.

The lifecycle harness submits no extra application frames and idles after its
success markers. Its 60.166-second run, like the game cold boot, was later stopped
by host SIGTERM. Host return code 0 alone is not shutdown or lifecycle evidence.
The harness result is not full-game teardown/reinitialization, pending-present
stress, failure recovery, kernel shutdown-callback coverage, unlimited cycling,
or real-hardware validation. Input progression and sustained gameplay/audio
remain unverified for the startup-only game candidate. Neither result explains
or resolves the separate historical presentation recurrence.

## Later integration, 2026-11-06

The startup-only results and limitations above are historical and remain intact.
The subsequent [scoped runtime integration](RUNTIME_FIXES.md) reuses this patch
once, adds separate reset-drain/DPC/audio fixes, and provides an optional private
pbkit FILEPATH selection. The installed SDK archive remains the CMake default.
Its later runtime evidence does not retroactively broaden this startup test.
