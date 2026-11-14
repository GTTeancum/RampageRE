# How the Xbox prototype draws the static world

Source: `C:\Programming\GitHub\RampageRE\Rampage Xbox prototype\RampageD.xbe` (debug build), decompiled with Ghidra.
All addresses are virtual addresses in that XBE. File offsets are marked "file offset". Source file names come from
assert strings. "Uncertain" marks anything inferred rather than read directly from code or data.

## Summary (the design in ten lines)

1. No scene graph, octree or sectors on the render path. Only a few city blocks exist at once; every world object is
   sphere-tested against the frustum each frame, the survivors are sorted front to back, and drawn in four fixed passes
   (Ground, Main, PreAlpha, Alpha; Alpha runs back to front).
2. Buildings do not go through that list. One manager (`BuildingRenderMgr`, max 78 entries, pre-sorted by sheet
   texture at load) draws all window faces of all buildings, then all wall faces of all buildings, so the material
   changes about twice for the whole skyline.
3. A building wall is not a mesh asset. It is a grid of cells kept on the CPU, emitted as plain non-indexed triangle
   lists into fixed-size vertex buffers that are created once per building: 3 layers (window, shell, crack) x 3 faces
   (left, right, front; there is no back face), 24 or 28 bytes per vertex.
4. Damage only sets dirty bits (per layer, per face, per floor). The buffer is rewritten in place from the first dirty
   floor upward, and **at most one building is rewritten per frame**; the others keep drawing their stale buffer.
5. Building lighting is baked on the CPU into the vertex colour; the building vertex shader is 9 instructions
   (transform, copy colour, copy UV, fog) and needs 4 constants.
6. Static meshes are loaded from the bundle directly into contiguous memory in final vertex format (floats, one
   stream) and registered as D3D vertex/index buffers. Nothing is converted or copied at load, nothing is touched at
   run time.
7. Every material x modifier combination is compiled offline into a table (489 used records, 809 slots) that names the
   pixel shader, the vertex shader, the blend/z/alpha state and the textures per pass. 427 of 489 are single pass.
8. 80 vertex shaders x 3 skinning variants = 240. Up to 8 programs stay resident in the NV2A program store
   (8 slots x 17 instructions); switching is a `SelectVertexShader`, not a re-upload.
9. Shader constants are uploaded lazily through per-parameter dirty bits. A world-matrix change invalidates only the
   parameters that depend on it; an unlit textured object costs one 4-vector upload (world x view x projection).
10. Redundant state is removed at three levels: material apply is skipped if material and pass are unchanged, the pass
    record is diffed against the previous one, and every render-state setter has a shadow copy.

---

## 1. The per-frame draw loop

### 1.1 Order of a frame

`RWorldRenderer::render` (vf12, 0xa66f0) wraps `WorldRenderer::render` (vf12, 0x1de570), which runs:

| Step | Virtual | Function | What it does |
|---|---|---|---|
| 1 | +0x14 | 0x1de0c0 | camera, fog mode linear, fog start/end, fog colour |
| 2 | +0x08 | `RSceneryMgr` 0x30cf0 | sky box and backdrop, fog off, z-write off |
| 3 | +0x0c | 0xa6cd0 | block ground shapes (0x246e0), ground shadow sheet (0x4b750), then **BuildingRender (0x95730)** |
| 4 | +0x00 | 0x1de300 | build the visible-object list and sort it |
| 5 | +0x10 | 0xa6d00 | blob shadows for listed objects (0x2f5a0) |
| 6 | loop | 0x1de570 | passes 8 "Ground", 0x10 "Main", 0x20 "PreAlpha", 0x40 "Alpha" |
| 7 | +0x04 | 0x1de4a0 | per-object post-render hook, list cleared |

The light set is installed before and removed after (0x229500 / 0x229620).

### 1.2 Visibility of world objects

`WorldRenderer::buildList` (0x1de300) walks the world's single linked list of objects (list head at World+0x58).
For each object:

- skip if object flag 0x20 is clear (0x4d7f0);
- sphere against frustum (0x1d95f0 -> 0x20fda0 -> 0x20ea90): object centre at +0x40, radius at +0x50, tested against
  4 side planes plus near/far distances. Result 0 = outside, 1 = fully inside, 2 = straddles;
- ask the object (`vf10`, +0x28) whether it wants to render;
- take its `RenderObject` (object+0x74). If the sphere straddles and the render object has flag 0x100, call its finer
  test (`vf3`, +0x0c);
