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

## On the console (2026-11-14)

The first runs on real hardware: the test build (four computer-controlled monsters, King of the City in Las Vegas,
five minutes) launched through the debug monitor, logs copied off, frames read out of the console's memory.
Lowest / median / highest over 5-second windows:

| Picture | Frame rate | Free RAM at worst | Notes |
|---|---|---|---|
| 640x480 4:3 | 38 / 47 / 59 | 13.9 MB | three screen buffers, with the scan-out guard |
| 640x480 for a 16:9 screen | 36 / 45 / 57 | 14.5 MB | the same |
| 1280x720, two buffers, no guard | 20 / 27 / 39 | 8.3 MB | heavy tearing on the television (the owner's report) |
| 1280x720, two buffers, scan-out guard | 17 / 21 / 29 | 8.0 MB | a frame over 1/30 s waits for the next refresh: 20 a second |

xemu had shown 35-60 in every mode: a real console is about half as fast as xemu on this PC at 640x480, and
1280x720 costs the real GPU about 15 ms a frame that xemu does not charge.

What the console showed that xemu could not:

- **Tearing.** pbkit hands out the next buffer to draw into without looking whether it is on screen. With a front
  and one back buffer it always is until the blank; the GPU-side wait written for that (`NV097_FLIP_STALL`) does
  not hold the GPU back on real hardware. `gliBackBufferAcquire` (patches/nxdk-gles11-scanout-guard.patch, after
  the Perfect Dark Xbox port's `nv2a_acquire_back_buffer`) waits, before a frame's first clear or draw, until the
  buffer about to be drawn into has left the screen. A frame read out of memory never shows a tear: only the
  television does.
- **The depth buffer's screen.** `pb_depth_on_boot_screen` needs the screen `XVideoSetMode` allocates on a 16 KiB
  boundary; on the console it was not, and 1280x720 paid 3.5 MB for a fourth screen (5 MB free, the vertex store
  refused thousands of times). `win_xbox.c` `boot_screen_align` pads the allocation onto the boundary: 8 MB free,
  no refusals.
- **A crash at the first launch.** Launched over a running dashboard, the build died on an illegal instruction with
  five 4 KiB pages of its own code overwritten by six-channel sound frames. Not seen again after a cold reboot.
  The audio processor (MCPX APU) is suspected: this program never touches it, so `plat_xbox.c` `quiet_apu` now
  switches its voice processor off and puts its DSPs in reset at startup. Unproven.
- **Monsters flickering between black and washed out.** The fixed-function lighting stage, left enabled, still
  runs behind a vertex program on a real NV2A and replaces the program's colours; xemu ignores the enable in
  program mode. Found by switching the program's inputs off one at a time from `xbox_args.txt` on the console
  (lighting, colour, normals, fog: none helped) and then the lighting enable (steady in twelve frames out of
  twelve). `gliSkinDraw` now switches it off for its draw, as Direct3D does for any vertex shader
  (patches/nxdk-gles11-skin-lighting-off.patch).
- The game's save folder (`E:\UDATA\RampageRE` to the game) is `xV:\RampageRE` to the debug tools, whose own `xE:`
  is another folder. `xbox_args.txt` there works (the 640x480 runs above were made with it).

Where that leaves the targets: 640x480 4:3 holds above 30 on the console. The 16:9 picture is in the 40s, not 60.
1280x720 without tearing is 20; it needs either frames under 1/30 s (the GPU's part of the frame is the cost
there) or a third screen buffer (3.5 MB, of the 8 that are free).
(Both were done: the section after this one.)

## 1280x720 at 30 (2026-11-14, on the console)

Same test (four computer-controlled monsters, Las Vegas), lowest / median / highest over 5-second windows outside
the block loads:

| Build | 1280x720 | Free RAM at worst |
|---|---|---|
| three screen buffers, scan-out guard | 23 / 26 / 33 | not noted |
| + mip levels for the 8-bit textures, the sky drawn late | 20 / 27.6 / 41 | 4.8 MB |
| + the effects thinned to a fixed two screens | 26.7 / 31.5 / 41.1 | 5.4 MB |
| + the effects' budget following the frame | **30.1 / 32.0 / 41.4** | 4.2 MB |

640x480 with the last build: 35 / 43 / 58 (as before, within what two runs differ by).

What was written:

- **Three screen buffers at 1280x720 by default** (`--xon twobuf` for two). With two, a frame over 1/30 s waits a
  whole refresh for its buffer to leave the screen. The third costs 3.5 MB: the push buffer is 1 MiB at 720 lines
  (2 MiB at 480), and the vertex store keeps 3.5 MiB free.
- **Mip levels for the 8-bit textures** (patches/nxdk-gles11-palette-mips.patch; `--xoff mips`).
- **The sky after the street and the batched buildings** (`--xoff skylate`), at its real distance and depth-tested,
  so it is filled only where it shows. A first version that pushed it to the far plane with `glDepthRange` drew
  nothing at all on the console.
- **Test builds: `--xskip N`** leaves kinds of drawing out (render_gl.c `g_xbox_skip`), to measure on the console
  what each costs.

What each kind of drawing costs a 1280x720 frame on the console (first block, ms a frame; the frame is the GPU's
there, so these are GPU costs; "nothing drawn" is the refresh, 17 ms):

| Left out | Frame, ms | Saved |
|---|---|---|
| nothing | 36.5 | |
| the effects | 27.5 | 9.0 |
| the main pass's items | 30.2 | 6.3 (with the effects out too: 8.2, of which skinned 2.6, the others 5.0) |
| the opaque batches | 33.2 | 3.3 |
| the sky | 34.7 | 1.8 |
| the blended batches | 34.9 | 1.6 |
| the street | 35.3 | 1.2 |
| the shadows | 35.3 | 1.2 |
| the HUD | 36.2 | 0.3 |

At 640x480 the effects cost 0.8 ms and the items 3.9: there the frame is the game's own work, and the GPU has
time to spare.

**The effects are the 1280x720 problem.** A test build logs how much of the screen the effects' quads cover
(`xbox fx` lines). Over 34 windows: frame = 27.2 ms + 2.5 ms for each screen covered (correlation 0.92), and the
smoke and dust of falling buildings cover 2 to 9 screens (one frame up to 28). The game's effects are hundreds
of soft quads over each other, sized for a PS2 that fills several times faster and has a third of the pixels.
Not blending the faintest texels (alpha reference 8/255) measured no gain.

`draw_fx` now keeps the world's effects inside a budget (render_gl.c `g_xbox_fx_budget`, `--xfxbudget N` in
hundredths of a 1280x720 screen, 0: off; default 200):

- When a frame's quads would cover more, a share of the particles is left out: the same ones from frame to frame
  (`FxQuad.key`, from the particle's slot), those next to go faded, never under a fifth drawn; the share comes
  back slowly and drops at once.
- The quads drawn are made denser by what it takes to hide what all of them hid (`fx_denser`: one quad of alpha
  1 - (1 - a)^n for n of alpha a; an additive quad n times as bright).
- A particle the game draws several times over (`int.multiple_render`) is drawn once, denser the same way.
- Above 480 lines the budget follows the frame (`g_xbox_fx_frame_ms`, `--xfxframe N`, default 32, 0: the fixed
  budget): what the rest of the frame takes is measured, and the effects get what is left of 32 ms, between 0.8
  and 3 screens. The first block's street alone takes 31 ms and leaves them under a screen; later blocks take 23
  to 26 and leave two to three.
- At 640x480 the same pixel budget is six screens, which only the largest clouds reach.

Seen in frames read off the console with the budget on and off: the dust and smoke of hits look the same. Not
seen side by side: a whole building's cloud at its thickest (the two runs do not play alike), and anything that
shows only in motion.

What is left of a 1280x720 frame without effects is 27 ms; the unskinned items of the main pass (cars, debris,
damaged buildings' own parts) are 5 ms of it and the next thing to look at.

## The Wii's monsters and Dallas on the console (2026-11-14)

The port's Wii extras (docs/WII_PLAN.md) on the Xbox: a `wii` folder beside `default.xbe` (`Game`, `Shell`, `Audio`)
switches them on. As it stood they did not fit: Dallas with PS2 monsters used all 64 MB at 640x480 and the scene
failed to build. Measured on the console at 1280x720, four computer-controlled monsters (Boris, Sarah, Brian,
Cyril), lowest / median / highest over 5-second windows outside the block loads:

| Scene | Frame rate | Free RAM at worst |
|---|---|---|
| Dallas, blocks 1 to 4 | 27.7 / 31.3 / 38.0 | 4.3 MB |
| Dallas, blocks 4 to 7 (Fountain Place and Downtown the busiest: 65,000 triangles a frame) | 23.4 / 29.2 / 36.9 | 2.4 MB |
| Las Vegas with the four Wii monsters | 28.0 / 31.9 / 39.8 | 4.5 MB |
| Las Vegas with PS2 monsters, the Wii folder present (40 monsters in the roster) | 29.2 / 32.2 / 39.8 | 8.0 MB |

(The Las Vegas run with Wii monsters was made before the last memory and reflection changes.)

What was written:

- **Lean images of the Wii's bundles** (`bundle.c` `lean_open`): laid out as the PS2's, their numbers big endian
  (all 129 checked). Dallas.bdg is 9.4 MB, 5.2 of it textures and 2.8 mesh streams; Shell.bdg 12 MB, 11.7 of it
  textures. `tex.c` reads a Wii texture's pixels from the disc as it does a PS2 one's.
- **The Wii's textures in the GPU's own formats** (`tex.c` `tex_gx_native`, `render_gl.c` `xbox_texture_gx`,
  patches/nxdk-gles11-texture-native.patch `gliTextureNative`; `--xoff gxnative`): a CMPR texture is DXT1 with
  its bytes in another order, converted exactly; RGB5A3 goes to 16 bits (A1R5G5B5 exactly when all its texels are
  opaque, else A4R4G4B4); I8 to 8 bits with a grey palette. With the mip levels of the file. Dallas's textures in
  memory: 11.5 MB to 4. Checked on the console by captures.
- **A page for a texture about to be replaced** (`gliTexturePlaceholderNext`): the full-colour image the 8-bit
  and native paths made only to replace it was up to 4 MB of contiguous memory for a moment.
- **The largest Wii textures halved above 480 lines** (`g_xbox_gx_skip`, `--xgxskip N`): the level under the
  largest for textures of 512 a side or more (Dallas has four of 1024; the Wii's monsters' are 512, the PS2's
  256). 4 MB to 2.5.
- **HUD layouts' nodes sixteen at a time** (`hud.c`): each layout had a fixed table of 136 KB and a game has some
  fifteen: 2 MB to about 0.5. (Found by walking the heap of a RAM snapshot in xemu.)
- **Particle pool 4,096** on the Xbox (1.5 MB at 8,192; about 1,300 alive at the most seen); **decoded sound
  kept: 1 MB above 480 lines** (2 at 480); **the vertex store's reserve 2.25 MiB at 1280x720** (3.5 before).
- **Reflections' coordinates from the GPU** (patches/nxdk-gles11-direct-spheremap.patch `gliDirectSphereMap`,
  `--xoff gpusphere`): GLES 1.1 has no texture coordinate generation, so gl4es made the sphere-map coordinates
  of every reflective part for every vertex of every frame and sent them as client arrays. Fountain Place's
  mirror-glass towers were 6,000 triangles that way and 10 ms of the frame (42 ms to 32 without them). The NV2A's
  fixed-function pipeline does it itself (NV097_SET_TEXGEN_S = sphere map, the texture matrix for the scale).
  Checked on the console by captures of the towers both ways.
- **The effects' budget** may now go down to a quarter of the fixed one, and one particle in seven.
- Test builds: `xbox draws` (draws and triangles a frame by kind), `xbox slow draw` (what goes through gl4es and
  why), `wii read` (the Wii files read), `--xskip` 512 / 1024 / 2048, `--xkeepfree KB`.

What the measurements showed:

- With the store short of memory a block is slow whatever its size: refused arrays are drawn through gl4es from
  client arrays (thousands of triangles a frame) and a GPU-posed model is posed by the CPU from then on. Dallas's
  slow blocks at 20 to 23 a second were this until the memory was found.
- The Wii's monsters are 19,500 triangles a frame for four (the PS2's: about 10,500), 4.7 ms of a 1280x720
  frame; posed by the CPU instead they cost 3 ms more (the CPU is then the limit).
- At 640x480 the same Dallas block takes 26 ms without effects and 29 with: the game's own work on the CPU is
  4 ms more than in Las Vegas. At 1280x720 it is 31 ms without effects, so the effects' budget sits at its floor
  there.
- The GPU's occlusion culling register (NV097 0x1D84, Direct3D's D3DRS_OCCLUSIONCULLENABLE) made no difference
  set or not (30.5 and 31.2 ms against 30.8 twice). Not kept.
