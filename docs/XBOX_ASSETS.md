# Private Wii-to-Xbox asset pipeline

Status: executable inventory, texture reduction and lossless mesh preparation.
Synthetic conversion tests pass. The user's full Wii Game folder was supplied on
November 6, 2026; all 129 original bundles now pass the native C readers under
ASan/UBSan and the format checks below. This does not establish conversion quality,
full Xbox gameplay coverage, or runtime memory/performance savings. No optimized
retail Wii variant has been applied. This tooling does not change the running Xbox
build.

## Recommended sequence

1. Keep the user's original Wii extraction unchanged. Inventory it alongside the
   known PS2 base, then test the unmodified Wii overlay first in an isolated Xbox
   build once the baseline runner is free. Do not interrupt another Xemu run.
2. Measure the selected scene's physical-memory minimum, load peak, frame time,
   mesh/skinning work, texture allocations and repeated-load growth. Compare the
   same city/block, monster count, camera and test duration. The inventory is an
   asset-cost reference, not a substitute for this runtime measurement.
3. If needed, create separate texture-reduced and losslessly deduplicated bundles.
   Test one change at a time. Keep the highest-quality version that meets the
   measured memory and frame-time limits with headroom for the rest of the game.
4. Only if geometry or skinning is still expensive, qualify an actual edge-collapse
   simplification/reimport step on the supplied models. No such art reduction is
   currently enabled automatically. Keep the original rig, native clips, material
   records, collision/gameplay data and cloth simulation, and validate deformed
   poses, silhouettes and material seams before accepting any simplification.

There is no reason to convert through a PS2 texture or model format. Both Wii
and PS2 readers already converge on the native `MeshPart`/RGBA representation.
Original Xbox has a shared 64 MiB memory budget; on-disc Wii CMPR size alone does
not establish the runtime cost. This renderer decodes texture mip 0 to RGBA8,
and retains the source bundle too. Compressed texture upload is a separate
renderer feature, not implemented by this converter.

## Implemented commands

Run from the repository root. Requires Python 3 and Pillow, which the existing
texture/export tools already use. Outputs are private local files. The tools do
not download or upload anything.

```sh
python3 tools/xbox_assets.py inventory \
  --ps2-vol /private/PS2/RAMPAGE.VOL \
  --wii /private/WiiExtraction \
  --out /private/reports/original-assets.json

python3 tools/xbox_assets.py optimize \
  --input /private/WiiExtraction/DATA/files/Game/Boris.cmg \
  --output /private/variants/half-textures/Game/Boris.cmg \
  --texture-scale 0.5

python3 tools/xbox_assets.py optimize \
  --input /private/WiiExtraction/DATA/files/Game/Boris.cmg \
  --output /private/variants/deduplicated/Game/Boris.cmg \
  --deduplicate
```

The `inventory` Wii argument accepts a directory with `Game`, `DATA/files/Game`,
or `files/Game`. It reads Game/Shell/Audio bundles and lists availability of all
10 extra monsters plus Dallas files. `--ps2-vol` works directly on the archive,
with no full extraction. Either source can be omitted. Unavailable platform
figures are `null`, not a measurement of zero.

The optimization command handles one `.cmg`/`.bdg` at a time. Source and destination
must differ, and existing output/report files are never overwritten. The default
report is `<output>.json`. Run it for the target monster model bundles and Dallas
world bundles selected by the inventory. Do not blindly reduce Shell, GUI, fonts,
loading-screen images or every shared bundle. The complete original overlay and
PS2 base are still required; a few optimized files alone are not a runnable game.
When staging, copy the original overlay to a separate private variant and replace
only the selected bundle files. Never write into a hard-linked original.

### Texture stage

- Reduction uses exact-aspect-ratio power-of-two dimensions and a BOX filter.
  `--texture-scale 0.5` halves width and height, so base RGBA pixels fall by 75%.
  `--max-texture-side 256` may be used as a cap instead. Scales between halving
  steps round down to the next halving step; dimensions that cannot be divided
  exactly are left unchanged and reported.
- The resized image is encoded in native Wii GX RGBA8 tiles, which the current
  C decoder already reads. It is not a PS2 conversion or a PNG runtime override.
  The Xbox build disables the PC texture-pack feature, so that is not the path.
- RGB and alpha of the resized image survive the encoder exactly. The original
  alpha mask necessarily changes when downsampled. Reports include alpha-test
  coverage before/after; hair, feathers, foliage, signs, borders, small text and
  atlas regions require image review. No opaque/transparent mode is guessed.