- append a 12-byte entry `{RenderObject*, float distance, u16 passMask, u16 clipResult}`.

The list is then quick-sorted **ascending by distance** (0x1de020 -> 0x1df050, comparator 0x1df510, ties broken by
pointer). There is no octree, sector, portal or distance cull in this path; `octree_tem.h` is only referenced from
0x21fcc0-0x220f70, which is not render code (uncertain what it serves; likely collision).

The clip result is passed down to each draw so that a fully-inside object skips per-mesh tests and is drawn with the
"needs clipping" device flag off (0x85380 / `GraphicsDevice` vf55 0x312c0). What that flag does at D3D level was not
traced (uncertain).

### 1.3 Passes and sorting

For each pass the loop calls one virtual on every listed render object whose mask has the pass bit:
Ground -> +0x10, Main -> +0x14, PreAlpha -> +0x18, Alpha -> +0x1c. Ground, Main and PreAlpha go front to back;
**Alpha iterates the list in reverse**, i.e. back to front.

Inside a shape the sub-meshes are split by a material flag (0x20000) into an opaque group and a blended group
(`LWShape` vf24/vf25, 0x23ed20 / 0x23eca0), so one object can contribute to Main and to Alpha.

During the Main pass an **instance cache** is switched on (when the flag at `WorldRenderer+0x230` is set;
0x1df7d0(1)):

- `StdMeshRenderer::render` (vf7, 0x2615b0) first tries `MeshCache::add` (0x246630). A mesh is accepted if its
  renderer is marked cacheable (renderer+0x20) and a material field is zero (0x2465f0 -> 0xef4d0; taken to mean
  "no modifiers attached", uncertain).
- `add` copies the current world matrix (64 bytes) into a preallocated array and links it into a per-renderer list.
  Capacity is **0x800 = 2048 instances** (constructed at 0x2aa810); the cache flushes itself when full.
- At the end of the pass `MeshCache::flush` (0x246300) sorts the distinct renderers (0x246c10, comparator 0x2470d0)
  by: opaque before blended, then **stage-0 texture**, then **material pointer**, then insertion order. For each
  renderer it calls `StdMeshRenderer::renderList` (vf12, 0x261800): bind vertex buffer and index buffer **once**,
  then per instance set the world matrix and draw.

So opaque static props are grouped by texture and material, and instances of the same mesh share one buffer bind.
Objects outside the Main pass, and meshes with modifiers, draw immediately.

### 1.4 The ground

The block manager code (0x246e0, next to `RBlockMgr.cpp`, which loads a "GroundShape" per block) makes three shape
draws (0x226f0 x3; taken to be the ground of the previous, current and next block, uncertain) plus a list of extra
pieces (0x23a20). Each is one ordinary static shape drawn with one world translation along the street. The ground
shadow sheet (0x4b750) is one vertex buffer drawn as a single 296-triangle strip.

### 1.5 Cost of one object in D3D calls

For one sub-mesh, one pass, immediate path (0x2615b0):

| Call | When |
|---|---|
| `SetStreamSource` (0xe9370) | always (no redundancy check at engine level) |
| `SetIndices` (0xe93d0) | always |
| material apply (0xee950) | nothing if same material + pass as the last draw; otherwise only the differences (section 3.3) |
| `SelectVertexShader` | only if a different program is wanted; `LoadVertexShader` only if it is not resident |
| `SetVertexShaderConstant` | only parameters whose dirty bit is set. After a world-matrix change: one call of 4 vectors for an unlit shader; plus one call each for 3 light directions and the eye position for a lit shader |
| `DrawIndexedVertices` / `DrawVertices` | one, whole sub-mesh |

Typical unlit textured prop with a repeated material: **4 calls** (stream, indices, 4 constants, draw). In the cached
path: **2 calls per instance** (constants, draw) after one stream + indices bind per mesh. No call is made for the
world matrix itself; `SetTransform` (0xe7f00) only stores it and clears dirty bits.

Counters kept per frame by the device: draw calls (+0xe0), vertices (+0xd8), triangles (+0xdc), material cache hits
(+0xe8), shader selects/loads (+0x16a8/+0x16ac), constants skipped/uploaded (+0x16b0/+0x16b4).

---

## 2. Vertex data

### 2.1 Where static vertices live

