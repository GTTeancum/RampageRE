# Renderer output-refill regression

From the repository root:

```sh
./tests/render_output_refill/run.sh
./tests/render_output_refill/run.sh --sanitize
```

Requires Python 3, a POSIX shell, and a host C compiler (`CC`, default `cc`) with GNU-style function/data section collection. No game assets, SDL installation, audio device, GL headers/context, nxdk toolchain or emulator is used. The script resolves its checkout relative to itself. It creates a new temporary directory under `TMPDIR` (default `/tmp`); `KEEP_TEST_OUTPUT=1` retains that run's generated sources, extraction manifest, binaries and logs. Otherwise only that newly created directory is removed on exit.

## Validation status

**Both repository tests passed normally and under ASan/UBSan on 2026-11-05.** Leak detection was disabled, so this is not a leak check. Retained manifests and generated-file hashes were verified against the current reviewed production sources. The queue compile emits one expected unused-function warning for `xbox_audio_unmute_emulated_codec`, because this fixture replaces device startup.

The first sanitized attempt passed the helper but hit a deliberate 2 MiB per-file limit while GCC wrote temporary assembly for the queue test. This was a compiler-output/storage guard, not a failed assertion. The successful sanitized rerun used `CC=/tmp/rampage-cc-pipe.sh`, whose entire command is `exec cc -pipe "$@"`, with the same guard. No test semantics or production sources changed, and all attempts were retained.

## What is exercised

- Exact `rnd_create`, `rnd_set_output_pump` and `pump_output` with the renderer's actual private types: null/disabled callbacks, no clock query when disabled, initial time, the 16ms threshold, dense calls, a long gap, backward clock, unbind/rebind, independent renderers and unchanged unrelated fields.
- Exact helper registered to current `sound_pump`: bounded refill at checkpoints through 100 modeled 240ms frames split into 80/110/50ms consumption phases, and no duplicate PCM when the queue is full.
- Preserved event clock/fraction, instances, tracks, listener position, RNG, handle counter and manual-mix state while output is serviced.
- Existing native/emulated game and movie queue targets, unavailable sound state and closed output device.

The helper is an opportunistic checkpoint, not a timer or maximum 16ms latency guarantee. The queue test advances its synthetic eligibility clock separately from modeled queue consumption. It does not model GPU blocking, SDL threads, real hardware, gameplay rendering, movie demux, device creation or renderer teardown. Movie queue tests use a zero-PCM pull stub; they do not establish movie reentrancy or audio quality. Broader lifecycle and scene-bound tests remain separate.

## Exact-source extraction and boundaries

`extract_sources.py` reads the current checkout on each run. Renderer types and complete allocation/setter/helper definitions are extracted without changing their bodies. The complete current `sound.c` is retained except for these explicit host-boundary substitutions:

1. Platform string declarations map to host equivalents.
2. SDL declarations map to the small in-memory queue boundary used by the fixture.
3. SDL device startup/shutdown bodies are replaced and never called by the tests.

`out_feed`, `out_open`, `sound_pump`, mixing, queue targets, readiness checks and event-state code are not reimplemented. Unused sound entry points are discarded at link time with section collection rather than mocked. A failed volume-read stub satisfies the mixer boundary; the fixture does not load assets. Generated files are never checked into the repository.

The manifest records whole-source hashes, exact extracted span hashes and line ranges, every substituted span/replacement, and generated-file hashes. Missing or ambiguous source patterns fail explicitly. Review source changes before changing extraction patterns. The suite has no dependency on historical `/tmp` artifacts.

## Validation provenance

Equivalent private execution evidence is retained outside the repository in `/tmp/rampage-render-refill-review`: `test_renderer.c`, `test_sound.py`, `test_sound.c`, `sound-tests.log`, `README.md`, and their corresponding existing normal/sanitized binaries. No earlier evidence was deleted or overwritten by promotion. Exact current helper bodies were compared to the previously tested helper and matched.

Repository execution evidence:

- Normal log: `/tmp/rampage-render-refill-repository-normal.log`; retained outputs: `/tmp/rampage-render-refill.8eP3da`.
- Successful sanitized log: `/tmp/rampage-render-refill-repository-sanitize-pipe.log`; retained outputs: `/tmp/rampage-render-refill.kyrwBf`.
- Initial guarded compile failure: `/tmp/rampage-render-refill-repository-sanitize.log`; partial outputs: `/tmp/rampage-render-refill.hvelsw`.

All three extraction manifests are identical. Source hashes used for promotion and both successful repository runs:

- `src/render_gl.c`: `c7716862e1370f3f789cd16e12df74e2583b40fcba9df98e87f6b67a4a1df837`
- `src/render_gl.h`: `75a97769ed3514b85e96e70142736ee02eeef3f23886ff283cfcac172c4f4744`
- `src/sound.c`: `a3aecf517d7da91163ae9ece16631ccc00e9da91e777d9c39aac258cd0544566`

Future runs test whatever is currently in the checkout and emit fresh hashes; these historical values are provenance, not a hardcoded version gate.

## Scratch space and sanitizers

Generation currently writes about 72 KiB of text. Successful retained outputs totaled 231,860 bytes normally and 868,596 bytes with sanitizers. Compiler scratch peaks were not measured; ordinary sanitizer compilation can write temporary assembly larger than 2 MiB. For comfortable headroom, budget **16 MiB normal / 32 MiB sanitized**, or use a compiler adapter invoking `cc -pipe` on a constrained filesystem. These are conservative recommendations, not enforced minimums. The wrapper itself does not enforce a storage limit.

`--sanitize` adds AddressSanitizer and UndefinedBehaviorSanitizer. Leak detection defaults off because LeakSanitizer cannot run under ptrace-based environments. On a compatible host, set `ASAN_OPTIONS=detect_leaks=1` to enable it. A run with leak detection disabled is not a leak check. The script preserves each test's failing exit status and prints its captured output.
