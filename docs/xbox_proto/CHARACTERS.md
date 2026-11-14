# Characters in the Xbox prototype: monsters, people, army, vehicles

Reverse-engineered from `Rampage Xbox prototype/RampageD.xbe` (debug build, October 2005). Addresses are virtual
addresses in that file. Vertex-shader constant numbers are given two ways: `c[n]` is the Direct3D register
(-96..95), `C[n]` is the NV2A slot (0..191) = Direct3D register + 96, which is also what `tools/xbe_vshdis.py` prints.
Functions the Ghidra dump missed (the three BundledSkinRenderer draw functions) were disassembled by hand from the
ELF with capstone; everything else is from the decompiled C.

## Summary

1. A monster is one skinned mesh, split offline into sections of at most 48 bones, drawn on the GPU by
   `BundledSkinRenderer`. One section with one bone is drawn as a plain rigid mesh instead. No CPU skinning in the
   normal path.
2. Skinned vertices are small: position float3, bone indices as 2 or 4 SHORTs, weights as 2 or 4 floats, normal
   float3, uv float2: 44 bytes (two bones) or 56 bytes (four bones). Sections are indexed triangle strips.
3. The bone index stored in the vertex is the final constant register number, so the shader does `ARL` and reads the
   register directly: 18 instructions unlit / 44 lit for two bones, 26 / 52 for four. Two-bone or four-bone is chosen
   per section from the section's "max influences" field.
4. The palette is world-space 3x4 matrices (three constant rows per bone), uploaded per section with one
   SetVertexShaderConstant per bone; the shader then multiplies by one shared view-projection in C[96..99].
5. Animation keys are 8 bytes (16-bit time + three 16-bit values), sampled through a per-track cursor that caches
   the current key pair and a precomputed slerp arc; only animated tracks are touched.
6. Clip changes cross-fade from a frozen snapshot of the old pose over 0.15 s; two clips are sampled together only
   for the partial-body overlay. Rigid node trees skip sampling and recomposition with a per-node time cache and
   version counters.
7. People are `Actor` objects, each either a flip-book sprite (one textured quad, 4 vertices) or a "lightweight"
   shape of rigid pieces. Window occupants are 28-byte records; only one is turned into a live Actor every 0.75 s,
   and only near the monsters. There is no street-crowd class in this build.
8. Army units are rigid node-tree shapes capped at 20 nodes and 20 meshes; traffic is plain rigid objects on a line
   mover, default cap 7 vehicles per lane set.
9. Shadows are blob quads: monsters draw one ground quad plus up to two wall quads; all other objects are batched
   into at most 4 draws (one per shadow texture, 6 vertices per object). Building shadows on the ground are a
   vertex-coloured grid drawn in one call and recomputed only on change.
10. Cloth is simulated on the CPU in fixed sub-steps and written (position + normal only) into a per-instance dynamic
    vertex buffer on each draw.

---

## 1. Monster skinning

### Which renderer

`SkinInstance` set-up is `FUN_00212930` (source `3space\win32\Skin.cpp`). For each mesh section it picks a
`MeshRenderer` subclass:

| Section | Renderer | Evidence |
|---|---|---|
| bone count < 2 | `StdMeshRenderer` (rigid, 0x34 bytes) | `if (*(uint*)(section+0xc) < 2)` in `FUN_00212930` |
| more bones, section is a bundled skin section | `BundledSkinRenderer` (0x3c bytes) | `___RTDynamicCast` succeeds, then `FUN_00262020` |
| more bones, cast fails | `StdMeshRenderer` | same function |
| entries of the second ("software") list | `SSkinRenderer` (0x1c bytes) | tail loop of `FUN_00212930` |

`SSkinRenderer` is a stub on Xbox: its draw slots (`0x00262ca0`, `0x00262cd0`, `0x00262d00`, `0x00262c40`,
`0x00262c70`) only assert "Feature not yet implemented." `SoftwareBlendMesh` (source
`3space\win32\software_blend_mesh.cpp`) is created only by `Skin::loadPwkFile` (`FUN_00211f20` -> `FUN_002126e0`),
the loose-file path used when a skin is not in a bundle; its CPU blend (`FUN_00260a30`, position + normal, source
vertex 68 bytes with a count and up to four weights) runs when the vertex buffer is built. The shipping path is
`FUN_00212210` (bundle record type 0x10), reached from the resource loader `FUN_001e7600`, which tries the bundle
first. So: **bundled skins -> GPU, always**; the software path is a tool/PC leftover that cannot draw on Xbox.