- Bundled meshes (`BundledSubMeshWrapper`, table 0x521924, set up in 0x264470): the bundle loader (0x226610,
  0x2281c0) carves 128-byte-aligned pieces out of one contiguous allocation (0x226210,
  `D3D_AllocContiguousMemory(size, 0x80)`) for vertex data. The vertex buffer is then created **around that memory**
  by `GraphicsDevice` vf19 (0xe73e0), which calls `D3DResource_Register` (0xeccf0) on a header that is already in
  the file; the index buffer by vf18 (0xe72a0), which only relocates the data pointer of an in-file header. No
  `CreateVertexBuffer`, no `Lock`, no copy, no format conversion. (That the vertex bytes are read from disc directly
  into the contiguous block is inferred from the loader's allocation calls, not traced byte by byte.)
  The wrapper has exactly one vertex format (it asserts the requested one equals its own, 0x264510).
- Loose `.pwk` meshes (`PwkPrimitiveWrapper`, `PwkSubMeshWrapper`): `CreateVertexBuffer` through vf16 (0xe7050) with
  usage 8 (write-only), one `Lock`/fill/`Unlock` at first use per requested vertex format (0x26f9d0, 0x27a860), then
  never touched. Index buffers through vf17 (0xe71a0): `CreateIndexBuffer(count*2, 0, D3DFMT_INDEX16)`, one memcpy.
- `XbVertexArray` (0xf40b0..0xf4280) is a thin wrapper: D3D buffer at +0x1c, stride at +0x10, locked flag at +0x18.
  Its destructor waits if the GPU still uses the buffer ("XbVertexArray was busy -- waiting!").

### 2.2 Vertex formats

The engine's format code is the D3D FVF bit set (decoded in 0x2101c0): 0x002 position (3 floats), 0x010 normal
(3 floats), 0x040 diffuse colour (4 bytes), 0x080 specular colour, 0x100/0x200 one/two UV sets (2 floats each),
0x006..0x00e blend weights. All data is in **one stream**.

The shader declarations inside the three shader blobs (rigid blob at 0x5c5bb0, file offset 0x5b26d0) use only these
types: no packed normals, no shorts for rigid geometry in this build.

| Use | FVF | Stride | Declaration |
|---|---|---|---|
| unlit textured (`MT_DECALTEX`) | 0x102 | 20 | v0 FLOAT3, v6 FLOAT2 |
| lit textured (`MT_SIMPLETEX`) | 0x112 | 32 | v0 FLOAT3, v3 FLOAT3, v6 FLOAT2 |
| vertex-coloured textured (`MT_DECALTEX_VCOLOR`, building shell/crack) | 0x142 | 24 | v0 FLOAT3, v4 D3DCOLOR, v6 FLOAT2 |
| lit + vertex colour (`MT_SIMPLETEX_LIT_VERTEX`) | 0x152 | 36 | v0 FLOAT3, v3 FLOAT3, v4 D3DCOLOR, v6 FLOAT2 |
| building day windows | 0x052 | 28 | v0 FLOAT3, v3 FLOAT3, v4 D3DCOLOR |
| two UV sets | 0x202 / 0x212 | 28 / 40 | + v7 FLOAT2 |
| bump mapped | - | - | v0, v6, v10 FLOAT3, v11 FLOAT3, v3 |
| skinned variant A / B | - | - | + v1 SHORT2 / SHORT4 indices, v2 FLOAT2 / FLOAT4 weights |

The release build was reported to use the same registers (v0, v3, v6, v1/v2). Whether release packs normals was not
checked here.

### 2.3 Indices and primitive types

- Indices are 16-bit, held in a D3D index buffer, bound with `SetIndices`, drawn with
  `DrawIndexedVertices(type, count, D3D__IndexData + start*2)` (0xeddb0; this is the XDK's inline expansion of
  DrawIndexedPrimitive). On Xbox this copies the indices into the push buffer on every draw.
- One draw per sub-mesh; the engine never splits a mesh.
- Engine primitive type -> D3D (table 0x4d0ad8): 0 points, 1 line list, 2 line strip, 3 **triangle list**,
  4 **triangle strip**, 5 fan, 6 quad list.
- Loose `.pwk` meshes are always triangle lists (0x2726d0 and 0x264bb0 return 3).
- Bundled meshes carry their own primitive type (u16 at +0x14, 0x264be0). The building roof code asserts type 4 for
  the roof mesh (0xa1020) and draws it with `indexCount - 2` primitives (0xa1e70), so shipped building shapes are
  **indexed triangle strips**. Whether every bundled world mesh is a strip is uncertain (it is data).
- Building walls and the ground shadow use **non-indexed** draws (`DrawVertices`).
- Dynamic immediate geometry (particles, debug) goes through `PrimDrawer` (`xb_prim_drawer.cpp`, 0xf0020), not
  covered here.

---

## 3. Shaders and state

### 3.1 How a material picks its shaders

A material is described by a 0xd8-byte descriptor: material type (`MT_*`, 126 types, names at 0x573998), the vertex
format, the set of attached modifiers (`mt_*`, 21 names at 0x573ba0: object/texture-space transform, lightning,
stripes, attenuated light, projected texture, alpha, building alpha, tweening, shadow, z-fade, colour, silhouette...)
and per-texture filter/clamp options (names at 0x573c00).

At material load, 0xf0430 hashes the descriptor (sum of its 54 dwords, modulo 809) and looks it up in a
**precompiled table** at 0x573c50 (809 slots x 0x19c bytes, 489 in use). If the combination is missing the debug
build appends it to `T:\custom_mtls.txt` for the offline shader compiler (0xf1550) and asserts. Nothing is assembled
at run time.

Each record holds, per pass (0x1c bytes each, at record+0xdc+0x50):

| Byte | Meaning |
|---|---|
| 0 | bit 0 z-write, bit 1 alpha test, bit 2 specular enable |
| 1 | blend: 0 off, 1 SRCALPHA/INVSRCALPHA, 2 SRCALPHA/ONE, 3 ONE/ONE, 5 ZERO/ONE |
| 2 | z function (20 = LESSEQUAL default, 24 = ALWAYS, 19 = EQUAL for later passes) |
| 3, 6.. | number of texture stages and which material texture slot feeds each |
| 0x0a.., 0x18 | pixel-shader constant bindings and their count |
| 0x14 | pixel shader index (0..144) |
| 0x19 | vertex shader index (0..79) |

plus per record: cull mode, per-slot sampler byte (filter, clamp) and up to four literal pixel-shader constants.

Pass counts over the 489 records: 1 pass 427, 2 passes 52, 3 passes 9. 79 of the 80 vertex shaders and 144 of the
145 pixel shaders are referenced. **Everything is pixel shaders** (`SetPixelShader`, 0x2b8160) and vertex shaders;
fixed-function lighting and combiners are not used (the only `SetLight` callers are inside D3D).

The skinned variants are not separate table entries: the same vertex shader index is looked up in one of three
80-entry tables (rigid 0x5c5498, skinned A 0x5cc950, skinned B 0x5d84b8; chosen in 0x261ea0). All 240 are created
once at start (0xe5500 -> 0xe5a80).

Examples from the table:

| Material | FVF | VS (instructions) | Constants needed |
|---|---|---|---|
| `MT_DECALTEX` | 0x102 | vs02 (8) | c96..99 |
| `MT_SIMPLETEX` | 0x112 | vs01 (28) | c96..99, eye, 3 light dirs, ambient, 3 light colours, 1 specular |
| `MT_DECALTEX_VCOLOR` | 0x142 | vs21 (9) | c96..99 |
| `MT_SIMPLETEX_LIT_VERTEX` | 0x152 | vs31 (18) | c96..99, 3 light dirs, ambient, 3 light colours |
| `MT_RAMPAGE_BUILDING_SHELL` | 0x142 | vs21 (9), ps40, blend 1, alpha test | c96..99 |
| `MT_RAMPAGE_BUILDING_CRACK` | 0x142 | vs21 (9), ps41, blend 1, alpha test | c96..99 |

vs21 in full (file offset 0x5b3c90):

```
DP4 oPos.x <- v0, c[96]      DP4 oPos.y <- v0, c[97]
DP4 oPos.z <- v0, c[98]      DP4 oPos.w <- v0, c[99]
MUL oPos.xyz <- r12, c[58]   DPH oFog.x <- v0, c[99] ; RCC r1.x <- r12.w
MOV oD0 <- v4                MOV oT0 <- v6
MAD oPos.xyz <- r12, r1.x, c[59]
```

### 3.2 Vertex-program residency

`GraphicsDevice::selectShader` (0xeb1e0), called before every draw: the NV2A's 136 instruction slots are treated as
8 slots of 17. Each slot remembers which shader starts there. If the wanted shader is resident,
`SelectVertexShader(handle, slot*17)` (skipped entirely if it is already the active slot). If not, it takes the first
free run of slots that fits (or restarts from slot 0 when none fits; no LRU), `LoadVertexShader` + `SelectVertexShader`.
Shaders longer than 17 instructions take several consecutive slots.

### 3.3 Constants: what is uploaded when

Registers: engine register N = hardware `c[96+N]`. `c[58]`/`c[59]` (screen scale/offset) belong to D3D.
`c[103]` = (0, 0.5, 1, 2) is written once at start (0xe5500).

Every vertex shader carries a list of parameter ids (table 0x5c5858). After selecting the shader, 0xe9620 walks the
list; a parameter is uploaded only if its valid bit (device+0x1520) is clear, then the bit is set.

| Id | Register | Content | Invalidated by |
|---|---|---|---|
| 1 / 3 | c96..99 | world x view x projection, 4 rows (0xe8390) | world, view or projection change |
| 2 / 4 | c100..102 | 3-row companion matrix (0xe8300 / 0xe8720; exact product uncertain) | world / view change |
| 0 | c138..140 | world matrix, 3 rows (environment mapping) | world change |
| 5 / 6 | c104 | eye position in object / world space | world / view change |
| 7..10 | c105..108 | light direction **in object space** (inverse world applied on the CPU, 0xe84f0 + 0xedcd0) | world change |
| 0xb..0xe | c105..108 | light position in object space | world change |
| 0x19..0x1b | c110..112 | material colours | value compare (memcmp) |
| 0x1d | c113 | ambient x material | value compare |
| 0x1e..0x21 | c114..117 | light colour x material diffuse | value compare |
| 0x26..0x29 | c118..121 | light specular x material specular | value compare |
| 0x2e..0x31 | c122.. | texture matrix n, 3 rows | value compare |
| 0x32..0x35 | c134..137 | light attenuation | - |

Consequences:

- There is no per-frame constant block. View-only data is uploaded the first time a shader asks for it.
- Per object, `SetTransform(world)` (0xe7f00) just clears the world-dependent valid bits (0xece20). The upload is one
  combined 4-row matrix; lit shaders add 3 object-space light directions and the eye position. Because lights are
  moved into object space, there is no normal matrix and normals are used raw (assumes rigid, uniformly scaled
  transforms).
- Colours are compared against the last uploaded value, so consecutive objects with the same material upload none.
- Fog is per vertex: `oFog = dot(v0, c[99])` (clip w), with D3D linear fog start/end set once per frame.

### 3.4 State-change minimisation

Three layers, all in `xb_pmaterial.cpp` / `xb_pmaterialcompiler.cpp` / `xb_device.cpp`:

1. `Material::apply(pass)` (0xee950): if the device's current material is this one, it is not marked changed, and the
   current pass equals the requested pass, return immediately (counted as a "material cache hit").
2. `applyPass` (0xf04d0) diffs the new pass record against the previously applied one: `SetTexture` only for stages
   whose texture pointer differs; the 4 sampler states of a stage only if its sampler byte differs; `SetPixelShader`
   only if the handle differs; pixel-shader constants compared against a shadow copy; z-write / alpha-test / specular
   / blend / z-func / cull each only if the byte differs.
3. Every device render-state setter keeps a shadow value and returns early when unchanged: enum states (vf44,
   0xe8b90), bool (vf46, 0xe8ca0), int (vf48, 0xe8d90), float (vf50, 0xe8e20), sampler (vf52, 0xe8eb0).

D3D state blocks are not used by the engine (`ApplyStateBlock` is only called from D3D/XGraphics internals).
`SetStreamSource` and `SetIndices` are **not** filtered.

---

## 4. Destructible buildings

Code: `rampage\code\game\Building.cpp`, `BuildingState.cpp`, `BuildingFloor.cpp`, `BuildingRenderMgr.cpp`,
`CrackMesh.cpp`, `BuildingShadow.cpp`, `BuildingFxMgr.cpp` (0x86000..0xa2000).

### 4.1 Representation

A building is a box of cells, not a mesh:

- `BuildingState` (about 0x45c bytes, at `Building`+0x1ac; init 0x99770). Width up to 8 cells, depth exactly 2 cells,
  up to 19 floors (asserts at 0x99770). Cell width 5.0, floor height 3.0, first floor starts at height 6.0.
- Per floor (`BuildingFloor`, init 0x8d220 / 0x8d890):
  - a cell array (16 bytes per cell, floor+0x2c): damage state, window variant, flags;
  - a ring of grid vertices (32 bytes each, floor+0x30; `width + 3 + 2*depth` per floor): colour (rgb = baked light,
    **alpha = crack level 0..3** stored as level*255/3, 0x9fb20), a second colour for windows, the face normal, and a
    dent offset;
  - optional "Skeleton" / "DamagedSkeleton" shapes (ordinary static meshes of the interior).
- Roof ("TopFloorShapes"), ground floor ("GroundFloorShapes"), rubble ("RubbleShape") and adornments are ordinary
  static shapes.

The walls are emitted from this grid into **9 vertex buffers per full-detail building**: 3 layers x 3 faces.

| Layer | Day format | Night format | Content |
|---|---|---|---|
| 0 Window | FVF 0x052, 28 bytes (pos, normal, colour; cube-map reflection) | FVF 0x142, 24 bytes ("InnerWindowTexture") | one quad per intact cell |
| 1 Shell | FVF 0x142, 24 bytes | same | one quad per cell (+ edge caps on the front), UVs pick the cell's picture from the building's sheet texture by window variant and damage state |
| 2 Crack | FVF 0x142, 24 bytes | same | quads only where a corner has crack alpha > 0; 4 quads per cell that still has its window, else 1 |

Format/stride tables at 0x53ad60 / 0x53ad68, layer-to-format table at 0x4c36d4. Each quad is written as **6 vertices**
(two triangles, no indices) by 0x920d0 / 0x92360.

Each buffer is created once at building creation (0xa0e70 -> 0xa0ec0, usage 8) at its worst-case size and never
reallocated. Capacity in triangles: window and shell sides `2*depth*floors`, window front `2*width*floors`, shell
front `(2*width+4)*floors`, crack `8*cells*floors`. Example, 8 wide x 18 floors: shell front 360 triangles
(1080 vertices, 26 KB), crack front 1152 triangles (83 KB).

Distant ("DistFadeLevel" > 0) buildings and the non-interactive backdrop buildings get fewer layers: shell only by
day; window + shell at night; never a crack layer (0x99770 tail).

### 4.2 Drawing

`BuildingRenderMgr` (0x94770 init, 0x94b70 prepare, 0x95730 draw). Its entry array (32 bytes each, **max 0x4e = 78**)
holds every building plus three arrays of backdrop building states; it is sorted once by sheet texture
(0x95e60; the debug build logs "%i uses of sheet texture %s").

Prepare, every frame (0x94b70):

- box against the 4 frustum side planes (0x20ece0) -> outside / inside / straddles;
- face selection from the camera position: left face only if the camera is left of the building's left edge + 1,
  right face only if right of the right edge - 1, front always. The back is never built or drawn;
- interactive buildings are insertion-sorted **far to near** into a second list;
- buildings with any crack alpha (0x9b5f0) start the "sorted" group: from the first such building in that list
  onward, buildings are drawn one by one back to front (cracks are blended);
- the rewrite budget is decided (section 4.3).

Draw (0x95730), in this order:

1. all un-sorted entries: **window** layer (0x9df80);
2. all un-sorted entries: **shell** layer (0x9e0e0);
3. all un-sorted entries: shapes (0x9e480 "PreShapes": rubble, roof, ground floor, damaged-floor skeletons;
   0x9ec70 "PostShapes": adornments);
4. the sorted (cracked) entries, back to front, each: shapes, window, shell, **crack** (0x9e180), adornments.

One layer of one building (0x9e220) = set world matrix (a translation), apply the layer's material (a no-op when the
previous building used the same one), then per selected face `SetStreamSource` + `DrawVertices(TRIANGLELIST, 0,
bytesUsed/stride)` (0x9fc00). So an undamaged building costs at most 2 layers x (1 constant upload + up to 3 x 2
calls). Around the shell layer (full-detail buildings only) the code turns on alpha test (func GREATER, ref 1) and
restores z-write afterwards; around the crack layer alpha test GREATER with ref 8 and z function engine value 0x17,
which the value table at 0x4d0a08 maps to GREATEREQUAL (why not LESSEQUAL is not understood; uncertain).

