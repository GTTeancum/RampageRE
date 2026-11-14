# Effects, HUD, text and shadows in the Xbox prototype

Everything drawn that is not a mesh, plus the particle simulation, as found in the debug build
`RampageD.xbe` (addresses are virtual addresses in that file). Evidence is the Ghidra decompile queried with
`tools/re_query.py`; data tables were read straight out of the image. Things not traced to the end are marked
**uncertain**.

## Summary

1. All non-mesh drawing goes through one call: `GraphicsDevice::vf66` (0xeb8d0), which is `DrawVerticesUP` from a
   CPU array. There are no dynamic vertex buffers, no `Begin/End`, no `BeginPush` anywhere in the fx/GUI code.
2. Hard pools: 2,048 particles, 256 stretch particles, 128 mesh particles, 256 emitters, 256 particle systems,
   512 other effects (0xf55d0). The port's 8,192-quad budget is four times what the original allowed.
3. A cost/benefit limiter (0x123be0) refuses new particles and emitters by screen cost (cost / distance to the
   nearest camera), with a total budget of 450 and probabilistic refusal above 270.
4. Small particles ("UsesTinyParticles") are real GPU point sprites: 20-byte vertex (centre, colour, size), 128
   per draw, expanded by a 12-instruction vertex shader (file offset 0x5d4458).
5. Normal billboards are a static unit quad plus a per-particle world matrix built on the CPU; the debug build
   issues one 4-vertex draw per particle. That part is a design to avoid, not to copy.
6. Curves ("graphs") are objects classified at load; constant, linear and quadratic ones are evaluated inline
   (0x101f40). Sine uses a 128-entry table, rotation uses per-octant polynomials, 1/sqrt uses a 128-entry table.
   No libm call in the per-particle path except `floor` for tiled textures.
7. Each emitter is classified once at load to a specialised advance and render function by its feature flags
   (0x1251a0, 0x1322c0); the debug build only registers the generic "Flexible" variants.
8. HUD and text: 24-byte vertices (position, packed colour, uv), not pre-transformed, written into one 16 KB
   static buffer and drawn as quad lists of at most 64 quads; one draw per text line, one per GUI quad.
9. Shadows are flat geometry on the ground: all object shadows together cost at most 4 draws (one per shadow
   texture); the building ground shadow is one static 298-vertex grid whose colours change only when a building
   changes height.
10. Effect dynamic lights and projected textures are stubbed out in this game (they do nothing). No full-screen
    pass and no render-to-texture was found in the effects path.

## Common plumbing

| Thing | Where | What it is |
|---|---|---|
| `GraphicsDevice::vf66(prim, primCount, fvf, ptr, stride)` | 0xeb8d0 | Picks the vertex shader of the current material pass (0xeb1e0), then `DrawVerticesUP` (0xedee0), then bumps counters. The `fvf` argument is not used for the draw. |
| `GraphicsDevice::vf65(prim, start, primCount)` | 0xeb7a0 | Same, from a vertex buffer (`DrawVertices`). |
| Engine primitive numbers | table 0x4d0ad8 | 0 points, 1 lines, 2 line strip, 3 triangle list, 4 triangle strip, 5 triangle fan, 6 quad list. |
| `GraphicsDevice::vf29(0, matrix, slot)` / `vf27(slot)` / `vf26()` | 0xe7f00 / 0xe7e90 / 0xe7db0 | Set world matrix into one of 32 slots / select the current slot / returns 32. Changing either marks the transform shader constants dirty; they are re-uploaded at the next draw (0xe9620). |
| `vf46(i, bool)` / `vf44(i, value)` / `vf48(i, value)` | 0xe8ca0 / 0xe8b90 / 0xe8d90 | Cached render-state setters. Bool 0 = z test, 1 = z write, 9 = alpha test. Int 0 = cull mode, 1 = fog mode, 7 = z func, 8 = alpha func. Byte 6 = alpha ref. |
| `PrimDrawer` (render\xbox\xb_prim_drawer.cpp) | begin 0xf0020, vertex 0x8c110, uv 0x8c520, colour 0x8c330, end 0xf0130 | Immediate-mode helper. Writes dwords into a static buffer at 0x5fed20 (16,384 bytes, up to 0x602d20) and `end()` calls `vf66`. `begin()` asserts at most 0x40 primitives. |
| Vertex shader program cache | 0xeb1e0 | Up to 8 shaders kept loaded in the 136 program slots (17 slots each), switched with select instead of reload. Counters at device +0x16a8 (hits) and +0x16ac (loads). |