The only CPU skinning that runs in the game is for cloth anchor points (section 4).

### Section record (bundle), 0x58 bytes

Accessors `FUN_00214610..FUN_00214670`, `BundledSkinSectionWrapper` virtuals at `0x00265350..0x002653e0`,
fix-up `FUN_002652a0`:

| Offset | Meaning |
|---|---|
| +0x14 | 6 floats (bounds, returned by vf7 `0x002653a0`) |
| +0x2c | 4 floats (bounding sphere, vf4 `0x00265370`) |
| +0x44 | u16 material id |
| +0x46 | u16 max influences per vertex in this section |
| +0x48 | u32 bone count of this section |
| +0x4c | vertex array (file offset, fixed up to an `XbVertexArray` by `GraphicsDevice::vf18` `0x000e72a0`) |
| +0x50 | index array (same, `GraphicsDevice::vf19` `0x000e73e0`) |
| +0x54 | pointer to the section's bone map (u32 skeleton bone index per palette slot) |

Vertex and index buffers are pre-baked in the bundle; nothing is converted at load. Primitive type is fixed:
`BundledSkinSectionWrapper::vf8` (`0x00265930`) returns 4 = triangle strip.

### Vertex format

Vertex declarations are stored next to each compiled shader. Decoded from the shader blobs (file offsets
`0x5b9d34`, `0x5c591c`, `0x5b2774`):

| Variant | Declaration | Stride with normal + uv |
|---|---|---|
| rigid | v0 FLOAT3, v3 FLOAT3, v6 FLOAT2 | 32 |
| two-bone | v0 FLOAT3 (pos), v1 SHORT2 (bone registers), v2 FLOAT2 (weights), v3 FLOAT3 (normal), v6 FLOAT2 (uv) | 44 |
| four-bone | v0 FLOAT3, v1 SHORT4, v2 FLOAT4, v3 FLOAT3, v6 FLOAT2 | 56 |

Tokens: `40320000 40250001 40220002 40320003 40220006` (two-bone), `40320000 40450001 40420002 40320003 40220006`
(four-bone). Weights and normals are plain floats, not packed. Bump variants add v10/v11 FLOAT3. Unlit variants
drop v3.

### Bone indices are register numbers; the palette layout

`BundledSkinRenderer::vf7` (`0x00262090`, hand-disassembled) uploads palette slot `i` to the register found in the
48-entry signed-byte table at `0x005ed780`:

```
-96 -93 -90 -87 -84 -81 -78 -75 -72 -69 -66 -63 -60 -57 -54 -51 -48 -45 -42      (19 bones)
-36 -33 -30 -27 -24 -21 -18 -15 -12  -9  -6  -3                                  (12 bones)
 45  48  51  54  57  60  63  66  69  72  75  78  81  84  87  90  93              (17 bones)
```

In NV2A slots that is `C[0..56]`, `C[60..95]`, `C[141..191]`. Skipped on purpose: `C[57..59]` (`C[58]`/`C[59]` are
the screen scale/offset the shaders end with) and `C[96..140]` (the shader's ordinary constants: transform, fog,
lights, material).

The vertex stores that register number directly in v1 (a SHORT in -96..93), so the shader needs no multiply:

```
ARL a0.x, v1.x
MUL r2, v2.x, c[a0.x+0]      ; tools print this as c[a0.x+96]
MUL r3, v2.x, c[a0.x+1]
MUL r4, v2.x, c[a0.x+2]
ARL a0.x, v1.y
MAD r5, v2.y, c[a0.x+0], r2  ...
DPH r8.x, v0, r5  /  r8.y, v0, r6  /  r8.z, v0, r7       ; skinned position in WORLD space
DPH oPos.x..w, r8, C[96..99]                             ; world -> clip
DPH oFog.x, r8, C[102]
MUL oPos.xyz, r12, C[58] ; RCC r1.x, r12.w
MAD oPos.xyz, r12, r1.x, C[59]
```

(debug-build shader at file offset `0x5b9bec`, 18 instructions; lit `0x5b9d34`, 44; four-bone `0x5c5754`, 26 and
`0x5c591c`, 52. Same programs as the release build.)