### 4.3 What happens on damage

Damage never touches a vertex buffer directly.

- Cell damage (0x9c0b0, 0x9c630) and vertex damage (0x9d100, `addVertRadialDamage` 0x9f230) change the cell state,
  the crack level in the grid vertex alpha and the dent offset, then OR a mask into the floor's dirty word (floor+8)
  and the building's dirty word (state+0x1e4). Bit = `layer*3 + face`; the floor below is marked too, because quads
  span two floors' vertex rings.
- `updateStreams(layer, face)` (0x9b100, profile tag "UpdateStreams"): if the bit is set, find the first dirty floor,
  count the quads of every floor from there up (0x8e0d0), `Lock` the buffer (flags 0), seek to that floor's stored
  byte offset, and **re-emit every floor from the first dirty one to the top** through the emitter table at 0x4c2f00
  (`emitFloor{L,R,F}_{Window,Shell,Crack}`, 0x8e300..0x91b80; night window variants 0x8ee90..0x8f850). Each floor's
  start offset is stored for next time. Floors below the first dirty one are not rewritten.
- **Budget** (end of 0x94b70): for each visible building, 0x9af30 classifies the faces that will be drawn:
  0 = clean, 2 = dirty and never built (must build now), 1 = dirty but a previous version exists. All class-2
  buildings are built immediately. Class-1 rebuilds are limited to `1 - (number of class-2 buildings)` per frame,
  i.e. **at most one stale building is rewritten per frame**, chosen round-robin from the frame counter. A debug
  switch raises the limit to 1024. Faces that are not selected for drawing are not rebuilt at all.