Render states used below, decoded from the XDK numbering in this build: 0x39 z func, 0x3a alpha func, 0x3b alpha
blend enable, 0x3c alpha test enable, 0x3d alpha ref, 0x3e src blend, 0x3f dest blend, 0x40 z write, 0x5c fog
enable, 0x74 point size, 0x75 point size min, 0x76 point sprite enable, 0x77 point scale enable, 0x78/0x79/0x7a
point scale A/B/C, 0x7b point size max, 0x8f z enable, 0x93 cull mode.

## 1. Particle rendering

### Dispatch

- `ParticleSystemFx::render` (vf16, 0x11d7b0) walks the system's emitters in list order and calls
  `Emitter::render` (0x11f4e0), which calls the member function pointer stored in the emitter definition at
  +0x180.
- That pointer is chosen once per definition by 0x1322c0 (fx_particle_classify_render.cpp): it builds a
  31-field feature record from the definition (0x132490) and takes the first match in a table at 0x63ab60
  (0x90 bytes per entry, count at 0x5e79fc = 3). The table is filled by a static initialiser at 0x2a4ae0:
  - 0x132c60: empty function, for "NoRender" emitters or emitters with nothing to draw;
  - 0x133120: generic renderer, registered for particle type 2 (XForm);
  - 0x136910: the same generic renderer, registered for every other type.
- Both big functions are the same source (fx_particle_render.cpp, "Emitter::renderNormal--templated version")
  compiled with every template argument set to "Flexible", i.e. decided at run time. The template arguments
  are printed by a debug dump at the top of the function and name the specialisation axes: all_em_normalized,
  render_stretch, render_tiny, rotates, streams, no_render, test_z_buffer, dont_billboard, stationary,
  const_texture_rot, stretch_crosshatch, tex_coords_tile, all_pa_normalized, render_datapoints, additive_alpha,
  tiles, emitter_rel, write_z_buffer, use_world_mat, const_texture_rad, const_texture_center,
  stretch_alternate_align, tex_coords_stream, cycle_within_start_area, multi_render, forward_axis,
  em_dataset_type, blend_vertex_color, blend_vertex_alpha.
- **Uncertain:** whether the release build (`Rampage.xbe`, not decompiled) registers more specialised
  instances. The mechanism for it exists; the debug build does not use it.

### Common setup per emitter (0x133120)

- Early outs: "NoRender" flag; optional frustum test of the emitter bounds (0x20fe50).
- Alpha ref is read, set to 4 for the emitter, and restored at the end (state 0x3d).
- Material: 0x63a858 (normal alpha) or 0x63a854 (additive) depending on the "AdditiveAlpha" flag. They are
  created at 0x1192d0 from material presets 0x55 and 0x57 (both declared with format 0x142; blend mode 1 and 2).
  Additive emitters also switch fog off for the draw (`vf44(1, 0xb)`) and restore it.
- z test = "TestZBuffer" flag, z write = "WriteZBuffer" flag, cull mode none (0x13afa0 / 0x13b180, restored by
  0x13b050).
- Texture: one per animation frame; `SetTexture` only when the particle's frame byte (+0x28) differs from the
  previous particle's (0x13b130). That frame byte is the only batch key.
- No sorting anywhere: particles are drawn in linked-list order (emitter +0x10, next at particle +0x48),
  emitters in list order. No back-to-front sort was found.
- "multi_render" (definition +0xec, EDF key `int.multiple_render`): every draw is simply issued N times.

### Path A: point sprites ("UsesTinyParticles", flag 0x80) — the cheap path

- Vertex, 20 bytes: `float x, y, z; D3DCOLOR colour; float size`. Declaration built at 0x11bd10: stream 0,
  register 0 = FLOAT3, register 1 = D3DCOLOR, register 2 = FLOAT1.