Limits asserted in code: bone count per section <= 0x30 (48) in `FUN_00262020` and again in the draw; register
within -96..93. 48 bones x 3 rows = **144 palette constants**.

### How the matrices are computed and uploaded

- Once per frame per skin instance (`FUN_00212df0`, guarded by the "posed" byte at skinInstance+0x24, cleared by
  `FUN_00212f30`): first `FUN_00158240` recomposes the transform tree if its dirty byte (+0xd) is set, then for every
  skeleton bone `FUN_0013ac70` builds the skin matrix (bind-pose offset combined with the bone's current world
  matrix: one 3x3 multiply plus one point transform) into a flat array of 0x30-byte matrices (`FUN_00211ba0` =
  `base + bone*0x30`). An optional uniform scale is applied here.
- The matrices are **world space**. Proof: skinned shaders are fed semantic 3 for C[96..99] (`FUN_000e8220`, the
  cached view x projection, validity bit 0 of device+0x954, untouched by world-matrix changes) while rigid shaders
  get semantic 1 (`FUN_000e8390`, world x view x projection). Semantic lists decoded from the tables: rigid unlit
  `(1)`, skinned unlit `(3, 4)`; rigid lit `(1,7,8,9,5,30,31,32,29,38)`, skinned lit `(16,17,18,6,3,30,31,32,29,38,4)`
  - i.e. skinned lighting uses world-space light directions and eye position (16/17/18/6) where rigid uses
  object-space ones (7/8/9/5).
- Storage is 4 rows of 3 floats (X axis, Y axis, Z axis, translation). The upload transposes each into three
  4-float rows on the stack (`[0],[0xc],[0x18],[0x24]` / `[4],[0x10],[0x1c],[0x28]` / `[8],[0x14],[0x20],[0x2c]`) and
  calls `D3DDevice_SetVertexShaderConstant(register, rows, 3)` (`0x002b8120`) - one call per bone.

### Shader choice

Three parallel tables of 80 shader variants, 12 bytes per entry, created by `FUN_000e5500` -> `FUN_000e5a80`:
rigid `0x005c5498`, two-bone `0x005cc950`, four-bone `0x005d84b8` (240 shaders total, 160 of them skinned).

- Two- vs four-bone: `BundledSkinRenderer::vf0` (`0x00261ea0`) picks the table from the byte at +0x2d, which
  `FUN_00262020` set to `maxInfluences > 2`. It also ORs 8 (two-bone) or 0xC (four-bone) into the material's vertex
  format before asking the mesh for its buffers.
- Which of the 80 (lit/unlit, textured, fog, bump...): the material pass's byte at pass+0x69 indexes the table. So
  the material decides lighting; the renderer only decides rigid / 2 / 4.

### Shader program cache (worth copying)

`FUN_000eb1e0` (the SetVertexShader wrapper) treats the 136-instruction program store as 8 slots of 17
instructions. A shader stays loaded at its slot; reselecting it is `D3DDevice_SelectVertexShader(handle, slot*17)`
(`FUN_000edd90`) with no upload. Only a shader not present is loaded (`D3DDevice_LoadVertexShader`, `FUN_000edd70`),
evicting what it overlaps. Counters at device+0x16a8 (hits) and +0x16ac (loads). Constants are then set per
semantic only if that semantic's valid bit is clear (`FUN_000e9620`, bit array at device+0x1520).

### Draw cost per skinned section (`0x00262090`)

1. frustum test of the section sphere (`FUN_0022feb0` -> `FUN_00202cc0`): outside = return; fully inside = draw
   with no clip state; straddling = set clip state, restore after (`FUN_0013ad60`);
2. N x SetVertexShaderConstant (3 rows each), N = bones of this section (<= 48) - re-sent for every section, every
   time it is drawn; nothing is shared between sections;
3. SetStreamSource (`GraphicsDevice::vf57` `0x000e9370`), SetIndices (`vf58` `0x000e93d0`);
4. per material pass: apply pass state (`FUN_000ee950`), select shader (`FUN_000eb1e0`), DrawIndexedVertices
   (`FUN_000eddb0`, triangle strip) or DrawVertices (`FUN_000edeb0`) if the section has no index buffer.

`0x00262450` and `0x00262840` are the same routine for the other two render entry points (alternate
material / single pass).