- Output carries one mip level. The runtime may generate its own mips according
  to settings; original on-disc lower mips are not the runtime's generated mips.
- GX RGBA8 can increase on-disc/source payload bytes relative to CMPR. The report
  includes that increase, and a candidate is rejected if source texture payload
  plus one decoded RGBA base image would not decrease. This is not a guarantee
  about actual peak RAM or GPU allocation.

### Lossless model stage

- Deduplicate only byte-identical complete vertices within the same mesh part.
  This includes position, normal, UV, color, tangents, all bone indices and all
  stored weights. It never merges material boundaries, changes winding, deletes
  triangles, collapses an edge or reduces the four-influence Wii skinning to two.
- Rewrite the GX display-list indices and compact its vertex storage. Every
  ordered triangle corner, material index and vertex attribute is verified
  byte-identical before and after. Both 8-bit and 16-bit GX indices are supported.
- Rigid mesh deduplication is skipped for bundles containing cloth records. Mesh
  payloads with shared/unknown storage are also left alone. No deduplication
  savings are assumed; many real meshes may have no identical vertices.
- Native skeletons, inverse bind matrices, animations, material records, move
  graphs, scripts, cloth, sound references and other non-mesh/non-texture payloads
  are copied and checked byte for byte, including nonzero bundle segments. Cloth detection checks all segments.
- Bundles with unexplained non-padding bytes outside documented structures are
  rejected rather than silently dropping unknown data.

## Accounting and reproducibility

Each report records source and output SHA-256 and the exact policy. The same input,
policy, source code and Pillow version produce the same output. Keep the tool
revision and Python/Pillow versions with each test run.

Decoded inventory metrics cover segment 0, which is what the current native C
reader loads. Optimization can also process other segments; their individual
changes are listed but not added to segment-0 decoded costs. Whole-file size and
source hashes always cover the complete bundle.

Inventory costs include original bundle bytes, 32-bit Xbox bundle indexes and
mesh-part structures, decoded mesh arrays, actual index-buffer capacity rules,
vertex/triangle counts, skinning influence counts, texture dimensions/formats,
base/full-mip RGBA bytes, animation payload counts and skeleton nodes.

Do not add animation payload bytes on top of bundle file bytes: those animation
bytes already reside in the bundle. Do not add every bundle's totals and call that
the level's memory usage. Dynamic buildings, instances, skinned copies, temporary
decoding buffers, driver/DMA allocations, HUD/3D duplicate texture caches,
material-atlas cuts and power-of-two texture rounding can add memory. Runtime
profiling must close that gap.

## Existing interchange tools and the simplification boundary

`tools/anim.py` supports `.cmg` input, two/four-bone Wii skinning and animated glTF
export. `tools/mesh.py` supports Wii GX primitives and interleaved attributes;
`tools/tex.py` decodes all documented GX formats. They provide inspection and
Blender input, for example:

```sh
python3 tools/anim.py /private/WiiExtraction/DATA/files/Game Boris /private/inspection/Boris
python3 tools/anim.py /private/WiiExtraction/DATA/files/Game --models Dallas.bdg /private/inspection/Dallas
```

These are **exporters**, not a qualified lossless round-trip. Their glTF material
representation does not preserve every native material flag or layered effect;
cloth is represented by an approximation for preview. The game does not load that
glTF. Therefore, exporting to Blender, applying Decimate and re-exporting does
not yet create a validated playable replacement. Do not represent it as one.

If true simplification becomes necessary, implement a geometry-only importer that
reuses native metadata rather than round-tripping the whole bundle through glTF.
For skinned geometry it must preserve bone order/bind transforms, transfer all
weights, retain material splits/UV seams and evaluate original animation samples
against deformation-error bounds. Leave cloth and collision/gameplay records
untouched. Qualification requires actual Wii inputs. The current native bundle
writer and exact-corner validators are the reusable packaging and verification
parts of that future stage.

## Validation

```sh
python3 -m unittest discover -s tests/xbox_assets -v
python3 tests/xbox_assets/verify_c_readers.py
python3 tests/xbox_assets/verify_c_readers.py --ps2-vol /private/PS2/RAMPAGE.VOL
python3 tests/xbox_assets/verify_c_readers.py --sanitize --ps2-vol /private/PS2/RAMPAGE.VOL
```