- Written into a static buffer at 0x639e50 of 0xa00 = 2,560 bytes, so 128 sprites per draw (0x13ade0, 0x13add0).
  Flushed with `DrawVerticesUP(POINTLIST, n, buffer, 20)` when full or when the texture frame changes.
- Per particle on the CPU: position copy (or one matrix transform if emitter-relative), colour (see section 2),
  `size = 640.0 * sizeGraph(age) * particleScale`.
- States: point sprite enable 1, point scale enable 1, point size max 64.0, point size min 2.0, scale A = 0,
  B = 0, C = 1.0; alpha blend on, src = SRCALPHA, dest = INVSRCALPHA or ONE (additive); alpha test on with
  func GREATEREQUAL. The texture is bound to stage 3 (the stage the NV2A uses for point sprites).
- Vertex shader (file offset 0x5d4458, 12 instructions): transforms the centre by the 4 rows at c96..c99,
  passes the colour, computes fog from c100..c102, and writes
  `oPts = size * 1/sqrt(|pos - c104|^2)`, i.e. size divided by distance to the eye. The GPU does the expansion.

```
0: ADD r0.xyz <- v0, -c[104]        4: DPH oPos.w <- v0, c[99]       8-9: DPH oFog.y/z
1-3: DPH oPos.xyz <- v0, c[96..98]   5: DP3 r0.x <- r0,r0 ; MOV oD0 <- v1   10: MUL oPts <- r1.y, v2.x
                                     6: MUL oPos.xyz <- r12, c[58] ; RCC r1.x <- r12.w
                                     7: DPH oFog.x <- v0, c[100] ; RSQ r1.y <- r0.x   11: MAD oPos.xyz <- r12, r1.x, c[59]
```

### Path B: normal billboards (the default)

- Vertex, 24 bytes: `float x, y, z; D3DCOLOR colour; float u, v` (format 0x142).
- The vertices are a **static unit quad** at 0x5e72d8: corners (±0.5, ±0.5, 0), colour white, uv ±0.5. It is
  never rebuilt; only the colour and uv fields are patched per particle.
- Per particle the CPU builds a 3x4 world matrix: camera right and up axes (fetched once per emitter), rotated
  by the particle's angle and scaled by its size, plus the particle position as translation. It is stored in
  world-matrix slot i of the device (`vf29(0, m, i)`), colour goes to a stack array, uv centre and uv radius to
  two more stack arrays (all `alloca`, 32 entries).
- Particles whose size cubed is under 0.001 are skipped.
- When 32 are queued (`vf26()` = 32), or the texture frame changes, 0x13a730 flushes: for each queued particle
  it selects world slot i (`vf27`), patches colour and uv into the static quad, and calls
  `vf66(fan, 2 triangles, …, 0x5e72d8, 24)`. That is **one DrawVerticesUP of 4 vertices per particle**, each
  preceded by a transform-constant upload.
- So the shader does the transform but not the billboarding: the CPU supplies the axes through the matrix.
- "StretchCrosshatchRendering" uses a second static array at 0x5e7338: 16 vertices, three perpendicular quads
  joined by degenerate vertices, drawn as one 14-triangle strip.
- Texture coordinates: either constant, or a sprite-sheet cell (`cell = startTile + floor(tileAdvance(age))`,
  centre = (column + 0.5) / columns, row likewise), or centre/radius graphs.
- **Uncertain:** which of the 240 compiled vertex shaders the two particle materials resolve to. The draw takes
  it from the material pass table (0x5c5498 indexed by a byte in the pass record); that chain was not followed.
  It is a plain transform + colour + one uv shader by its inputs.

### Path C: stretch particles, strings and lightning (particle type 1, or flag 0x4000)

- Four corners computed on the CPU into a static 4-vertex array at 0x5e74b8 (0x119d30, 0x11a3b0, 0x11a6d0),
  same 24-byte format, world matrix = identity, one fan draw per particle.

### Path D: mesh ("Shape") particles

- Per particle: set world matrix, call the mesh's own draw (definition +0x124). Pool of 128.