The monster itself: `ROMonster::vf7` (`0x00082df0`) runs "PreloadTextures", "PreRender", then "SkinInst" =
`FUN_002132a0(worldMatrix, monster+0x978, cull = 0)`, then "ClothInst" (`FUN_0021c6e0`) if the monster has cloth,
then post-alpha effects. Monsters are not sphere-culled as a whole (cull argument 0); sections still are.

Not measured: how many sections and bones a real monster has (that is in the data files, not the code).

---

## 2. Animation sampling

Sources: `3space\key_interp_anim.cpp`, `rawanim.cpp`, `xform_tree_inst.cpp`, `char_xftree.cpp`,
`rampage\code\game\MonsterXFormTree.cpp`.

### Storage (`KeyInterpAnim`, vtable `0x00523eac`)

- Rotation tracks at anim+0x3c, variable length: `u8 node, u8 keyCount`, then 8-byte keys: `u16 time` + three
  `s16` (x, y, z of a unit quaternion; w is rebuilt as `sqrt(1 - x^2 - y^2 - z^2)`, sign in bit 0 of the time word;
  the low bits of the three values carry a small correction term) - decoder `FUN_00276120`.
- Position tracks at anim+0x38: 12-byte header, `u8 node` at +0xc, `u8 keyCount` at +0xd, 8-byte keys at +0x10:
  `u16 time` + three `s16` quantised values - decoder `FUN_00275f10`.
- Time is normalised: clip position 0..1 becomes a 16-bit value, even numbers only, last key >= 0xFFFE
  (`FUN_00275eb0`). Keys are irregular (key-reduced), not fixed-rate.
- Tracks with 0 keys use the skeleton rest value; with 1 key a constant. Only nodes that actually move have keys.
- `RawAnim` (vtable `0x00524434`) is the uncompressed sibling; not traced in detail.

### Sampling with a cursor

`KeyInterpAnim::vf14` (`0x00272f30`, to quaternion + position, 28 bytes per node) and `vf15` (`0x00272e60`,
straight to 3x4 matrices) walk the anim's track lists with one cursor per track:

- position cursor `FUN_00274be0`: holds the current key index, the two key times, decoded key 0, the delta
  (key1 - key0) and `1/(t1 - t0)`. If the new time is still inside `[t0, t1]` the cost is one multiply-add per
  axis. Otherwise it searches **forward from the cached index** (from the start only if time went backwards) and,
  when it advanced by exactly one key, reuses the old key 1 as the new key 0.
- rotation cursor `FUN_00274fb0`: same idea, plus it precomputes a slerp arc for the key pair (`FUN_00182e00`,
  "QuatArc::computeArc": start quaternion, a perpendicular quaternion and the angle). Evaluation `FUN_00058280` is
  `cos(t*angle)*q0 + sin(t*angle)*qperp` then normalise; nearly identical keys fall back to a straight lerp.
- cursors are initialised by `KeyInterpAnim::vf17` (`0x00273110`).

The stateless per-node samplers (`vf6` `0x00272810`, `vf7` `0x00272ae0`) do a linear key search from the start each
call; they are used only where one node is needed (rigid trees, the overlay blend below).

### Monster pose (`MonsterXFormTree::update`, `FUN_0005acd0`)

- Tree is `FixedXFormTreeInst<68>` (RTTI `$0EE` = 0x44): **at most 68 nodes**.
- Main clip: `anim->vf14(pose, time, cursors)` - one pass over animated tracks only.
- Clip change: `FUN_0005d6f0` snapshots the current pose; for the next 0.15 s (`0x3e19999a`, stored with its
  reciprocal) each node is blended snapshot -> new clip with a quaternion arc (`FUN_0005c850` / `FUN_0005c920`),
  after flipping the sign when the two quaternions are in opposite hemispheres. The old clip is not sampled again.
- Partial-body overlay (per-node pointer table at tree+0x1c64*4): for flagged nodes both clips are sampled with the
  stateless per-node sampler and blended with the node's weight. This is the only place two clips are evaluated.
- Optional mirroring (`FUN_002114d0`, `mirror_biped_anim.cpp`), root-motion extraction (`FUN_0005a260`).
- Then compose, `MonsterXFormTree::vf3` (`0x00059fe0`): nodes are stored parent-first; node i = local(i) x
  world(parent) (`FUN_00057c00`), unless a procedural animator owns the node (head, torso twist, tail, legs, IK -
  `MonsterProcAnimator` subclasses), in which case that animator writes the world matrix.

