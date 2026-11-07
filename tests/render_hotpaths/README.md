# Renderer and cloth hot-path regressions

From the repository root:

```sh
./tests/render_hotpaths/run.sh
./tests/render_hotpaths/run.sh --sanitize
```

Requires Python 3 and a host C compiler with C11 support (`CC`, default `cc`). No
assets, graphics context, emulator, SDL or Xbox toolchain are required. The
extractors read complete current production definitions, record their source
line ranges and hashes, and fail on missing/ambiguous definitions. Nothing is
linked against a frozen historical source. Public types come from current
project headers. The normal build uses O2; the sanitized build uses O1 and
AddressSanitizer/UndefinedBehaviorSanitizer.

Set `TMPDIR` to a writable workspace directory and `KEEP_TEST_OUTPUT=1` to retain
generated source, manifests, binaries and logs. Compilation uses `-pipe` to
avoid large temporary assembly files. Typical retained output is below 2 MiB
per run. Otherwise the runner deletes only its newly created temporary folder.

## Coverage

Both host and Xbox + low-memory macro configurations execute:

- 4,608 material/attribute/time combinations: non-cloud draws perform zero cloud
  remainder calls; cloud draws perform two and preserve exact scrolling values,
  including negative times and the region texture reset path.
- 24 shadow pass/list combinations: a missing pass makes no GL calls; existing
  passes keep one state push/pop and their geometry submission.
- Water UV scratch: zero-initialized ownership, empty and negative count,
  warm reuse, scene-switch retention, independent renderers, initial and growth
  allocation failure, preservation of the old allocation/capacity, retry, and
  zero tracked scratch after renderer teardown. A size-overflow branch is tested
  when the host `size_t`/`int` ABI permits it. A 64-bit host cannot exercise that
  32-bit-only input; real `water_new` accepts dimensions 1..1024, making its maximum
  count 1,050,625 and its UV size 8,405,000 bytes, representable in 32-bit `size_t`.
- 32 cloth mapping/mirroring/normal/debug cases with current CPU skinning and
  exact current local-coordinate conversion. Invalid/unmapped particle indices
  preserve rigid vertices. Debug-disabled frames emit no diagnostic.
- 44,928 RawAnim translation/quaternion cases compare bitwise against the same
  current function with only the old four-component trig expression restored.
  Zero/negative angle, two segments, negative and loop/segment boundary times
  are covered. Nonzero raw angles use one sine and one cosine instead of four
  each; zero angles use neither. This does not optimize keyed monster clips.
- 600 warmed cloth calls make zero allocation/free calls. Initial/growth OOM
  leaves the rigid pose intact and retries later. Two scenes own independent
  scratch; repeated teardown returns tracked scratch to zero.

## Boundary substitutions and limits

The GL interface records arguments and calls; its enums are distinct fixture
values with GL's real boolean semantics. It is not a graphics driver or complete
GL state machine. Texture lookup/upload, buffer lookup, sphere-coordinate setup,
output pumping and water arrays are controlled boundary fixtures. Material
parsing and bundle indexed lookups use current production implementations. The
water fixture does not run the actual reflection-UV algorithm.

The animation differential oracle restores only the historical raw-angle
expression in the extracted current function. Its call counters are transparent
math wrappers; it runs with `-fno-builtin`, as nxdk does. It establishes host
bitwise output equivalence, not Xbox ABI, cycle time or FPS.

The cloth fixture supplies fixed particle arrays and stubs stepping/reset,
collision and wind. It executes current skin transforms, particle mapping,
`cloth_to_local` and its math helpers. Teardown functions for unrelated empty
scene components are stubs. Allocation wrappers count only allocations made by
these extracted paths. These checks are not complete scene parsing, dynamics,
rendered-pixel, target-ABI or GPU synchronization acceptance.

Separate private audit evidence compares full baseline/candidate active GL call
traces and 32 exact baseline/candidate cloth position/normal/diagnostic results.
A separate actual-asset census compares 360 skin-array hashes across four
city/monster combinations with real cloth stepping. These are bounded checks,
not unlimited-session leak or real-Xbox performance guarantees.

LeakSanitizer defaults off because it may not work under ptrace. The explicit
scratch allocator ledger is still checked. On a compatible host use
`ASAN_OPTIONS=detect_leaks=1`; do not describe an ASan-only run as a general leak
check.

## Ownership and memory tradeoff

Water scratch belongs to each `Renderer`, retains high-water capacity across
`rnd_scene`, and is freed by `rnd_free`. It is no longer process-static storage.
Failed growth does not lose the allocation or publish a false capacity.

Cloth scratch belongs to `Scene` and is shared by sequential `skin_item` calls.
Only one maximum-particle buffer is retained, not one per skin. `scene_free`
releases it. Skinning already uses static palettes and is not reentrant; this
change adds no new threading or reentrancy guarantee. Its two fields are appended
to `Scene`, but its size changes: rebuild every translation unit that includes
`city.h`; old object files are not layout-compatible merely because offsets of
older fields are unchanged.

The actual-asset census retains 1,416..1,992 bytes of scratch for the tested
scenes. This is a deliberate small increase in live/peak requested heap in
exchange for removing repeated allocation/free calls, not a RAM reduction.