## 2. Particle simulation

### Structures and pools (0xf55d0, stats printer 0xf7210)

| Pool | Count | Element size | Bytes |
|---|---|---|---|
| Particles | 2,048 | 0x4c (76) | 155,648 |
| Stretch particles | 256 | 0x5c | 23,552 |
| Shape (mesh) particles | 128 | 0x6c | 13,824 |
| Emitters | 256 | 0xa0 | 40,960 |
| Particle systems | 256 | 0xc0 | 49,152 |
| Other effects (shared block allocator) | 512 | 0x70 | 57,344 |
| Graph constants | 13,200 | 8 | 105,600 |

All are free lists carved once at start-up; nothing is allocated per frame. Particle layout (from use):
+0x00 position, +0x0c velocity, +0x18 age, +0x1c lifetime, +0x20 1/lifetime, +0x24 size scale, +0x28 texture
frame (byte), +0x29 mesh index (byte), +0x2a start rotation (byte, 1/32 of an octant-turn unit), +0x2c start
tile (u16), +0x30 spin rate, +0x34 r,g,b, +0x40 alpha, +0x44 current limiter cost, +0x48 next.

### Update rate

- Every frame, variable step: `dt = world.frameTime * world.timeScale` (0xfd2e0, fields +0x68 and +0x14). No
  fixed step, no sub-stepping. An assert rejects emitter cycles shorter than one frame (0x125c30).
- `ParticleSystemFx::advance` (vf13, 0x11cf40) calls `Emitter::advance` (0x11f300) for each emitter through the
  member pointer at definition +0x190, chosen at load by 0x1251a0 (fx_particle_classify_advance.cpp) from a
  3-entry table at 0x63a8c8. The three registered variants are 0x1259c0, 0x125a90, 0x125b60; each is
  spawn (0x125f20 / 0x128000 / 0x12a130) followed by the per-particle loop (0x1275f0 / 0x1296d0 / 0x12b840).

### Per-particle work each frame (0x1275f0)

1. `age += dt`; kill if `age > lifetime`.
2. Move (0x12d300): `pos += vel * dt`, each axis optionally scaled by a velocity-multiplier graph (only when the
   definition has one; three null checks, hoisted out of the loop as one flag).
3. Collision (0x12d890): by default a single compare against the ground plane; the adapter's triangle test
   (0xf8930) only if the definition asks for object/building collision. Responses are an enum (bounce, slide,
   delete, age…).
4. Gravity and air resistance (0x12df30, 0x12e070): one vector add; drag is `vel *= max(0, 1 - k*|vel|)` using
   the table 1/sqrt (0x181be0, 128-entry mantissa table), or a constant factor for "LinearAirResistance".
5. Optional orbit acceleration: two graph evaluations.
6. Limiter bookkeeping: `cost = min(defCost / distanceToCamera, 0.75)`; the difference from last frame is
   applied to the global total (0x123da0).
7. Dead particles are unlinked and returned to the free list in the same pass.

Normalised age (`age * invLifetime`) is computed at most once per particle per pass and cached in the loop
context (0x131ab0, 0x13b1f0).

### Graphs (fx\graphs.h)

- Classes: `GraphConstant`, `GraphLinear`, `GraphQuadratic`, `GraphCubic`, `GraphQuartic`, `GraphQuintic`,
  `GraphSine`, `GraphSineTable`, `GraphLinearInterpolation`, `GraphReciprocal`, base `GeneralGraph`. Each is a
  small object with a virtual `evaluate(t)` in slot 1.
- The hot evaluator 0x101f40 avoids the virtual call for the common cases by comparing the object's vtable
  pointer with three cached ones (0x639dec/0x639df0/0x639df4, set at 0x108760):
  constant → return `a`; linear → `a + b*t`; quadratic → `a + b*t + c*t*t`; anything else → virtual call.
- `GraphSineTable` (0x107fd0): 129-entry table at 0x639be0 built once (0x107f20), index by fraction of a
  period, linear interpolation between two entries.
- `GraphLinearInterpolation` (0x107220): keyframes, linear scan for the segment, lerp.
- So: per-particle evaluation, no baked per-emitter tables, but the usual curve costs one compare and one or
  two multiplies.
