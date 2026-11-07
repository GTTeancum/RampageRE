# Move windows and low-memory startup ownership

Asset-free routine runs need Python 3 and a C11 compiler:

```
sh tests/moves_lifetimes/run.sh
sh tests/moves_lifetimes/run.sh --sanitize
```

Set `TMPDIR` to writable scratch storage, especially when `/tmp` is full. Every
run retains its generated sources, source/header SHA-256 manifest, executable
and result log. Nothing is deleted. `CC` selects the compiler.

The default reads the current repository's `src`. An optional `SOURCE_DIR`
override can point at reviewed, not-yet-applied city.c/main.c/moves.c/moves.h; missing
other source/header files fall back to the repository. It never writes source
files or installs a candidate. The int-return moves_start API is required.

## Coverage

- 3,200 synthetic runner comparisons: state/time bits, chosen/from/via/floors,
  live input/switch windows, derived/mirrored input, input ageing, callback
  arguments/order/results, and timed-event ranges. Also force, successful and
  absent events, clip end/self-loop, zero length, negative/zero dt, initial
  state fallback and graphs with zero transitions.
- Initial allocation failure, safe stop and retry; partial failures under the
  legacy two-allocation strategy; 1,000 warmed entries with a pending allocator
  failure that is never reached; graph replacement, invalid graph rejection,
  independent runner ownership and repeated stop.
- 16 complete main start_moves normal/replacement comparisons, and three
  nonfatal no-graph paths. Startup failure is distinguished from absence.
- 20 initial/next-block allocation-failure cases across every failure position
  with one through four players. Prior AI graph/view and face references are
  cleared, failed actors execute no post-init, old next-block state is not
  restored after failure, and a subsequent normal load succeeds.
- Extra-actor failure after model creation: the scene still owns that model,
  the reveal slot is not published, subsequent game construction is skipped,
  and exit/retry are safe.
- Seven rebuild/city-switch same-frame guards and six normal control-flow
  comparisons; open_city rejects an explicit failed partial load, and direct
  startup keeps the missing-city distinction.
- Actual extracted world-exit ordering: results are read before world release,
  renderer keys are dropped before bundles, runner/graph/AI ownership is freed
  before scene models, and managers precede the city.
- Null/partial/full low-memory non-shell exits followed by ordinary final
  teardown. Actual renderer cache drop/reset/free code deletes world, HUD,
  region textures and buffers once, retains its final scratch/video ownership
  until final free, and preserves the Xbox glFinish calls. Shared Vol/profile/
  GUI-bundle sentinels are untouched. Explicit ownership ledgers return to zero.

## Test boundaries

The extractor reads complete current runner functions, application types,
start_moves, extra_add, world_release and popup_free. Build, shell_enter,
open_city, exit dispatch, ordinary teardown and the frame's rebuild block are
tested as exact source spans wrapped in small fixture functions. The spans
must still match their anchors; changed source structure fails extraction
rather than silently testing an old copy.

Graph/model/property/audio and world constructor/destructor dependencies are
deterministic ownership-enforcing stubs. GUI subresource destruction is also a
stub. Renderer drop/reset/free logic is real source, with GL and allocator
boundaries instrumented. Reaching the code after a failed rebuild is treated
as a simulation/render fallthrough; the rest of the real frame is not run.
These tests are not a full world load, real script execution, GPU test, full
process leak scan, or proof of every application shutdown branch. Shell
reopening and unrelated reader allocation failures are outside scope.

The comparison oracle is deliberately limited:

- `legacy_windows.c` contains only the pre-reservation enter/start/stop
  ownership strategy. The remaining transition/input/event logic is shared
  current source, not an independent reimplementation.
- The normal main reference restores only the old initialization placement
  and old window ownership within current start_moves. It tests allocation
  reordering, not arbitrary historical main behavior.
- The unguarded frame specimen removes only the new failure guards from the
  current block, showing why fallthrough prevention is needed.

ASan/UBSan mode disables LeakSanitizer; explicit fixture allocation accounting
is not a claim that every process allocation is tracked. Full main.c/moves.c
also receive host low-memory syntax checks. No target toolchain, network,
emulator or game assets are needed. No result predicts Xbox frame rate.

Optional historical actual-asset differential evidence is documented in
`ACTUAL_ASSET_EVIDENCE.md` and is separate from routine runs.

## City status compatibility

This suite also uses `tests/city_status_extract.py`. It extracts the complete
current private City declaration, MAX_BOOKS, and actual `city_load_failed` /
`scene_failed` definitions into `city-status.inc`; no status result is stubbed.
`city-status-manifest.json` records exact input, extractor, fragment and output
hashes plus source lines. Its allocation-free assertions check null handling,
healthy owners, City-to-Scene sticky propagation, retention after owner reset and
independence of a second City. These are scoped status/ownership fixtures, not a
Book parser, complete City loader or whole-game performance measurement.

SOURCE_DIR must now include city.c as well as main.c/moves.c/moves.h. The test extracts world_load_failed and uses a correctly sized City owner. The checked weapons-reader dependency double invokes the actual failure callback, while preserving the existing counted healthy initialization. New cleanup/retry cases execute the exact world_release function and shell_enter prefix through the cleanup boundary for a failed partial Scene (have_scene=0), runtime Scene failure and City-only failure. After cleanup they require a zero Scene, zero scoped world owners and admission by the exact initial shell Start guard. Scene destruction remains the explicitly counted existing double, not actual scene_free. Normal mode and ASan/UBSan run these cases in both ordinary and LOW_MEMORY layouts. Only the narrow recovery cases run ordinary layout; the pre-existing full suite remains LOW_MEMORY. Empty repeated teardown does not require renderer detachment when no model or instance owners exist, while populated teardown still does.


### Failed result publication and healthy retention

The narrowed runtime fixture additionally runs the exact extracted main-loop
exit block for failed partial Scene, failed built Scene and City-only failure.
The City-only case goes directly to exit with Scene.construction_failed still
zero, so it independently selects the City operand. All Game outcome getters
assert if called in these cases. Before cleanup, the shell dependency requires
quit=1, choose_monster=0, city_destroyed=0, show_victory=0 and king_of_world=-1,
and byte-identical last_game choices. Healthy close_world still requires five
result getters; only failed-world cases now require zero getters.

An ordinary-layout positive control requires healthy shell entry to retain the
City, Scene, manager/Game owners, move graphs and renderer keys unchanged. Its
cleanup explicitly calls the extracted world_release followed by the ordinary
final-tail fragment. This is test disposal, not proof of the whole process-exit
path: the final-tail fragment omits manager/Game/DirGraph cleanup. The ownership
doubles remain strict. These additions do not roll back or assert PRNG state.


### Checked City admission and direct startup

The checked-open probe extracts the entire current open_city, actual fatal stop
helper, direct-start failure branch and final process-status tail. Counted City,
roster, renderer and build dependencies model admission and staging only. Ten
cases in each layout cover missing NULL/out0, fatal NULL/out1, and property-roster,
block-list and game-block failures, each with and without an existing world.
They require old City/Scene/metadata retention, closure of only the unpublished
new City, no build/world_release/scene_free, and no fatal renderer reattachment.
LOW_MEMORY restores the previous scene only for genuine missing media. Initial
fatal failures retain process status1 after cleanup, override autoplay status3,
and do not show the missing-media message. Healthy startup/output and autoplay
status remain separately checked. City tokens are counted synthetic dependencies,
not actual City parser/destructor execution; no new heap leak claim is implied.
