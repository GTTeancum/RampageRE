# Face/pose failures

Run `sh tests/face_pose_failures/run.sh` or add `--sanitize`. `SOURCE_DIR` selects a source directory; default is the installed checkout's src. Missing source or extraction boundaries fail, with no frozen fallback. `FACE_TEST_OUTPUT_ROOT` or `TMPDIR` chooses retained output; runs never delete outputs. Python 3 and a C compiler are required.

The owner harness extracts actual city face/body replacement, face blending and mirror functions. It checks every synthetic allocation failure, old-owner preservation, missing clips, sticky reentry, all active-hold combinations and zero scoped owners at teardown. Healthy cycles also cover zero-node and wide-mask cases. A Clip reader/free double implements the accepted reader contract; this does not test the parser. The extracted teardown block is actual instance clip/mask/held cleanup, not full scene_free.

The main harness extracts the actual type prefix and ten functions for face start/play/step/events, body start, side changes, effect start and fatal stop. It uses real look.c queue handling, and counted root/effect/event dependency doubles. It checks first-face failures, nested event failure stopping subsequent dispatch, delayed belch, restart, root use stopping after body/mirror failure, missing clips and sticky reentry. Eleven outer caller boundaries have source-adjacency checks, not whole-loop executable coverage. Complete main/city translation units and actual-asset teardown are tested separately by private validation.

Both harnesses run in ordinary and LOW_MEMORY layouts. Sanitized mode uses ASan/UBSan with detect_leaks=0 plus explicit owner ledgers; no LeakSanitizer claim. Receipts, generated extracts, binaries, stdout/stderr and source pins remain in each fresh output directory. Private baseline comparisons additionally establish byte-identical healthy output and allocation-count reduction; this installed suite does not contain an old source snapshot.

## City status compatibility

This suite also uses `tests/city_status_extract.py`. It extracts the complete
current private City declaration, MAX_BOOKS, and actual `city_load_failed` /
`scene_failed` definitions into `city-status.inc`; no status result is stubbed.
`city-status-manifest.json` records exact input, extractor, fragment and output
hashes plus source lines. Its allocation-free assertions check null handling,
healthy owners, City-to-Scene sticky propagation, retention after owner reset and
independence of a second City. These are scoped status/ownership fixtures, not a
Book parser, complete City loader or whole-game performance measurement.

Owner fixtures now bind a real scoped City. Extra direct face/body and main face-start cases assert City failure stops before allocation, event dispatch or root use. Outer static checks require the exact reviewed immediate receiver spellings after redundant guard removal: mo->v for the two mon_step callbacks and (Viewer *)v for the seven named manager continuations. mon_step must retain its explicit Viewer *v = mo->v alias. The script continuation requires both retained stop statements. Only whitespace may intervene, never another operation.