- Whether each graph's `t` is age in seconds or normalised age is a per-graph flag bit in the definition
  ("Normalized…" EDF flags), tested with 0x11e9b0.

### Per-particle work at render time

- Size: 1 graph.
- Colour (0x13a160): 3 graphs (r, g, b) only if the definition has colour graphs, else the stored particle
  colour; alpha: 1 graph or stored value; optional blend with the emitter colour (add, subtract, difference,
  multiply); clamp 0..1; pack to D3DCOLOR with four float-to-int conversions (0x303d0).
- Rotation: `startByte/32 + spin * age * 1.273 + rotationGraph`, in units of an eighth of a turn; integer part
  mod 8 picks an octant and 0x1883c0 evaluates fixed sine/cosine polynomials on the fraction. No `sin`/`cos`
  calls (the debug build calls them only inside asserts that check the polynomial).
- Texture: tile index needs one `floor`; centre/radius graphs are 2 + 2 evaluations when used.

### Limits (fx\fx_limiter.h, fx_limiter.cpp)

Two limiter objects are set up at 0xf55d0 with 0x123b20:

| Limiter | Guards | Budget | Soft start | Resource count | Low-resource bar |
|---|---|---|---|---|---|
| 1 | each new particle (0xf7470) | 450.0 | 0.6 × 450 = 270 | 2,048 | max 2.0 |
| 2 | each new emitter (0xf7900) | 999,999 (off) | — | 256 | max 20.0 |

Request test (0x123be0), with `cost` and `benefit` taken from the definition (emitter def +0x168/+0x16c for
particles, +0x9c/+0xa0 for emitters) and both divided by the distance to the nearest camera (0x1cebd0; minimum
over all active viewports, so split-screen is handled) unless the emitter uses tiny particles:

1. `total = currentTotal + cost`; if `total >= budget` → refuse.
2. If no free element → refuse.
3. If `total` is above the soft start: `need = (total - soft) * k * scale`; refuse if
   `benefit < random(0..1) * need` (k = 0.5 for particles, 5.0 for emitters). `scale` is meant to be
   `1 / (budget - soft)`; as decompiled, 0x123b20 computes it before it stores `soft`, so it may really be
   `1 / budget` (**uncertain**).
4. If the free count is at or below a threshold: `need = (1 - free/(0.7*pool)) * bar`; refuse if
   `benefit < random * need`. The threshold is stored by a float-to-int whose operand the decompile lost;
   70 % of the pool is inferred from the 0.7 parameter (**uncertain**).

The running total is kept as an integer (limiter +0x1c), the free count at +0x20 (0xfc3b0, 0x11ed80).

So load shedding is gradual and random: cheap, near, high-priority emissions keep being accepted while far,
expensive ones are dropped first. Emitters with `int.num_emitters_reserved` bypass the test. Refusals are
counted (manager +4 emitters, +8 particles) and printed by a debug line ("FxLimiter denying request…",
switch at 0x63a89c).

The definition also carries `enum.fxPriority` (High/Medium/Low, def +0x20), flags AdvanceOnlyWhenVisible /
DeleteWhenNotVisible / SupposedToBeLarge (def +0x24), `val.fxRadius` (+0x28) and `val.fxCullDist` (+0x2c)
(0x124030). **Uncertain:** where the cull distance and the visibility flags are enforced; the code that reads
them was not located. The cost formula behind `val.cost_fudge_factor`, `val.priority_fudge_factor` and
`val.cost_to_priority_multiple` was also not decoded.

### SSE

None in the decompiled fx code: everything is x87 and `__ftol2`. A search of the whole decompile for SSE
operations found nothing. **Uncertain** for the release build.

## 3. HUD, GUI and text

### Quads

- `OrthoQuad` holds four corners of 24 bytes each (x, y, z, u, v, colour) at +0x20, +0x38, +0x50, +0x68.
- Positions are ordinary model-space XYZ, **not** pre-transformed XYZRHW: the quad's transform is set as the
  world matrix (0x233b80 → `vf29`) and an orthographic camera is pushed for the draw (0x202900).