- Lighting change (0x86830 "BuildingLighting" -> 0x9d9c0 "ApplyLighting"): recompute every grid vertex colour, mark
  everything dirty; the rewrite then goes through the same budget.

### 4.4 Collapse and debris

- Floor collapse (`startCollapse` 0x9de60, per-frame `updateCollapse` 0x99300, speed from "CollapseTime"): the state
  stores the collapsing floor index and a 0..1 progress. Every frame it marks the building dirty; the emitters lower
  every vertex at or above that floor by `progress * floorHeight` (0x921c0 / 0x92470), so the upper part slides down
  by **re-emitting the buffers**, subject to the one-per-frame budget. The collapsing floor itself is shown by its
  skeleton mesh squashed vertically by `1 - progress` (0x9e480). When progress reaches 1 the floor is removed from
  the floor order table and the count drops.
- Whole-building collapse (0x99220): the building is translated downward and drawn with a ground clip plane enabled
  (clip flag 0x10000, height 1.1, in 0x9e480); no vertex work.
- Roof cracks (`CrackMesh`, 0xa1020): a second copy of the roof mesh's vertices in FVF 0x142 with a 3-byte per-vertex
  link to the grid. On change (0xa1a60) the buffer is locked and **only the colour dword of each vertex is
  rewritten**; it is drawn as an indexed strip reusing the roof's index buffer, and only while some crack alpha is
  non-zero.
