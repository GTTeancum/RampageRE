# Xbox audio allocation regression

Run from the repository root with a new evidence directory:

```sh
python3 tests/audio_cache/run.py --output ../audio-cache-test --sanitize
```

Python 3 and a host C compiler are required. No game assets, SDL installation,
audio device, Xbox toolchain, emulator, network access or dependency installation
is used. Compiler scratch stays in the new output directory. Existing output
directories are refused and failed-run evidence is retained. Budget 8 MiB of
artifact space. The largest requested synthetic PCM working set is about 4.4 MiB.

## Coverage

- Current complete `sound.c`, with only platform declarations and unexercised
  SDL startup/shutdown replaced. Decode, ownership, eviction, mixer and event
  functions are unchanged. Volume reads return deterministic synthetic bytes.
- 600 cache-miss requests, a warm cache hit, all 40 effect voices pinned above
  the 2 MiB soft target, then release/reclamation through `sound_update`.
- Allocation failure, first/second compressed-read failure and successful retry,
  100 sound teardown cycles, and 100 warmed looping-voice mixer pulls without
  new allocations or volume reads.
- A control reconstructed by reversing only the Wii PCM allocation-size hunk.
  Both implementations coexist in independent translation units. Direct
  `memcmp` checks every decoded PCM sample and compares count, rate and loop
  metadata. The test covers 129,087 ranges: every length 0 through 2,048 for
  starts 0 through 31 and 31 starts near `UINT32_MAX`, including endpoints that
  cross 32 bits. Synthetic predictor coefficients are nonzero.
- The corrected allocation equals the exact number of payload nibbles for
  every compared case, including partial header/payload frames and empty output.

The allocator wrapper records requested live allocations and checks ownership
directly. It is not a process RSS, fragmentation, SDL, kernel, file-descriptor or
unlimited-session test. ASan/UBSan supplement these checks. LeakSanitizer defaults
off for restricted hosts, and is not the basis for the ownership assertions.
These host checks do not establish Xbox frame rate or audible quality.

The target stays a **soft idle-cache target**, not a hard cap on active PCM.
Active and paused voices are never evicted. No quality, sample rate, resampling,
voice concurrency, queue target or DMA synchronization changes are tested or
introduced by this correction.

The runner retains generated current/control code, exact source/input hashes,
explicit host-boundary substitutions, commands, logs and test binaries. It
checks the input hashes again after execution. Unexpected sizing-hunk or source
layout changes require review rather than silently updating the control.