- A single quad (`OrthoQuadBase::render`, 0x233f10 → 0x233f70) is one `PrimDrawer` begin/end: a 2-triangle
  strip, 4 vertices, one draw per material pass. Format is chosen by what the quad has: position only (12
  bytes), + colour (16), + uv (20), + both (24).
- State around it (0x233b80, restored by 0x233e10): z test and z write from quad flags, optional alpha test
  with a per-quad alpha ref, optional z func ALWAYS, optional z bias.
- `GuiQuad::render` (0x158890) is exactly that: one draw per GUI quad, with the alpha temporarily scaled for
  fades.

### Batched quads

- `OrthoQuadDrawerP / PD / PT / PTD` (begin 0x234510…0x2349f0, add 0x234570…0x234a60) batch quads that share
  state: `begin(n)` opens a `PrimDrawer` quad list of `min(n, 64)` quads, `add(quad)` pushes its 4 corners, and
  when the count reaches zero it ends the draw and begins the next chunk for the remainder.
- So a batch is at most 64 quads = 256 vertices = 6,144 bytes per `DrawVerticesUP`, primitive type quad list
  (4 vertices per quad, no index buffer, no 6-vertex expansion).
- 0x234410 picks the drawer from the quad's texture and colour flags; `n = -1` selects the unbatched
  `OrthoQuadDrawerSingle`.

### Text

- `GuiText::render` (0x1459d0) → `OFont::drawText` (0x22c300) splits into lines and aligns; each line goes to
  `OFont::drawString` (0x22c790), preceded by a second call with an offset and the shadow colour if the text
  has a drop shadow.
- `drawString` sets the state once, begins one batch for the number of characters, and per character looks up
  a glyph record (u0, u1, v0, v1, pre-advance, width, post-advance; 0x22b6c0), fills the font's one reusable
  quad and adds it. Spaces add no quad. Optional per-vertex alpha for edge fades (0x22c200).
- Result: one draw per text line (two with a drop shadow), up to 64 glyphs each, all from one font texture.
- Fonts are texture atlases. `OFont` (render\OFont.cpp) loads a font file with its glyph table.
  `BitmappedFont2D` (3space\font_2d.cpp, 0x237e80) builds its atlas at load time by rasterising the glyphs of
  all registered strings with the XDK's XFONT (`XFONT_OpenBitmapFontFromMemory`, `XFONT_TextOut`), including a
  double-byte path; XFONT is not used at draw time.

## 4. Shadows

### Object shadows (code\game\RObjectShadowMgr.cpp, 0x2f5a0)

- Called once per frame from `RWorldRenderer::vf4` (0xa6d00) with the list of rendered objects.
- Each object type may have a "ShadowData" property set (loaded lazily at 0x2f120): TextureName, MaxHeight,
  FullHeight, Intensity, ExProjHeight, ShadowRect. At most 4 distinct shadow textures exist.
- `RRenderShadowData` is that loaded record. `BogusRenderShadowData` is one shared static instance (0x5f7ff8)
  assigned to objects that have no ShadowData, so the render loop can skip them with a pointer compare.
- Per object: `alpha = Intensity * globalIntensity * clamp((MaxHeight - height) / (MaxHeight - FullHeight))`;
  dropped if it rounds to 0. The four corners are the object's ground position, shifted along the sun
  direction by its height, plus its own X and Y axes times the ShadowRect extents; z is one global ground
  height. No ray casts, no ground geometry lookup, no stencil.
- Draw: objects are bucketed by shadow texture; each bucket is one `PrimDrawer` triangle list, 6 vertices of
  24 bytes per shadow (position, colour with the alpha, uv 0..1). The stack array holds 40 shadows and the
  drawer asserts at 64 triangles, so about 32 shadows per texture.
- Cost: at most 4 draws for every object shadow in the frame, about 30 multiplies per shadow.

### Building shadow on the ground (code\game\RShadowMgr.cpp)

- A fixed grid over the street: 22 × 5 cells of 5.0 units, origin (-64.5, -2.5). One vertex buffer of 298
  vertices, 16 bytes each (position, colour), laid out as 6 strips joined by degenerates (0x4b1d0, 0x4c050).