- Debris, dust and falling pieces are effects started by `BuildingFxMgr` (0x93200, 0x93600) through the FX system,
  which can refuse them on cost/benefit ("BuildingFxMgr nixed creation of %s", `fx_limiter.cpp` 0x123be0). They are
  not building geometry. Not traced further here.

---

## 5. Lighting of the world

- Lights: a light set per level and time of day (0xa5a70 reads "LightFile"; up to 3 directional lights, ambient,
  "RampStart"/"RampEnd"/"RampIntensity", shadow tints, cube map and tint). The set is copied into device arrays
  (vf33/vf35/vf37, called from 0x229500); D3D lights are never used.
- Props and vehicles with lit materials: lit **per vertex on the GPU** in the vertex shader: ambient x material +
  up to 3 directional lights (N.L clamped) x material, one specular term in the full variant (vs01, 28 instructions;
  vs31 without specular, 18). Light directions arrive already in object space.
- Unlit materials (`MT_DECALTEX*`, `*_VCOLOR`): texture times constant or baked vertex colour, 8-9 instruction
  shaders.
- Buildings: lit **on the CPU**, once, into the grid vertex colours (0x9d9c0 calling the world light function
  0xa6910: 3 directional N.L terms with the face normal, ambient, plus a height ramp, plus the building-shadow test
  0x96db0 / 0x972a0). Redone only when the lighting flag or the shadow state changes.
