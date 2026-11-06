# Movie terminal-padding regression

From the checkout root, choose a **new** output directory on the workspace filesystem:

```sh
python3 tests/movie_terminal_padding/run.py --output ../movie-terminal-test --sanitize
```

This runs 675 synthetic cases normally and with ASan/UBSan. To also compare complete, privately owned source films, add an existing directory containing `midway.pss`, `digital.pss` and `intro.pss`:

```sh
python3 tests/movie_terminal_padding/run.py --output ../movie-terminal-test-assets --sanitize --movies /path/to/existing/movies
```

That runs 679 cases in each configuration, including reopening Midway after the other films. No asset is included or copied. Requirements: Python 3 and a host C compiler (`CC`, default `cc`) supporting GNU-style section collection. No Xbox toolchain, SDL, GPU, sound device or emulator is used.

## What is tested

- All supported interleave widths (16 through 1024 bytes), mono/stereo, either half of a 32-byte interleave, channel-independent ending, and subsequent blocks.
- Nonzero valid end-frame samples and predictor history; output becomes zero only after the valid terminal frame.
- Identical sample counts, compressed consumption, PCM block positions, resampler phase and EOF/failure state. Rates: 24,000, 44,100, 48,000 and 96,000 Hz. This preserves existing resampler behavior, not a general resampler-correctness claim.
- No-end controls, all 255 nonterminal flag values, explicit looping/unknown/short headers and initialized headerless fallback.
- Partial interleave EOF, byte-fragment accumulation, frame/channel/block splits through actual PES demux, buffer compaction and fresh Movie instances.
- Optional full-file PCM comparisons: Midway 628,880, Digital 240,296 and Intro 7,079,128 frames. Every earlier sample is identical; only the final 28 stereo frames change from +28,672 to zero. Total counts remain unchanged.

## Current-source provenance and boundaries

Each invocation reads the current `src/movie.c`. The control reverses only the three terminal-fix hunks and retains every unrelated current source line. Missing or ambiguous patterns fail; review changes before updating extraction patterns.

Normal tests compile the complete current production file directly and the reconstructed control, with public entry points renamed into two translation units. `vol_find` is replaced by a one-file host boundary. Complete movie open/close, header parsing, demux, ADPCM and resampling functions are real source. Audio-only tests discard unused video elementary-stream bytes after pulls; they do not decode or validate video.

Sanitizer tests compile freshly extracted, complete audio/demux/close function bodies and exact Movie/buffer definitions. Extraction spans and hashes are recorded. The Xbox-sized 64 KiB compaction threshold is used. The sanitizer opener creates a zeroed Movie file context instead of the excluded video initialization; full-source normal tests separately cover real movie_open/close. Leak detection defaults off, so no leak-check claim is made.

Source/header/test hashes are checked again after execution. Retained outputs include the exact reconstructed control, extracted audio sources, command/environment records, final binaries, logs and source manifest. Generated binaries are not checked into the repository.

## Storage and failure handling

`--output` must not exist. Nothing previously present is removed, overwritten or moved. The runner retains its new evidence directory on success and failure. Compiler scratch uses its `compiler-tmp` child, never the system default temporary directory; the compiler handles only its own transient scratch normally. Existing assets are opened read-only.

The integration run with both modes retained about 0.5 MiB including the repository tests. Compiler scratch peak was not measured; budget at least 2 MiB of free workspace for a future run. Use `--sanitize` only when the host supports ASan/UBSan. Failures are retained and returned; they are not converted into passes.

## Verified integration

On 2026-11-06, fresh normal and sanitizer runs each passed 679 cases against production source SHA256 `565f3600479041a64e0a5604646144658159290203fbf01b14b4cb051a9abfd3`, exactly matching the frozen plain target's source. Reconstructed control SHA256: `dfbaa524f2aa91b1bcd13b56b90d020c39492250f60722a48d7da2f4641fbccb`.

Runtime scope and remaining limitations are recorded in `docs/XBOX_MOVIE_TERMINAL_PADDING.md`.