- At level start each cell casts rays along the sun direction and records up to 4 buildings that shade it
  (0x4b370). When a building's height changes (0x4ba40) the affected cell rectangle is recomputed and only the
  colour field of those vertices is rewritten under a lock (0x4bdd0). Nothing is touched on other frames.
- Draw ("RenderGroundShadow", 0x4b750, from `RSceneryMgr::vf5` 0x30f30): identity world matrix, z func
  ALWAYS, alpha test GREATER with ref 1, one `DrawVertices` triangle strip of 296 triangles.

### Shadows on buildings (code\game\BuildingShadow.cpp) — partly uncertain

- 0x98950 ("ShadowUpdate") recomputes, when a neighbour changes height, up to two shadow rectangles per
  building: the neighbouring building's box projected along the sun direction onto this building (0x973e0,
  0x96f30). `ShadowModifier` (a material modifier, 0x25f8e0 / 0x25f9b0) feeds them to the building's shader
  as constants.
- **Uncertain:** the exact shader maths, and how the "MonsterBlob" texture and the MShadowR/G/B,
  MonsterShadowIntensity and TShadowTint values loaded at 0xa5a70 are applied (monster shadow on building
  faces). That lives in the mesh material path and was not traced here.

## 5. Other effects

| Effect | Finding | Evidence |
|---|---|---|
| Dynamic light (`DynLightFx`) | Does nothing in this game. It asks the world adapter for a light handle every frame; the Rampage adapter returns 0, and the "update light" entry is an assert "Feature not yet implemented". Explosions do not light the world. | 0x123670; adapter 0x1cea80, 0x1ceac0 |
| Projected texture (`ProjectedTexFx`) | Same: the adapter's allocate entry returns 0. | 0x122800; adapter 0x1ceaf0 |
| Lens flare (`LensFlareFx`) | Source point tested against the view frustum only (no occlusion query, no z read-back). Brightness from the angle between view direction and source with a linear falloff. Then one textured quad per layer (format position + uv, 2-triangle strip), each with its own world matrix and material colour; z test and z write off. N layers = N draws. | 0x121a90, 0x121200 |
| Swoosh trail (`SwooshFx`, 0xe58 bytes each) | Ribbon built on the CPU into a stack array of 24-byte vertices as a triangle strip, flushed every 64 segments with `vf66`; the last two vertices are copied to the start of the next chunk. z write off, cull none. | 0xfe7d0 |
| Screen darken (`ScreenDarkenFx`) | The effect only advances a timer. The adapter sums all active ones into an amount, a weighted colour and a list of exempt objects (0xf8260, 0x1115f0). **Uncertain** how the game applies the result; no full-screen quad was found in the effect itself. | 0x111510 |
| Camera shake, sound | No drawing. | — |
| Full-screen passes, render-to-texture | None found in effects, GUI or shadows. `SetRenderTarget` is reachable only through `GraphicsDevice::vf68` (0xeba00), for which no caller was found; a back-buffer copy exists at 0xc57c0 for the shell (0xc8d60). | call graph |

## 6. Budgets and counters

| Constant | Value | Where |
|---|---|---|
| Particle pool | 2,048 | 0xf55d0 |
| Stretch / shape particle pools | 256 / 128 | 0xf55d0 |
| Emitter pool | 256 | 0xf55d0 |
| Particle-system / general effect pools | 256 / 512 | 0xf7210, 0xf5ad0 |
| Limiter total cost budget / soft start | 450 / 270 | 0xf55d0 |
| Per-particle cost clamp | 0.75 | 0x1275f0 |
| Point sprites per draw | 128 (2,560-byte buffer) | 0x13add0 |
| Point size clamp | 2 to 64 pixels | 0x133120 |
| Billboard matrix batch | 32 | 0xe7db0 |
| Billboard minimum size | size³ ≥ 0.001 | 0x133120 |
| Particle alpha ref | 4 | 0x133120 |
| `PrimDrawer` buffer / primitives per draw | 16,384 bytes / 64 | 0x5fed20, 0xf0020 |
| Quads per HUD batch | 64 | 0x234510 |
| Object shadows per texture / textures | about 32 / 4 | 0x2f5a0 |
| Ground shadow grid | 22 × 5 cells, 298 vertices | 0x4c050 |
| Swoosh segments per draw | 64 | 0xfe7d0 |