No level of detail, update-rate limit or off-screen skip was found for monsters; they are always fully animated.

### Rigid trees (`XFormTreeInst::vf3`, `0x00211100`)

Per node a 0x40-byte entry: local matrix, `+0x30` time last sampled, `+0x34` per-node time override, `+0x38` own
version, `+0x3c` parent version last seen.

- a node is resampled only if its time differs from the cached time;
- a node's world matrix is recomposed only if its own sample changed or its parent's version changed.

A shape whose animation time has not moved costs nothing per frame.

---

## 3. People, army, vehicles

There are no People / Crowd / Civilian / Soldier / Vehicle classes in this build (all 602 RTTI classes were
listed). What exists:

### Actors = people (`Actor`, `.\code\game\Actor.cpp`, 260 bytes each)

`Actor::init` (`0x000ab750`) reads a list of named animations, all of one kind ("Cannot mix animation types!"):

**`ActorAnim` -> `ActorAnimBitmap`: a flip-book sprite.** (constructor `FUN_000ad0c0`)
- frames are separate textures named `BaseName` + two digits; optional `FrameList` with a duration per frame,
  otherwise frames are discovered until one is missing and share `Duration` equally;
- advance (`0x000ada10`): adds frame time; on a frame change swaps the texture in the actor's material; flags for
  reverse and repeat;
- draw (`0x000adc10`): **one textured quad, 4 vertices, immediate mode** (`PrimDrawer` begin `(0x102, 1 primitive,
  type 6 = quad)`). Size = texture width and height / 32 x `AnimScale`, standing upright in the actor's local XZ
  plane (the camera looks along Y, so no billboard maths). Texture coordinates are mirrored when the actor faces the
  other way (bit 0x8000, set from the sign of the facing vector in `Actor::update` `0x000abf00`).

**`ActorAnimShape`: a real model.** (constructor `FUN_000adfd0`)
- a shape plus an optional animation; pose = one normalised time handed to the shape (`shape->vf7`), draw =
  `shape->vf4`;
- the shape class is `LWShape` / `LWAnimShape` ("lightweight"): **rigid pieces on a node tree, no skinning**. Draw
  loop `LWAnimShape::vf27` (`0x0022f3e0`): for each piece (0x44-byte entry: material, node index, embedded
  `StdMeshRenderer`) `FUN_0022ed10` sets the world matrix **only if this piece's node differs from the previous
  piece's**, then the renderer draws (stream, indices, per pass: state, shader, one draw). So one draw per piece,
  one matrix per node, matrices not repeated for consecutive pieces on the same node.

`ROActor::vf5` (`0x000ab3b0`) is the actor's render object: scale state, world matrix (shifted with its building if
it stands in a window), then the current animation's draw.

### Window occupants (`.\code\game\OccupantManager.cpp`)

- Per building, at load (`FUN_000bac60`): up to 300 window slots are queried, shuffled, and filled with
  `NumMilitaryOccupants` / `NumNonMilitaryOccupants` / `NumEdibles` records. A record is 28 bytes: building, column,
  floor, side, occupant type (byte), edible type (byte), two actor pointers. **No object, no draw.**
- Each frame (`FUN_000bbd10`): a timer; every **0.75 s** (`0x004bf974`) it picks **one** record of a randomly
  weighted type whose window lies in the horizontal band around the players (`FUN_000bc330`: min and max player x;
  if they are less than 20 units apart the band becomes 30 wide; then 10 more on each side) and spawns one Actor for it with its default animation.
- Actors are removed again by the same manager (`FUN_000bc630`, `FUN_000bc850`).

So the number of people alive is bounded by spawn rate and by the object pool below, and they exist only where the
monsters are.

### Army units and weapons

- `AttackObject` (388 bytes, derives from `RSimpleObject`) spawned by the AI manager from `AIDef` / `AIInstance`
  entries with `MinDelay`, `RespawnDelay`, `OffsetDelay`, `MaxOccurances` (`FUN_000afc50`, spawn loop in
  `FUN_000b0450`).
- Their models are `SimpleTAShape` (an `LWShape` + node tree + animation, used by `WWeaponCluster`):
  **max 20 nodes and max 20 meshes** (`FUN_00230280`, error strings "...too many for SimpleTAShape. (max is %i)",
  0x14). Rigid pieces, drawn like the lightweight shapes above, posed by the version-counter tree of section 2.