- Day/night: a global flag selects the day or night property set (index `(flag != 0) + 0x4f` in 0xa5a70 and
  0x308d0) and the night window emitters; it is a level-load choice, not a per-frame blend (no blend code found;
  uncertain whether a transition exists elsewhere).

---

## 6. Budgets and limits found

| Item | Value | Where |
|---|---|---|
| Push buffer / kick-off size | 2 MB / 32 KB | `Direct3D_SetPushBufferSize(0x200000, 0x8000)`, 0xe5020 |
| Instance cache | 2048 world matrices per flush (two 128 KB arrays) | 0x2aa810, 0x246430 |
| Building entries | 78 | 0x94770 |
| Building size | width <= 8 cells, depth = 2, <= 19 floors | 0x99770 |
| Building rewrites per frame | 1 (plus never-built faces) | 0x94b70 |
| Ground shapes drawn | 3 (uncertain: previous/current/next block) | 0x246e0 |
| Vertex program residency | 8 slots x 17 instructions | 0xeb1e0 |
| Vertex shaders | 80 x 3 variants; longest rigid 43, skinned 59 / 67 instructions | 0xe5500 |
| Pixel shaders | 145 | 0xe5500 |
| Material records | 489 used of 809 slots; 87% single pass | 0x573c50 |
| Lights per shader | 3 directional (+2 point in special shaders); device holds 8 | 0xe9620 |
| Ground shadow sheet | 296-triangle strip | 0x4b750 |

