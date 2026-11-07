# Renderer cache and texture decoder allocation failures

From the repository root:

```
sh tests/renderer_cache_failures/run.sh
sh tests/renderer_cache_failures/run.sh --sanitize
```

Python 3 and a host C11 compiler are required (`CC`, default `cc`). No GL driver,
SDL, assets, Xbox toolchain or emulator is required. Each run retains all of its
own generated files under `TMPDIR` (default `/tmp`); nothing is deleted.

The extractor reads complete current render_gl.c cache functions and all of
tex.c. Indexed bundle lookup functions and public headers also come from current
source. A missing/ambiguous definition fails extraction; manifests pin source,
headers and exact extracted spans. The optional SOURCE_DIR override supplies
reviewed candidate files, falling back to current repository files for anything
not overridden. REPO_ROOT is useful when running this package before installation.
Neither option changes production files.

## Checks

Normal O2 and ASan/UBSan O1 each run host and Xbox + low-memory macro layouts:

- 36 synthetic pixel cases cover PS2 32-bit, 16-bit and indexed textures;
  external, alpha and intensity palettes; ordinary and swizzled indexed data;
  Wii I4, I8, IA8, RGB5A3, RGBA8 and CMPR.
- 440 healthy renderer cases include all those images, HUD/world uploads,
  mipmaps, anisotropy, missing/replacement texture-pack boundary behavior,
  crop/no-crop regions, cache hits, eight buffer attribute combinations,
  buffer updates, mesh replacement and scene transitions.
- 5,875 host and 5,976 Xbox-macro counted fault/retry cases include 101 repeated
  texture/region/buffer lifecycle groups. Every decoder allocation is injected,
  including both indexed-image allocations. Outputs remain unpublished on
  failure; valid decode retries return identical bytes/hashes.
- Bundle table allocation cannot publish an unusable cache or exhaust its 16
  slots. HUD cache allocation retries without losing an existing world texture.
  Missing/unsupported/truncated texture data and no-crop regions stay
  negative-cached without repeated allocation work.
- Region metadata, decode, crop and handle failure preserve prior entries and
  allow retry. Texture-pack absence/allocation failure retains original-image
  fallback: that separate API intentionally does not distinguish the two.
- Zero/no-write texture-name generation and every incomplete buffer-name pair
  use fallback, never publish unusable handles, and retire any returned partial
  pair. Partial-name generation is a defensive synthetic boundary scenario,
  not a demonstrated failure mode of gl4es, nxdk or any native driver.
- Mesh table/part table/staging allocation failures, table replacement and
  forced-moving realloc keep or retire the correct owner. Failed stage growth
  keeps old buffers/revision until retry. Independent renderers remain independent.
- Optional mip scratch failure keeps a usable single-level linear texture.
  Deliberate staging capacity survives scene changes and is released at final
  renderer teardown. Crop metadata storage retained after a failed attempt is
  still owned by its bundle cache and freed at texture drop.

GL boundaries validate ownership and hash calls, arguments, uploaded pixels and
buffer contents. For a healthy run, setting TRACE_FILE when directly invoking a
retained binary with --healthy also records this stream for bytewise comparison.
The --probe and --indexed-fault entry points are baseline diagnostic controls;
ordinary regression mode expects repaired source, not the historical source.

## Optional actual-texture corpus

```
TEXTURE_CORPUS_DIR=/path/to/corpus sh tests/renderer_cache_failures/run.sh --sanitize
```

The optional host-ABI pass loads each .bdp/.bdg mini-bundle with complete current
bundle.c. Entry 0 must be the texture; keep its original name table and any
referenced palette entry. Corpus files are not installed or copied by this suite.
For each image it checks the decoded output hash, injects every decoder allocation
and both renderer caller paths, and retries in the same process. Name index 0
is tested through texture_at's zero_ok path; region_texture deliberately rejects
zero, so that case is not claimed as region coverage. The independently preserved
private corpus contains 16 actual PS2/Wii textures across all nine formats.

## Limits

These are intercepted CPU owners and fake GL names, not GPU allocations or a
whole-process leak scan. LeakSanitizer is disabled for the restricted runtime;
ASan/UBSan plus explicit ledgers are used. Full native GL upload failure reporting
(glTexImage2D/glBufferData), driver internals, GPU synchronization, framebuffer
ownership, movie uploads, exhaustive malformed/swizzled input, real rendered
pixels, target ABI, FPS and physical-RAM usage are outside this suite. Xbox VBO
checks opt into its normally-disabled buffer path. Synthetic texture-pack checks
also exercise a boundary that the shipping Xbox texpack stub normally disables.
No claim of universal leak freedom or performance improvement follows.
