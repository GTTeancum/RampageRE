# Xbox: plan for 30 fps and above

Status (2026-11-14): the prototype has been reverse-engineered (docs/xbox_proto/); the list below and a second
batch (memory, 1280x720 and widescreen, Xbox button pictures) and a third (the busiest frames, more memory) are
written and have run in xemu with four computer-controlled monsters. Results are under "Measured in xemu" and at
the end of "Third batch". Nothing has run on a
console yet, and xemu's timing is not a console's. Targets: stock 64 MB console, four monsters in a city; 640x480
4:3 never under 30 frames a second (33 ms a frame, worst frame included) and faster whenever the scene allows, not
capped or locked at 30; 640x480 on a 16:9 screen at 60; 1280x720 at 30.

## Working rule

Assess, write the whole batch, then test once. No emulator run after each change; compile checks only until
every item below is written. There are no phases: one batch of rewrites, one test pass. Test runs are four computer-controlled players in xemu, muted, sampled every
10 seconds. The real console is used only when its owner says it is free.

## Where the time goes now

Measurements that exist:

| Build | Where | Result |
|---|---|---|
| `main` (frozen XBE's source) | xemu, four players, Las Vegas | 14 fps, 49 MB of 64 used; CPU samples: about 40 % in pbkit's write-combine flush, about 15 % in `skin_item` |
| `xbox-devkit-fixes` (batching, CPU skinning) | real console (its commit notes) | 19-20 fps |
| `main` + runtime DXT1 encoding | xemu, same run | 9.8 fps, worst frame 3.4 s, 0.6 MB saved: dropped |

A frame is about 26,000 triangles (35,000 at most). At 30 fps that is under a million a second, far below what
the NV2A draws. The frame is CPU-bound: the cost is in preparing and submitting draws, posing, and game logic.
Everything below is estimated from the code (no profile of the branch exists); the estimates add up to 19-32 ms
of the roughly 50 ms frame.

## What the prototype XBE does (Oct 2005 Pipeworks build)

- 240 compiled vertex shaders in `Rampage.xbe` (file offsets 0x25f984-0x281778): the whole renderer is vertex
  programs over static vertex buffers, no CPU transform.
- Skinning on the GPU with an indexed matrix palette, three rows a bone (`ARL a0.x, v1.x` /
  `MUL r, v2.x, c[a0.x+96..98]`): inputs v0 position, v1 bone indices already scaled, v2 weights, v3 normal,
  v6 texture coordinates. Two-bone and four-bone variants; the unlit two-bone program is 18 instructions, the
  lit one 44. Ours is 87 and always blends four.
- Screen transform in the program (`MUL oPos, r12, c[58]`, `RCC`, `MAD oPos, ..., c[59]`), fog from a plane.
- Textures are DXT1/DXT3 with full mip chains, stored in the GPU's layout.

## The prototype is the reference

`Rampage.xbe`, `RampageRD.xbe` and `RampageD.xbe` are the same engine running natively on the Xbox, written by
its developers for this hardware. Each item below starts from how the prototype does it, and the port copies that
design; an item is designed from scratch only where the prototype has no answer. So far only its skinning shaders
and texture formats have been read. To read the rest:

- The three builds are linked against XDK 5849, and that XDK's libraries and symbols are installed on this PC
  (`C:\XDK\Xbox\Lib`, `C:\XDK\Xbox\symbols`): Direct3D, DirectSound and the C library inside the XBE are
  labelled by matching against them, which leaves the engine's own code.
- `RampageD.xbe` is unoptimised and keeps its asserts with source names (`render\xbox\xb_device.h`,
  `xb_prim_drawer.h`, `fx\fx_particle_render.cpp`, `shared\xbox\xb_file_cache.h`, `xb_background_writer.h`,
  `sound\soundfile.h`); the release build has the same code optimised.
- The engine is the PS2 game's, which this repository has already mapped function by function (COVERAGE.md), so
  its game-side code is recognised by shape.

What it is asked, and where the answer goes:

| Question to the prototype | Item |
|---|---|
| How world geometry reaches the GPU: vertex and index buffers, the 240 vertex shaders by material, state sorting, culling | 1-3 |
| How a damaged or moving building part is drawn | 2 |
| How people, army units and vehicles are drawn (and how many) | 4 |
| How properties and names are looked up in play | 5 |
| How particles are built and drawn | 7 |
| How sound is played: hardware voices and streams on the audio chip, or mixed by the CPU | 7 |
| What is loaded when: the hard-disk file cache, background loading, textures and sounds per level | 8 |
| How the frame is timed and presented | 9 |
| How 64 MB is divided | all |

Where the prototype's answer needs something the port's libraries do not have (Direct3D's vertex buffers map to
the static store; DirectSound's hardware voices have no nxdk driver), the item says so and gives the nearest
equivalent.

## The work: one list, written in one pass

No phases. Each item is first read out of the prototype, then written. The items are taken in this order
(largest expected saving first) and none is tested on its own; compile checks only until the list is done.
Nothing starts until the plan is approved.

1. **Monsters posed by the GPU.** Fix the never-run skinning path before its first run (specular output never
   written, fourth light, per-part fallback instead of demoting the whole monster and dropping a frame
   (`render_gl.c` 2233, 2060), program-loaded flag reset with the context), then replace its 87-instruction
   four-bone program with a two-bone one after the prototype's (about 50 instructions; four bones only for Wii
   meshes). Removes `skin_item` for monsters, about 15 % of CPU.
2. **Destroyed and moving parts stay on the fast path.** Today a changed part leaves its batch for 1-64 s, waits
   90 draws to be registered, and after four re-registrations is staged through gl4es for the rest of the level
   (`render_gl.c` 704-720, 1828-1870). Such parts are registered as they change and drawn direct. The likely
   cause of the frame rate falling as buildings come under attack.
3. **Everything else that still crosses gl4es goes direct**: region-textured scenery and taxis (78 materials,
   `data.h` 113; the region scale baked into the texture coordinates), effects and HUD quads, shadows, water,
   debris. With it: no matrix calls for items whose parts are all batched (`render_gl.c` 3232-3240), indices
   written once into the push buffer in long spans, duplicate colour/normal/light/modelview sends removed.
4. **A person is one draw.** People, army units and vehicles are rigid pieces, one draw and one matrix each
   (`city.c` 582-594); a person's pieces are merged at load into one mesh and drawn with a one-bone program.
5. **Property lookups by string every frame** (`prb.c` 129-143; people, vehicles, rail, parade, shadows, army)
   remembered, misses too; names for bones, move-graph calls, effects and sound events resolved once. Estimated
   2-6 ms.
6. **Animation math** (`anim.c` 19-55, 157-174): a key cursor per track, normalised lerp in place of acos/sin,
   SSE 3x4 multiplies, people posed at half rate, staggered. Estimated 2-4 ms.
7. **Particles, sound mixing, collision, cloth, building meshes**: each one to two milliseconds of avoidable
   work a frame (`fx.c` 1897-2003, `sound.c` 1221-1355, `collide.c` 537-588, `cloth.c` 671-679, `city.c` 2420).
   Effects are mixed at 24 kHz and doubled, music stays at 48 kHz.
8. **Nothing loaded or converted during play.** An Xbox texture pack made on the PC by the asset-prep tool
   (DXT1 with mips for solid and cut-out textures, 8-bit palette for the rest, in the GPU's layout) replaces
   decoding, palette building and two GPU drains at a texture's first sight; textures, sounds and property books
   are loaded with the level; no log line goes to the hard disk in play; no wait on the GPU inside a frame
   (`city.c` 4305, the per-texture `glFinish`). The runtime DXT encoder is dropped.
9. **Catch-up bounded.** The world's fixed 1/30 s steps keep their accumulators, so the frame rate is free to
   run above 30; after a hitch at most two steps run in one frame, so one slow frame does not make the next one
   slow. No frame lock, no change to the game's speed.

## Then one test pass

The whole list written and compiling, then: xemu four-player runs in three cities with the CPU sampled every 10 s,
frame rate, worst frame and memory from the game's log, frames read from guest memory and looked at. The
console decides the result (xemu's timing is not the console's); a run there needs its owner's go-ahead.

## Open risks

- The direct path and GPU skinning have never run anywhere; the review found no fault in `gliDirectDraw` itself
  and the listed ones in the skinning path. Fog sign and packed vertex formats can only be confirmed on screen.
- Estimates are from code reading. If the first test pass shows a different hot spot, the plan changes there.
- NV2A early-Z under pbkit, monster materials that force the CPU fallback, and crowd sizes are in data or
  hardware, not in the code.

## What is written (2026-11-14, compiled, not yet run)

| Item | State | Where |
|---|---|---|
| 1 Monsters posed by the GPU | written: two- and four-bone programs after the prototype's (48 and 56 instructions), short bone offsets, fourth light, specular output, fog as the fixed pipeline's, a skin checked whole before it goes to the GPU | `platform/xbox/shaders/skin2.vp`, `skin4.vp`, `patches/nxdk-gles11-skin-programs.patch`, `render_gl.c` `xbox_skin_*` |
| 2 Changed parts on the fast path | written: copies of their own in GPU memory, remade on a budget (64 KB a frame, one always, 96 KB more for parts kept waiting five frames), the old copies drawn meanwhile | `render_gl.c` `xbox_own_build`, `xbox_part` |
| 3 The rest direct | written: regions' scale baked (batches and loose parts), effects' and HUD's quads as 24-byte vertices straight from GPU memory, no matrix calls for batched items and one load for the others, inline indices in spans of 2,000. Not written: shadows, water and debris still cross gl4es; duplicate state sends | `render_gl.c` `qb_*`, `bat_*`, `gliDirectQuads` |
| 4 A person is one draw | written: a moving model's rigid pieces merged and drawn with the skinning program, shared by all instances of the model | `render_gl.c` `rig_*` |
| 5 Lookups by name | written: property records remembered (misses too), one-pass compare, sound events remembered; bones and move-graph calls in progress | `prb.c`, `plat_xbox.c`, `sound.c` |
| 6 Animation math | written: key cursors (512 KB at most), cached arcs, no library calls on Xbox, SSE affine products | `anim.c`, `city.c` |
| 7 Particles, sound, collision, cloth | particles and sound written (effects mixed at 24 kHz, integer output, refills bounded); collision and cloth in progress; building mesh rebuilds not written | `fx.c`, `sound.c` |
| 8 Nothing loaded in play | written: a level's textures made as it loads, buffered log, three pairs for CPU-posed skins (no wait for the GPU). The sample banks whole in memory is written but off (`--xon sndpre`: 8.6 MB). Not written: the texture pack made on the PC (DXT1 and mips), property books with the level | `render_gl.c` `rnd_preload`, `plat_xbox.c`, `city.c` |
| 9 Catch-up bounded | written: two steps a frame, under one carried over | `vehicle.c`, `rail.c`, `streetobj.c`, `fx.c`, `city.c` |

Test builds take `--xoff NAME` in their built-in command line (CMake `RAMPAGE_XBOX_TEST_ARGS`) to turn one path back
to what it replaced: `quads`, `own`, `region`, `skin`, `skip`, `preload`, `rigid`; and `--xon sndpre`.

## Second batch (2026-11-14): memory, video modes, Xbox buttons

New targets from the owner: 640x480 on a 16:9 screen at 60 frames a second, 1280x720 at 30, the 4:3 picture at 30
and above as before; and the PS2 button pictures replaceable by Xbox ones.

### Where the 64 MB went (first batch's build, four monsters in Las Vegas, a snapshot of the whole RAM in xemu)

| Use | MB | Notes |
|---|---|---|
| Game data files held in memory | 17.4 | Las Vegas 5.1 (2.8 of it texture pixels), four monsters' animations 6.3, effects 1.4, the rest GUI and monsters |
| Other working memory | 13 | meshes as float arrays, HUD layouts 1.2, sound, effects world 1.5 |
| GPU side | 17.0 | three screen buffers 3.6, depth 1.2, a fourth screen nobody shows 1.2, vertex store 4.9, scratch arena 2.0, textures 2.4, push buffer 0.5 |
| Program | 7.3 | code 3.2 (0.8 SDL, 0.8 gl4es), fixed tables 3.9 of which 2.4 were three quad lists |
| Free | 7.8 | the vertex store stops growing at 8 |

1280x720 needs 3.5 MB per screen buffer instead of 1.2.

### What was written

| Item | What | Where |
|---|---|---|
| Screen buffers | The depth buffer is placed in the screen `XVideoSetMode` leaves allocated (never shown again once pbkit has its own): one screen's worth back in every mode. At 1280x720 a front and one back buffer instead of three, with the GPU itself waiting for the refresh that shows a finished frame (`NV097_FLIP_STALL`, released by the blank handler, as Direct3D does) | `patches/nxdk-pbkit-back-buffers.patch`, `win_xbox.c` |
| Video modes | 1280x720 when the dashboard has 720p enabled and the cable is HD; otherwise 640x480, drawn for a 16:9 screen when Widescreen is set. Test builds: `--xvideo 480|481|720` | `win_xbox.c` |
| Texture pixels not kept | A bundle is read without its textures' pixels (every one of the 117 bundles has them last in the file, checked); a texture's pixels are read from the disc when it is made into a GPU texture. Las Vegas in memory: 5.1 to 2.4 MB | `bundle.c` `bdp_open`, `lean_open`; `tex.c` |
| 8-bit textures as they are | The world's textures are 8-bit with a palette on the PS2 and are stored that way by the GPU. They are handed over unexpanded instead of expanded to full colour, uploaded, the GPU waited for, the upload read back and the palette found again | `patches/nxdk-gles11-palette-direct.patch`, `tex.c` `tex_indexed`, `render_gl.c` `xbox_texture_p8` |
| Push buffer several frames long | 2 MB as in the studio's build. The restart (which waits for the GPU to finish everything) happens every few frames instead of every frame; the GPU draws a frame while the next is sent; the CPU is held to one frame ahead by counting the frames the GPU has finished; released vertex memory is freed as soon as the GPU has finished the frame it was released in | `patches/nxdk-gles11-pushbuffer-frames.patch`, `nxdk-gles11-static-fit.patch`, `nxdk-pbkit-back-buffers.patch` (`pb_frames_done`) |
| Fewer register round trips | The GPU is handed new commands every 16 KB instead of every 4 KB | `patches/nxdk-pbkit-kick-batching.patch` |
| Vertex store allocator | Skips full chunks instead of walking every block of every chunk; its reserve of free RAM is 5 MB at 1280x720 | `patches/nxdk-gles11-static-fit.patch` |
| Scratch arena | 768 KB instead of 2 MB (a frame stages about 100 KB at most; test builds log `xbox staging frame_peak_kb`) | `platform/xbox/CMakeLists.txt` |
| Fixed tables | The HUD's and the front end's quad lists are one list of 4,096; the effects' 4,096 | `main.c` |
| `fminf` / `fmaxf` | Compares instead of three library calls each (3 % of the profile) | `src/xbox_math.h` |
| Names | A bundle's names in a hash table (shadows, effects and HUD ask by name every frame); a monster's bounding sphere merged once, not twice, and a homing shot's target once a frame | `bundle.c`, `army.c` |
| Effects | An emitter's texture looked up once, not per quad; one square root for the depth pull instead of four | `render_gl.c` `draw_fx` |
| Xbox buttons | `XBOXTEX.RXT` beside `RAMPAGE.VOL`: a picture in it replaces the bundle texture of the same name. `tools/xbox_glyphs.py prototype <folder> XBOXTEX.RXT` makes it from the studio's own Xbox pictures (same names: `Controller_B0`, `_B1`, `_Back`, `_Select`, `_White`, `Trigger_Left`, `_Right`), `png <folder>` from any PNGs. No art is in the repository. `controller_map` (the pad diagram) has no Xbox counterpart in the prototype | `texpack_xbox.c`, `tex.c`, `tools/xbox_glyphs.py` |

More switches: `--xoff twobuf` (three buffers at 720p), `longpb` (512 KB push buffer, restarted every frame),
`texdrop` (bundles whole in memory), `meshdrop` (the meshes' streams kept in memory), `p8` (textures through full
colour), `glyphs` (the bundles' own button pictures), `batpace` (a changed part rejoins its batch the frame it is due);
`--xvideo 480|481|720`.

On the console itself (a normal build has no command line): the words of `E:\UDATA\RampageRE\xbox_args.txt`, if that
file exists, are taken as the command line, e.g. `--xvideo 480` or `--xoff twobuf --xoff longpb`. No file, nothing
changes. This path has only been compiled; xemu test runs cannot put a file on the emulated hard disk.

### Measured in xemu (2026-11-14)

Four computer-controlled monsters, Las Vegas, King of the City, sound processed and muted, frame rate over
5-second windows from the game's own log, frames read from the emulated console's memory and looked at. The PC
was running other work throughout (20 to 56 % busy), and xemu's speed follows it: only runs made back to back
compare. xemu does not model the GPU's fill rate, and part of its CPU time is emulator overhead on hardware
register accesses that a console does not pay. A console run needs its owner's go-ahead.

| Build | Picture | Windows: lowest / median / highest | Free RAM at worst | Errors |
|---|---|---|---|---|
| `main` (the frozen XBE) | 640x480 | 14 median | 15 MB | - |
| first batch | 640x480 | 31 / 47 / 57 | 7.3 MB | none |
| second batch | 640x480 4:3 | 39 / 53 / 60 | 11.9 MB | none |
| second batch | 640x480 for a 16:9 screen | 37 / 52 / 59 | 12.1 MB | none |
| second batch | 1280x720 (5.5 minutes) | 34 / 49 / 59 | 5.9 MB | none |

- The picture was looked at in all three modes; the results screens show the Xbox A and B pictures. The
  1280x720 picture was looked at on the build before the last (same drawing code); reading a frame out of xemu
  can freeze the emulator in that mode (two buffers, the GPU waiting for the refresh), so the last 1280x720 run
  took no captures.
- A level change is one frame of 2.5 to 4 seconds; the first frame of a city is 9 to 11 seconds.
- 640x480 for a 16:9 screen is not at a steady 60 in xemu: the heavy windows are in the 40s.
- 1280x720 leaves 5.9 MB free. The vertex store did not run short (no refusals), but there is little room.
- The profile after this batch: 12 % idle (frames waiting for the display's refresh), then a long tail with
  nothing above 5 %: the frame's item loop 5 %, the driver's submit 5.5 % (mostly emulator overhead), effects 8 %
  over four functions, cloth 5 %, USB interrupts 3 %, animation 3 %.

## Third batch (2026-11-14): the busiest frames, and more room at 1280x720

A profile of only the frames that miss 60 a second (a test build sets `g_xbox_heavy_frame`; the profiler reads it
with each sample) in the 16:9 picture. About a quarter of those frames is not the game's work: the kernel's idle
loop (waiting on xemu's GPU), interrupt acknowledgements and the driver's interrupt handlers, all of them hardware
register accesses that trap in an emulator. The rest is spread thin. What stood out, and what was written:

| Item | What | Where |
|---|---|---|
| Batches rebuilt whole | A changed part that comes back into its batch has the whole batch built again (up to 10,000 vertices through their matrices, four allocations). With buildings under attack that was several batches every frame: 44 % of the frame routine's own time. Parts now come back together: in four frames of every sixteen, at most one batch's worth of vertices a frame, and parts for a batch already to be built ride along | `render_gl.c` `bat_frame`, `bat_join` (`--xoff batpace`) |
| A monster's bounding sphere | Asked a dozen times a frame for every monster (every climbing probe of every other monster): kept while the pose (`Instance.pose_serial`), placement, mirroring and scale are unchanged | `main.c` `mon_shape`, `city.c` |
| A shape's box | `city_shape_box` parsed every mesh of the shape each time the army, a boss or rubble asked: the last 48 answers are kept | `city.c` |
| Quad chunks | The quad batcher's 96 KB chunks are its own and go round (a chunk is filled again two frames after it was last drawn from) instead of being released and taken from the vertex store for every thousand quads | `render_gl.c` `qb_run` |
| Vertex store gives chunks back | A chunk with nothing in it goes back to the system (one empty chunk is kept); existing chunks are tried before a new one is made | `patches/nxdk-gles11-static-release.patch` |
| Mesh data not kept | A bundle is read without its meshes' vertex streams as well as without its textures' pixels; `mesh_read` and the cloth setup read a mesh's streams from the disc (`bdp_payload`) when a level is built | `bundle.c`, `mesh.c`, `city.c` (`--xoff meshdrop`) |
| Quad vertices in line | `qb_vertex` at each of its calls | `render_gl.c` |

Measured in xemu the same afternoon (same runs as before; the PC 27 to 52 % busy with other work, and the dips
follow it: the second-batch build, run first as the yardstick, did 52 / 59 / 60 in the 16:9 picture where it had
done 37 / 52 / 59 the night before on a busier PC). Lowest / median / highest over 5-second windows:

| Picture | Frame rate | Free RAM at worst (second batch) | Errors |
|---|---|---|---|
| 640x480 4:3 | 35 / 57 / 60 | 15.2 MB (11.9) | none |
| 640x480 for a 16:9 screen | 36 / 56 / 60 | 14.8 MB (12.1) | none |
| 1280x720 (5 minutes, no captures) | 31 / 55 / 59 | 8.2 MB (5.9) | none |

Frame rates in xemu cannot settle whether this batch is faster: the spread between two runs of one build is
larger than the change. Memory is exact. The picture was looked at in the 4:3 run (results screen, play, menu).

What does not move with the PC's load is where the samples fall. The same heavy-frame profile before and after
(16:9 picture, 200 seconds each): frames missing 60 a second were 36 % of all samples before and 18 % after; in
them `mon_spheres` went from 1.5 % to 0.15 %, `army_monster_shape` from 1.1 % to 0.1 %, and the kernel's idle loop
from 6.6 % to 17.3 %: what is left of the slow frames is to a large part xemu's own GPU thread being waited for.
The batch rebuild is still the largest item inside the frame routine in the frames it happens in (36 % of that
routine there); it now happens in fewer frames. Whether the 16:9 picture holds 60 and 1280x720 holds 30 is for a
console to say.

### Not written

- Building walls in fixed buffers rewritten one building a frame (the studio's design): the port's changed parts
  already keep copies of their own in GPU memory, remade on a budget; the profile shows no building rebuild cost
  above 1 %.
- Shadows, water and debris on the direct path: under 1 % each in the profile.
- Mipmapped DXT textures made on the PC: a real console's texture sampling would gain, xemu cannot show it.
- The meshes' float arrays after the GPU has its copy, the HUD layouts' fixed node tables (0.9 MB). (The meshes'
  file data: third batch.)
- The level-change frame is still 2.5 to 4 seconds.