Perf reporting: `RenderPerfRec` (0x239070) prints, per named block, triangles, vertices, "Material Transitions",
"Material Cache Hits", "DrawPrim Calls" and "Avg. Triangles per DrawPrim"; the on-screen line is
"Verts/frame, Tris/frame, Mat miss/frame" (0x1e3290). Those are the numbers the developers tuned against: material
misses and triangles per draw call. No numeric targets are stored in the executable.

---

## What the port should copy

Concrete points for a renderer that writes NV2A push buffers directly (static vertex arrays in GPU memory, inline
16-bit indexed draws, vertex programs available).

**Buildings (the worst case first)**

1. Give each building its own fixed, worst-case-sized vertex arrays in GPU memory, split by layer and by face. Never
   allocate or free on damage.
2. Emit walls as plain triangle lists straight from the cell grid, 24 bytes per vertex (position floats, colour,
   UV floats). Draw them with a non-indexed array draw: no index data in the push buffer at all.
3. On damage, only set a dirty bit (layer, face, floor). Rewrite in place from the first dirty floor upward, keeping
   each floor's start offset.
4. **Cap rewrites at one building per frame**, round-robin; keep drawing the stale buffer meanwhile. Only a face that
   has never been built may exceed the cap. Do not rebuild faces that are not going to be drawn.
5. Bake building lighting into vertex colour on the CPU and run buildings through the minimal program
   (4 dot products, colour copy, UV copy, fog; constants = one 4-row matrix). No normals, no light constants.
6. Select faces by camera position (left / right / front) before drawing, and never build a back face.
7. Draw the skyline layer by layer, not building by building: all windows, then all shells, with the entry order
   pre-sorted by sheet texture, so material and texture change a handful of times per frame.
8. Only cracked buildings leave that batch; draw those back to front after the rest, with the crack layer last.
9. Animate a floor collapse by offsetting the emitted vertices through the same budgeted rewrite, and a whole-building
   collapse by a translation plus a ground clip, not by editing geometry.
10. For meshes that only change colour (roof cracks), rewrite only the colour field in place.

**Static meshes**

11. Store vertex and index data on disc in final GPU layout and load it directly into GPU-visible memory; no
    per-vertex work at load.
12. One stream, one draw per sub-mesh. Prefer indexed strips for shipped meshes (fewer inline indices per triangle).
13. Collect opaque instances during the main pass (matrix copy only), then sort by texture, then material, and draw
    each mesh's instances back to back with the vertex array bound once: per instance = one 4-row matrix + one draw.
14. Cull with one sphere test per object and pass "fully inside" down so sub-meshes skip their own tests. The
    prototype uses no spatial structure at this world size.
15. Sort opaque front to back, draw blended geometry in a separate pass back to front.

**Shaders, constants, state**

16. Upload one combined world x view x projection matrix per object. Transform light directions and the eye into
    object space on the CPU so lit programs need no normal matrix.
17. Give every program a list of the constants it reads and keep a valid bit per constant; a world-matrix change
    clears only the world-dependent bits. Compare colour constants by value before uploading.
18. Keep several vertex programs resident in the 136-slot program store and switch by start address; upload a
    program only when it is not resident.
19. Precompute per material a pass record (program, combiner setup, blend, z, alpha test, cull, textures) and apply
    it by diffing against the previous record; skip entirely when material and pass repeat.
20. Shadow every render state and return early on equal values. Also filter redundant vertex-array binds, which the
    prototype does not do.
21. Use the shortest program that fits: 8-9 instructions for unlit and vertex-coloured geometry, 18 for lit without
    specular, 28 only where specular is needed.

**Measure what they measured**

22. Track per frame: draw calls, triangles per draw call, material misses, constants uploaded vs skipped, program
    loads vs selects.
