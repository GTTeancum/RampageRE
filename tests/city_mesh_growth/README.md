# Scene mesh-vector growth containment

Run `tests/city_mesh_growth/run.sh` or append `--sanitize`. The runner tests both
ordinary and RAMPAGE_LOW_MEMORY layouts, retaining all files in a fresh TMPDIR
subdirectory and printing that path immediately. CC selects a C99 host compiler;
Python 3 is required. REPO_ROOT and CITY_SOURCE optionally select a repository
and a private candidate source. Defaults test the current checkout, never a frozen
implementation. No assets, emulator, download, installation or deletion occurs.

The extractor finds unique function definitions and balances braces after masking
comments and literals. It extracts actual scene_fail, scene_vector_grow, scene_add_mesh and
mesh_free, preserves diagnostic source locations, and records SHA-256 hashes of
source, headers and harness files. Both full city translation-unit layouts also
receive syntax-only checks. Compiler warnings are errors; assertions stay enabled.

The harness verifies:
- 513 inserts with capacities 256, 512, 1024 unchanged in both modes. Successful
  realloc always moves (old/new allocations coexist). Every prior nested MeshPart
  and position pointer, value, Bundle pointer and returned index survives growth.
- Failed initial and populated 256-to-512 allocation, signed -2 return and sticky
  construction_failed latch, otherwise unchanged Scene, populated vector bytes
  and live requested bytes. Repeated latched calls do not allocate. The
  incoming temporary Mesh is explicitly separate and test cleanup frees it.
- Signed-index capacity overflow through scene_add_mesh, helper signed addition
  overflow, invalid zero/UINT32_MAX steps, and SIZE_MAX byte multiplication overflow
  via synthetic helper element size. Overflow makes no allocator call.
- Actual mesh_free releases every published synthetic mesh exactly once, zeroes
  each, and the vector owner is explicitly released. This is not scene_free testing.

Expected each run: 2 allocator failures, 3 latched append failures, 1,546 acquired and
released allocations, and zero requested live bytes on completion. The ledger
rejects unknown/double frees and checks 32-byte tail canaries. Peak requested bytes
are 84,060 on the tested 64-bit host; metadata and untracked snapshot bytes are
excluded. These are synthetic test costs, not Xbox memory estimates. All fixture
loops and allocations are bounded.

ASan/UBSan are optional; LSan defaults off for restricted runtimes. Explicit owner
accounting runs regardless. ASAN_OPTIONS/UBSAN_OPTIONS can override defaults.
Sanitizers need virtual address space and should not run under console-sized caps.

The helper returns NULL on failure; scene_add_mesh latches construction_failed
and returns -2. This tests owner-preserving failure propagation, not all callers
or complete world recovery. No production abort or test longjmp is used. Actual
scene_free, full-level loading, malformed assets, runtime model rollback,
fragmentation and Xbox execution/performance remain outside this fixture.

## City status compatibility

This suite also uses `tests/city_status_extract.py`. It extracts the complete
current private City declaration, MAX_BOOKS, and actual `city_load_failed` /
`scene_failed` definitions into `city-status.inc`; no status result is stubbed.
`city-status-manifest.json` records exact input, extractor, fragment and output
hashes plus source lines. Its allocation-free assertions check null handling,
healthy owners, City-to-Scene sticky propagation, retention after owner reset and
independence of a second City. These are scoped status/ownership fixtures, not a
Book parser, complete City loader or whole-game performance measurement.

The real append also rejects a poisoned City owner before any allocation or publication. Existing growth/fault totals and peak-byte expectations are unchanged.