- Helicopters are simple objects on a `HelicopterMover`.

### Traffic (`FUN_000b0130`, `FUN_000b0450`, `FUN_000b0d60`)

- Level data `TrafficDef` / `VehicleDef` with `MaxVehicles` (default **7**; the code subtracts 2 per extra lane
  set), `MaxCount`, `Frequency`, `MinReleaseTime`.
- A vehicle is an `RSimpleObject` (272 bytes) with a `LineMover`: it drives from x = -75 to x = +75 (or the
  reverse) at 15 units/s on a lane at y = lane x 41 - 7.5 (or - 12.5), then is removed (`FUN_000b11d0`).
  Release timers are random 2..5 s.
- Draw is `ROSimple::vf5` (`0x001d83a0`): set world matrix, draw the shape. Rigid, no animation.

### Culling and rates for all of these

- Every mesh section does the three-state sphere test before drawing (section 1). Whole skinned instances can also
  be sphere-tested first (`FUN_002029d0` in `FUN_002132a0` when the caller passes cull = 1).
- Each render object reports a centre and radius (`RenderObject::vf2`); the world renderer's use of it was not
  traced here.
- No distance-based level of detail, no reduced animation rate and no pose sharing between instances were found.
  The saving comes from *not creating* people (records + timed spawn), from sprites, and from the skip logic in the
  rigid trees.

---

## 4. Cloth (`ClothRenderer`, `3space\cloth.cpp`, `xbox_mesh_renderer.cpp`)

- Set-up `FUN_00262e80`: creates a **dynamic** vertex buffer (usage flag 0x200) per cloth section per instance and
  copies the mesh's static vertices into it once.
- Every draw, `ClothRenderer::vf7` (`0x002630a0`) first calls `FUN_00263390`: lock, and for each vertex write the
  position (3 floats at offset 0) from the particle array (88 bytes per particle; position at
  `+0x24 + currentBuffer*12`, i.e. two position buffers that swap) and, if the format has a normal, the normal from
  particle+0x48; unlock. UVs and everything else stay from the initial copy. Then stream, indices, per pass: state
  and one draw. World matrix is identity (particles are in world space).