Debug counters: device +0xe0 draw calls, +0xd8 and +0xdc primitive and vertex totals (0xede50, 0xeddf0,
0xede20); device +0x16a8 / +0x16ac shader cache hits / loads, +0x16b4 constant uploads; fx manager +4 / +8
refused emitters / particles; allocation totals printed by "Fx creation denied…" (0xf6060); pool report
"Are %i free particles (of %i)" (0xf7210); per-system report "Total of %i emitters and %i particles"
(0x11e560).

## What the port should copy

1. **Cut the particle budget to the original's.** 2,048 live particles in total (plus 256 stretch, 128 mesh),
   256 emitters. The port builds up to 8,192 quads; the game was authored against a quarter of that.
2. **Add the limiter.** Keep a running total of `min(defCost / distance, 0.75)` over live particles; refuse
   new particles outright at 450, and probabilistically between 270 and 450 and when less than 70 % of the
   pool is free, comparing against the definition's benefit. This is what kept the original's worst frames
   bounded, and it degrades far and expensive effects first.
3. **Draw "UsesTinyParticles" emitters as point sprites.** 20-byte vertex (centre, packed colour, size),
   size = 640 × world size, divided by eye distance in the vertex shader, clamped 2..64 pixels, texture on
   stage 3, 128 per draw. The CPU writes 5 words per particle instead of 4 vertices.
4. **Use 24-byte vertices with a packed 32-bit colour** for every quad (particles, HUD, shadows, trails):
   3 floats, one colour word, 2 floats. Drop float colours.
5. **Submit quads as a quad list** (4 vertices per quad, no indices, no 6-vertex expansion), straight from the
   array they were built in. The original never copies vertices to a second staging buffer; do the same —
   build in place, one submit.
6. **Do not copy the one-draw-per-particle billboard path.** Keep the port's batching by texture/state run.
   The pieces worth taking from that path are the state keys: batch breaks only on texture frame change;
   z test, z write and additive blend come from three emitter flags; alpha ref 4; fog off for additive.
7. **Classify curves at load.** Tag each curve as constant, linear, quadratic or general and evaluate the
   first three inline. Replace `sin`/`cos` for particle rotation with the octant polynomial (or a table),
   replace 1/sqrt in drag with a table or the hardware reciprocal square root, and compute normalised age
   once per particle per frame. Only tiled textures need a `floor`.
8. **Classify emitters at load** into a few specialised advance and render loops by feature flags (colour
   graphs or not, rotation or not, tiles or not, collision or not, emitter-relative or not), so the
   per-particle loop has no feature branches. The original's table-of-functions design at 0x1251a0 / 0x1322c0
   is the model.
9. **Skip work the original skips:** particles with size³ under 0.001 are not drawn; velocity graphs are
   tested once per emitter, not per particle; collision is a single ground-plane compare unless the
   definition asks for more.
10. **HUD:** one state setup and one draw per text line (two with drop shadow), at most 64 glyph quads per
    draw, from one static scratch buffer of 16 KB. One draw per GUI quad is what the original does too, so the
    gain there is only in removing the staged copy and the index expansion.
11. **Object shadows:** replace the three staged passes with the original's scheme: one flat textured quad on
    the ground per object, alpha faded by height, all objects sharing a shadow texture in one draw — at most 4
    draws per frame in total.
12. **Building ground shadow:** one static 298-vertex grid; rewrite vertex colours only for the cells affected
    when a building changes height; one draw per frame.
13. **Effect lights and projected textures can be left out:** in the Xbox prototype they are stubs and cost
    nothing. Lens flares are a frustum test and one quad per layer, with no occlusion read-back.
14. **Memory (64 MB console):** the whole effects system is about 450 KB of fixed pools plus a 16 KB quad
    scratch buffer and a 2.5 KB point-sprite buffer; no per-frame vertex buffers are allocated.