The C check compiles current `bundle.c`, `mesh.c`, `tex.c`, `tree.c` and `anim.c`.
Sixteen synthetic Wii cases cover rigid, two-bone and four-bone meshes at both
index widths, all six GX texture formats, quads/strips/fans, and multiple material
parts. The C pixel hash is compared with the Python decoder, including the
reduced RGB and alpha. It compares triangle-corner/skin attributes, bind poses and
five animation samples before/after, confirms smaller vertex/texture counts, and
verifies no-op conversion. Eighteen Python tests additionally cover unknown/shared
storage refusal, UV seams, alternate-segment cloth, and exclusive output writes. Optional retail PS2 checks read George, his animations and Las Vegas
from the user's own archive into temporary files, comparing C counts to Python.
It does not launch Xemu, access an HDD image or touch a staging build.

Sanitizer mode enables ASan and UBSan. LeakSanitizer is disabled because the cloud
executor's ptrace sandbox prevents it from running. This is not a leak-check pass.
No generated retail assets or game images belong in source control or a release.

## Current PS2 reference, November 5, 2026

Measured from the available user-owned PS2 VOL; these are bundle inventory values,
not per-frame draw counts or measured process RAM:

| Bundle | Vertices | Triangles | Decoded mesh arrays | Base RGBA textures | Source bundle |
| --- | ---: | ---: | ---: | ---: | ---: |
| George.cmp | 3,889 | 2,628 | 246,396 B | 262,144 B | 464,096 B |
| Lizzie.cmp | 4,189 | 2,992 | 268,096 B | 262,144 B | 461,472 B |
| LasVegas.bdp | 57,080 | 33,305 | 2,512,904 B | 8,419,328 B | 5,210,848 B |
| Chicago.bdp | 72,008 | 42,206 | 3,204,012 B | 11,108,352 B | 7,051,840 B |
| NewYork.bdp | 90,936 | 53,599 | 4,012,196 B | 10,641,664 B | 7,493,536 B |

George's separate animation bundle is 1,460,000 B and has 146 animation entries.
Across the 30 PS2 model bundles, triangle counts range from 1,988 (Natalie) to
3,342 (Rhett), decoded mesh-array storage from 172,588 B to 295,524 B, and each has
262,144 B of base RGBA texture storage. All 117 PS2 bundles were inventoried.
Original Wii inventory is now available; optimization savings are still unmeasured.

## Original Wii input qualification, November 6, 2026

The supplied Game-only archive has 348 files (303,111,158 B expanded), including
129 bundles (209,861,536 B). All 40 model/animation pairs are present, including
the ten exclusives. All ten exact `Hud_<Name>_Head` textures occur in WorldGui.bdg
at 40 x 62 pixels, GX RGB5A3. Game/fx.bdg and the Wii shared property books are
present. Shell and Audio were not included, so this is not a complete Wii menu,
roster-artwork or voice-bank input set.

Unchanged retail input checks:
- All 129 bundles load through the current C bundle, texture, mesh, tree and
  animation readers under ASan/UBSan (LSan remains unavailable in the sandbox).
- Python and C vertex, triangle, RGBA-byte and animation-entry counts agree for
  every bundle. All 6,656 meshes and 7,181 animation entries pass the supplied
  format tools; all 40 move graphs parse. No source bytes changed.
- Explicit template-book/cross-book dependencies and monster property-book
  references resolve against the uploaded Game files plus the existing PS2 base.
  This checks 1,854 references to 35 distinct book/group targets. It is not a
  whole-game asset-completeness proof.
- The earlier partial Boris/Dallas package is byte-identical to the same files in
  the full Game upload.

Boris has 2,985 vertices, 5,233 triangles and 227,564 B of decoded mesh arrays.
His color and bump textures are both 512 x 512, totaling 2,097,152 B of potential
base RGBA images. The renderer may not upload both, so that total is not observed
GPU residency. Dallas.bdg has 77,967 vertices, 64,508 triangles, 3,837,104 B of mesh
arrays and 21,338,112 B of potential base RGBA images across the whole bundle.
The ten exclusive monster bundles range from 4,467 to 5,233 triangles; each has
2 MiB of potential color-plus-bump RGBA images.

The unchanged Xbox tests are scheduled through the sole emulator runner. Native
reader checks and name presence alone do not prove that every monster animates,
attacks, displays a HUD portrait, respawns or completes a level correctly.