- The level-change frame with the Wii's data is 4.7 to 5 s (2.8 with the PS2's): the textures are converted and
  the meshes read from a folder's files, opened for each read.

Where that leaves 1280x720 with the Wii's content: at or just under 30 in most of Dallas and in the PS2's
cities; 24 to 30 in Dallas's two busiest blocks, where the frame is 60,000 to 65,000 triangles.

The Wii files a console needs (74 MB of the disc's 316): the ten monsters' `.cmg` and `_anims.cmg`, `Dallas.bdg`,
every `.bdg` of `Game` but the seven PS2 cities' own and the `_scripts` ones, all of `Shell` and `Audio`. The 30
shared monsters' and the seven cities' Wii copies are never read (`vol_find` asks the PS2's archive first).

### Long frames (the owner's report: frequent short freezes)

Test builds log every frame over 70 ms with what it did (`xbox hitch`: the time in the game's step, the drawing,
the sound, the scene's update, and the disc reads, sounds decoded, textures made and mesh streams fetched in it).
A console run that nothing touches while it plays (no log pulls, no captures: a capture pauses the game for 2.5 s,
and the test builds' own log write every 5 s was a 0.1 s frame, now every 30 s), Dallas, four Wii monsters,
165 s of play:

| Cause | Frames | Typical | Longest |
|---|---|---|---|
| a sound heard for the first time (read from the disc and decoded) | 19 | 86 ms | 175 ms |
| mesh streams read from the disc (a building's next damage state) | 27 | 84 ms | 115 ms |
| a texture used for the first time | 6 | 87 ms | 104 ms |
| a block change | 4 | 4.4 s | 12 s (the city again) |
| none of these | 1 | 81 ms | |

Every one is the disc read in the middle of play, and nearly all of it was the Wii folder's files. The same run
with the PS2's monsters in Las Vegas (nothing of the Wii's in play) has 7 long frames in 195 s. What made the Wii's
content different, each measured on the console:

| Step | Long frames in play (about 170 s of Dallas, four Wii monsters) | Block change |
|---|---|---|
| as above | 53 | 4.4 s |
| the Wii's sound bank converted to the PS2's format by the preparation tool | 51 | 4.4 s |
| every file of the Wii folder kept open once read | 14 | 2.45 s |
| (the PS2's monsters in Las Vegas, for comparison) | 7 | 2.8 s |

- **Opening a file is 30 to 45 ms on the console.** The Wii folder's files were opened for every read, then (a
  first fix) only the last one read was kept open: 44 of 51 long frames still had a file opened in them, the
  city's bundle and the sound bank taking turns. `vol.c` `path_read` keeps up to twelve open (Win32 handles). The
  PS2's data never paid this: it is one archive, open from the start. A block change went from 4.4 s to 2.45 s
  with it (about 400 reads).
- **The Wii's sounds are converted before the first launch** (the owner's direction: conversions belong in the
  installer, not in the running game). `tools/wii2ps2.py sound` writes a PS2-format bank (little endian tables,
  16-byte sample entries, PS-ADPCM) with the Wii bank's events and programs as they are and the samples its 153
  extra events can play (136 samples, 288 s; the game asks the PS2's bank first, so the rest are never played and
  are left absent): 5.1 MB instead of 13.1. Each sound is decoded and encoded once more (signal to noise against
  the Wii's decoded sound: median 32 dB, lowest 20.5). `sound.c` `bank_load` reads a bank of the Wii folder by
  what it is ("UBER": the PS2's reader; "REBU": the Wii's, still there for an unconverted folder). A sound's
  load in a long frame: 49 ms typical before, 10 ms after both changes (the PS2's: 8).
- **The preparation tool does it** (`tools/xbox_prep/asset_engine.py` `wii_prework`), and leaves out the Wii
  files the game never reads: 82 files and 65 MB go to the console instead of 380 and 315.
- **Dallas's models are converted to the PS2's mesh format before the first launch** (the owner: "Dallas should
  be PS2 format", with no fidelity lost to the eye). `tools/wii2ps2.py models` rewrites every rigid mesh (entry
  type 0) of a Wii bundle as the PS2's: little endian part records, each part one section of vertices in strips.
  The header's byte 0x2B becomes 'P' and `mesh.c` reads such a bundle's rigid meshes with the PS2's reader
  (`Bundle.ps2_mesh`). The rest of the bundle is the Wii's, byte for byte: its tables' byte order is still
  converted when the bundle is loaded, never in play. Dallas.bdg: 1,059 meshes, 64,508 triangles; 77,967 vertices
  in the Wii's indexed lists, 104,223 in strips (1.62 a triangle). Also fx.bdg (10 meshes) and Shell.bdg (11).
  - What the PS2's format holds less finely is rounded to it: a normal to 1/127, a colour to 1/128, a texture
    coordinate to 1/4096 and, where it is beyond the format's range of 8 (151 vertices of Dallas), moved by whole
    repeats of the texture. Positions and triangles are exact. Every converted mesh is read back with the PS2's
    reader and its triangles compared corner for corner with the Wii's.
  - On the console, four fixed views of Dallas with no monsters, the Wii's file against the converted one, frame
    for frame out of the console's memory: the buildings are the same pixels but for one level in 255 on under
    2 % of them; the ground plane (one texture repeated far past 8 times) is one level off on a third of its
    pixels, never more than three. The sky and the traffic differ only by when each frame was taken.
  - A Dallas run after it: 8 long frames in play in 170 s (14 before, the PS2's content 7), blocks 1 to 4 at
    30.2 / 32.1 / 35.7; Fountain Place 27 to 33 (24 to 30 before), Downtown 26 to 34 (23 to 33). Free memory at
    worst 1.9 MB in Fountain Place (2.4 before: the strips' extra vertices), the store refusing nothing.
- **The Wii's monsters stay as the Wii has them.** Counted: every one of the ten has 10 to 45 % of its skin's
  vertices on three or four bones (Boris 691 of 2,941, Sarah 304, Brian 1,348), the third bone's weight up to a
  third. The PS2's skinned format holds two. None converts without changing how it bends.

The frame is also no longer cleared to the fog's colour where the sky is drawn late; what the scene leaves bare
(a strip at the bottom edge in Dallas, found with a magenta clear) is filled by one far rectangle
(`xbox_far_fill`): 1.2 ms of a 1280x720 frame, measured twice. No bare pixel in any of the eight cities (two
blocks each, three captures each).


### Not written

- Building walls in fixed buffers rewritten one building a frame (the studio's design): the port's changed parts
  already keep copies of their own in GPU memory, remade on a budget; the profile shows no building rebuild cost
  above 1 %.
- Shadows, water and debris on the direct path: under 1 % each in the profile.
- Mipmapped DXT textures made on the PC: a real console's texture sampling would gain, xemu cannot show it.
- The meshes' float arrays after the GPU has its copy, the HUD layouts' fixed node tables (0.9 MB). (The meshes'
  file data: third batch.)
- The level-change frame is still 2.5 to 4 seconds.
