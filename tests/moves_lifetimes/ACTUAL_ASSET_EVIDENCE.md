# Optional actual-asset evidence

The November 6, 2026 private audit separately tested the unchanged move graph
parser, current runner versus the legacy allocation strategy, and production
director AI steps on authorized PS2 George/Lizzie/Ralph and Wii Boris graphs.
Assets and private evidence are not included or required by this test suite.

Normal and ASan/UBSan runs produced the same results:

- 98,083 exact runner/window/input comparisons.
- 69,224 pad steps and 8,508 production AI steps.
- 312 event switches and 231,900 matching deterministic script-check calls.
- Final trace digest: `cad470f96f81e4a1`.
- PS2 graphs: 176 states, maximum 24 transitions/state, 48 requested window
  bytes per runner. Boris: 181 states, maximum 25, 50 requested bytes.
- In the deliberately dense traversal, 103,728 allocations became four startup
  allocations, with no window allocations during transitions and none retained
  after stop. This is not measured gameplay frequency or a hardware speedup.

The private audit manifest records input member offsets/hashes, exact sources,
commands and executable hashes. Consult that audit artifact when reproducing
the asset-dependent comparison with independently authorized local assets;
this repository does not contain a hidden path, downloader or automatic asset
discovery step. Real script execution and world-facing destroy-building AI
were not part of that differential harness.

The graphs' maximum-sized reservation may retain more requested memory during
small/zero-transition states than the old per-state arrays, up to 46/48 bytes
for these graphs. The dense traversal's peak window bytes did not increase.
Allocator metadata/padding and target-process memory were not measured by
that component test.