- Simulation `FUN_002199d0`, called from the monster update each frame: collision shapes follow their bones; pinned
  vertices are CPU-skinned to their bones (`FUN_0021a7c0`, up to a few weighted bones each); then each section is
  stepped in sub-steps no longer than the section's max step (`section+0x34`): `while (remaining > maxStep)
  step(maxStep)`, then one last partial step if more than 0.0001 s remains (`FUN_0021aa60`). If a pin moves faster
  than a threshold the cloth is reset instead of simulated (teleport guard).
- Cloth instances are sphere-culled before drawing (`FUN_0021c6e0`).
- Also used for building flags/awnings (`ClothObject`, "Cloth Objects must be attached to buildings.").

---

## 5. Shadows

No shadow volumes or render-to-texture shadows are used for characters in the code traced; it is all flat quads.

### Monsters (`ROMonster::vf4`, `0x00083f40`)

- Ground blob `FUN_00083fb0`: one textured quad (4 vertices, triangle strip, immediate mode), half-extents 1.2 x and
  0.8 x the monster radius, placed on the ground or roof under the monster with a 0.01 bias; alpha fades in and out over
  0.4 s and with height (`FUN_000849e0`).
- Wall shadow `FUN_00084210`: when the monster is on or next to a building, up to two more quads on the building's
  front and side faces, clipped to the building's box by adjusting the texture coordinates.
- Cost: 1 to 3 quads and one material per monster.

### Everything else (`RObjectShadowMgr`, `FUN_0002f5a0`, called from `RWorldRenderer::vf4` `0x000a6d00`)

- An object opts in with a `ShadowData` block: `TextureName`, `MaxHeight`, `FullHeight`, `Intensity`,
  `ExProjHeight`, `ShadowRect` (default -1,-1 .. 1,1), parsed once by `FUN_0002f120`.
- At most **4 different shadow textures** per frame (4 material slots). Objects are bucketed by texture.
- Per object: the quad is the shadow rect along the object's two horizontal axes, pushed along the light direction
  in proportion to height; alpha = intensity x global intensity x height fade; objects above `MaxHeight` or with
  zero alpha are dropped.
- **One draw per bucket**: a triangle list with 6 vertices per object (position, colour, uv) at ground height. The
  per-bucket scratch buffer holds 40 objects.
- Total: at most 4 draw calls for every car, tank and person shadow in view.

### Building shadows on the ground (`RShadowMgr`, `FUN_0004b750`)

A fixed grid over the street (about 22 x 5 cells) whose vertex colours are computed by ray tests against the
buildings along the light direction (`FUN_0004b370`, up to 4 buildings per cell), written into a vertex buffer
(`FUN_0004c050`) only when the building set changes, and drawn as one 296-triangle strip.

`ShadowModifier`, `ProjTextureModifier`, `PairedProjTextureModifier` and `BogusRenderShadowData` exist as classes;
their use for characters was not found (uncertain - they look like material modifiers for projected-texture
effects, `fx\fx_projected_tex.cpp`).

---

## 6. Budgets found in code

| Limit | Value | Where |
|---|---|---|
| Bones per skinned section | 48 (144 constants) | `FUN_00262020`, `0x00262090`, table `0x005ed780` |
| Weights per vertex | 2 or 4 | shader tables `0x005cc950` / `0x005d84b8` |
| Monster tree nodes | 68 | `FixedXFormTreeInst<68>` |
| `SimpleTAShape` (army/weapons) | 20 nodes, 20 meshes | `FUN_00230280` |
| Vertex shader program store | 8 slots x 17 = 136 instructions, shaders stay resident | `FUN_000eb1e0` |
| Shader variants | 80 each for rigid / 2-bone / 4-bone | `FUN_000e5500` |
| World object pools (`RWorld`, table `0x004c3f5c`) | 128 objects up to 256 bytes; 128 up to 512 bytes; 16 up to 2176; 4 up to 51072 | `FUN_001c9b80` |
| - which means | at most 4 monsters (51,048 bytes each), 16 buildings (2,060), and 128 in total of actors (260), vehicles (272), attack objects (388), projectiles, weapons, cloth and water objects | type registrations `FUN_001db330(size, ..., name, base)` |
| Traffic | `MaxVehicles` default 7 | `FUN_000b0130` |
| Occupant spawn | one per 0.75 s; 300 window slots scanned per building | `FUN_000bbd10`, `FUN_000bac60` |
| Object shadows | 4 textures, 40 objects per texture batch | `FUN_0002f5a0` |
| Clip cross-fade | 0.15 s | `FUN_0005acd0` |

Debug counters: shader cache hits/loads (device+0x16a8 / +0x16ac), constant uploads (+0x16b4), and per-draw
totals at +0xd8 (vertices or indices submitted), +0xdc (a second count that depends on primitive type) and +0xe0
(draw calls) - incremented by `FUN_000eddf0`, `FUN_000ede20`, `FUN_000ede50`.

---

## What the port should copy

The port today: an untested 26-bone, always-four-weights, 87-instruction skinning program with float bone indices
and 80-byte vertices; people are rigid pieces with one draw and one matrix each.

### Skinning

1. **Shrink the vertex.** Position float3, indices as 16-bit integers, weights float2 or float4, normal float3,
   uv float2: 44 bytes (two-bone) / 56 bytes (four-bone) instead of 80. On NV2A the SHORT2/SHORT4 attribute formats
   are native (D3DVSDT 0x25 / 0x45).
2. **Bake the constant slot into the index.** Store `slotOfBone` (the NV2A constant index of the bone's first row,
   minus whatever base the program adds) at build time. The program is then `ARL` + three `MUL`/`MAD` per
   influence, nothing else.
3. **Two programs, chosen per section.** Split by "max influences in this section": <= 2 -> the 18 (unlit) / 44
   (lit) instruction program, otherwise 26 / 52. Against 87 instructions that is 50-60% of the instructions when
   lit and 20-30% when unlit (vertex-program time on NV2A scales roughly with instruction count - a rule of thumb,
   to be confirmed by measurement on the console). Count influences in the PS2 data first:
   if most monster vertices have one or two bones, most sections take the short path.
4. **World-space palette, shared view-projection.** Put the model matrix into the bone matrices on the CPU; the
   program does skin -> world, then one 4-row view-projection, then fog and the screen scale/offset. The
   view-projection is uploaded once per frame, not per character, and lights stay in world space for every
   skinned draw.
5. **Three rows per bone, 48 bones per section, palette local to the section.** Use the same register map so the
   ordinary constants never move: palette in slots 0..56, 60..95 and 141..191; 57..59 and 96..140 left alone.
   Split meshes offline into sections by bone set (the port's 26-bone limit can rise to 48, which means fewer
   sections and fewer draws). Before each section, push only that section's rows - one contiguous constant upload
   per range rather than one call per bone is an improvement over the prototype and trivial with a push buffer.
6. **Sections with a single bone are rigid draws** with the ordinary rigid program and the bone matrix as the model
   matrix.
7. **Indexed triangle strips** for skinned sections.
8. **Keep programs resident.** Load the handful of programs in use into the 136-slot store once (8 slots of 17 in
   the prototype) and switch with the program start register; do not re-upload a program when the material
   changes. Track which constants are still valid and skip re-sending them.
9. **Sphere-test each section** against the frustum with three results (out / fully in / straddling) and only
   enable clipping work for the straddling case.

### Animation

10. **Per-track cursors.** Keep, per playing clip and track, the current key index, the two decoded keys and
    `1/(t1 - t0)`; advance forward from the cached index. Precompute the slerp arc when the key pair changes so a
    frame costs one sin/cos pair and a normalise per rotating bone.
11. **Only iterate tracks that have keys**; constant and rest-pose bones are copied once.
12. **Cross-fade from a snapshot** (0.15 s) on clip change rather than sampling two clips; sample two clips only
    for bones under a partial-body overlay.
13. **Rigid trees: cache the sample time and use version counters** so an idle tank turret or a parked prop costs
    no matrix work.
14. Compose parent-first in a flat array of 3x4 matrices; let procedural bones overwrite their world matrix in the
    same pass.

### People (the port's biggest draw-count problem)

15. The prototype never draws a person as many small draws in bulk. Its two answers are: **a sprite** (one quad,
    one texture swap per frame change, mirrored by flipping uv) for people in windows and at distance, and **not
    instantiating** people at all until needed (28-byte records, one spawn per 0.75 s, only near the players).
    Apply both to the port: keep people as records, cap live ones, and give far or small people a single quad.
16. For people that must stay 3D, stop paying one draw per piece. The prototype's own machinery suggests the fix:
    treat the person as **one skinned section whose vertices each reference one bone** (weight 1, second weight 0)
    and draw it with the two-bone program - one draw, about a dozen matrices in the palette (36 constants), instead
    of a dozen draws each with a matrix and state change. This is a recommendation derived from the monster path,
    not something the prototype does for people (its 3D actors are still one draw per piece, with the matrix
    reused for consecutive pieces on the same node - copy at least that).
17. Sort a person's pieces by node so consecutive pieces share a matrix upload, and by material so state is not
    re-sent.

### Vehicles and army

18. Rigid pieces are fine here (the prototype does the same, capped at 20 pieces and 20 nodes per unit). Use the
    version-counter tree so unmoving parts cost nothing, and cap counts the way the prototype does (7 vehicles,
    128 small objects in total).

### Shadows

19. **Batch all object shadows by texture**: at most 4 draws per frame, 6 vertices per object, alpha from height.
    Do not issue one draw per shadow.
20. Monsters: one ground quad plus up to two wall quads clipped by texture coordinates. No geometry projection.
21. Building shadows on the ground: a vertex-coloured grid updated only when a building changes.

### Cloth

22. One dynamic vertex buffer per cloth piece; rewrite position and normal only, leave uv in place; fixed
    sub-steps; cull before simulating if the port can afford the visual pop (the prototype simulates regardless
    and culls only the draw).

### Memory note (64 MB)

The prototype's choices are also memory choices: 44/56-byte vertices instead of 80, pre-baked buffers used in
place, 28-byte occupant records instead of objects, and four fixed object pools (about 0.34 MB) rather than a
general heap for world objects.

---

## Not traced / uncertain

- Real per-monster numbers (sections, bones per section, vertices): data, not code.
- How the world renderer uses each render object's sphere (whole-object culling, sorting).
- `RawAnim` sampling details; the exact quantisation scale of position keys (`FUN_00275f70`).
- The dynamic-cast target that separates `BundledSkinRenderer` from `StdMeshRenderer` in `FUN_00212930` is assumed
  to be `BundledSkinSectionWrapper`.
- Whether the final PS2 game's street people correspond to this build's `Actor` sprites/shapes; this build has no
  crowd system to compare against.
- `ShadowModifier` / `ProjTextureModifier` usage.
