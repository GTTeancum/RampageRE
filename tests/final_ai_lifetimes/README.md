# Final AI graph lifetime regression

Run against the selected **current candidate/integrated source** directory, not a cached object or executable:

```sh
TMPDIR=/absolute/private/evidence/tmp python tests/final_ai_lifetimes/run.py src normal
TMPDIR=/absolute/private/evidence/tmp python tests/final_ai_lifetimes/run.py src sanitize
```

The script retains generated excerpts, compiled binaries/objects, commands, logs, hashes and results in a unique child of TMPDIR. It never edits source. The optional FINAL_AI_BASELINE_MAIN environment variable requires the generated defective reference main.c to equal that supplied file byte for byte.

## What is executed

- Complete current director.c and moves.c, separately compiled with heap accounting. The real DirGraph constructor/destructor and real MoveRunner start/stop and MoveGraph destructor are executed. Synthetic MoveGraph owners are explicitly allocated; this is not a successful moves_read parser test.
- The selected main.c final graph loop, complete final shutdown tail, capture-open failure block, completed-capture block, world_release monster loop, and absent MoveGraph replacement prefix are extracted from source. Unexpected shapes fail rather than silently selecting old fragments.
- A defective reference is made by reversing exactly this repair in current main.c. The initial private proof additionally requires it to equal the accepted baseline. Reference retained ownership is measured before test-only rescue frees it.
- All 256 combinations of independent have_graph/have_ag flags across four slots, at 1, 2, 7, 31 and 255 states. Each successful DirGraph has seven actual allocations. The maximum four-graph payload tested is 284,580 bytes, not an observed process leak or target RAM claim.
- Actual moves_read absence after a live replacement produces have_graph=0 and have_ag=1. Final cleanup must release that independent graph.
- All 127 nonempty allocation-failure masks over the seven DirGraph positions; later positions can be unreachable after an earlier grouped failure. The constructor must leave an empty graph. All 127 partial owner subsets also test the real destructor twice directly, without asserting that a partial graph with a false success flag is a valid final-Viewer state.
- Three absent inputs: no entry, wrong entry type, null raw data. All 256 flag combinations through world_release's monster loop and then final cleanup. Three repeated final *graph-loop* cleanups per candidate flag/size case.
- 64 shutdown cases: initial status 0/1 crossed with normal and three capture failures (neither file, video only, audio only), with/without a scene, with/without autoplay, and autoplay result 0/3. The candidate preserves capture failure result 1 and closes each successfully opened partial file once. Non-graph subsystem spies assert popup -> renderer -> graph owners -> scene -> city -> sound end -> sound free -> window -> autoplay order.
- 16 completed-capture cases execute the original capture completion closes followed by final shutdown, verifying neither FILE is closed twice and autoplay result is preserved. These tests do not add final FILE cleanup: an interrupted capture remains separately open in the application until process exit.
- Real graph script callbacks and real AI steps run before cleanup. Destructor ordering and no further callback invocation are asserted; the accounting rejects foreign or double frees.

There are 1,360 counted flag/shutdown cases per variant, plus 256 world-release cases, 127 construction-failure cases, 127 direct partial-destructor cases and the absence/replacement checks. The candidate records 4,096 repeated graph-cleanup checks. Repetition applies only to graph loops/destructors, not the entire Viewer shutdown, which is not a restart/reentrancy API.

## Limits

Renderer, Scene, City, audio output, popup, window and autoplay shutdown are order spies, not dynamic real subsystem teardowns. DirGraph/MoveGraph fixtures use the real selected graph units; other allocations, GPU memory, allocator retention and process resource ownership are not part of the zero-owner assertion. ASan and UBSan run with halt-on-error; LeakSanitizer is disabled. This is not a whole-game leak, speed/FPS, malformed-input, target-build, gameplay or whole-Viewer-cleanup certification. No game assets are needed or copied.

## Composition with City/runtime v4

Runtime v4 already declares and preserves initial startup failure status. The defective reference reverses only capture-open cleanup, the shutdown label and final independent AI-graph release. It preserves the runtime status code. The existing 32 shutdown scenarios also run with initial status 1, making 64. This proves final-tail status precedence with shutdown-order spies, not actual City startup execution. Checked-open/main tests cover that separate branch. Every prior graph, failure-mask, output and ownership assertion remains.
