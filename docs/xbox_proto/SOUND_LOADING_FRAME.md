# Sound, loading, frame timing and memory in the Xbox prototype

Source: `C:\Programming\GitHub\RampageRE\Rampage Xbox prototype\RampageD.xbe` (debug build, XDK 5849), Ghidra dump
queried with `C:\Programming\GitHub\RampageRE\tools\re_query.py`. Addresses are virtual addresses in `RampageD.xbe`.
DirectSound library functions were identified from the debug library's own "Class::Method" strings (the game calls
them through one-line wrappers at 0xe17c0-0xe19a0 and 0xe4de0-0xe4e60). Only the debug build was decompiled; where the
release build might differ this is said. "Uncertain" marks things not proven from the code.

## Summary (12 lines)

1. Sound: no CPU mixing at all. Every playing sample is its own DirectSound buffer on the audio chip (0xe0a10).
2. Sample data: one 7.5 MB bank (`Rampage.samp`, 288 mono Xbox-ADPCM samples, 287 at 22,050 Hz) loaded whole per package; buffers point into it (`SetBufferData`, no copy, no decode).
3. Limits: 64 voice slots, 32 sound events, 128 registered 3D emitters of which only the nearest 16 are audible; no voice stealing, extra sounds are dropped.
4. 3D is done by DirectSound (listener + per-buffer position/velocity/min/max distance, deferred, one commit per frame); the game only sorts emitters by distance.
5. Music: `.wxv` = RIFF WAVE, Xbox ADPCM stereo 48 kHz, streamed by a dedicated thread (8 ms sleep) through a DirectSound stream, 3 packets of 18,432 bytes. WMA code exists but is never used. XACT files in `Media\` are never referenced.
6. Loading: all level data goes into one fixed 27.5 MB physical block used as a stack of bundles; zeroed on every package switch. Nothing proven to load during play.
7. Textures and static vertex buffers are stored ready-made in the bundle and registered in place (`D3DResource_Register`), zero copy, zero conversion.
8. The hard-disk cache and its background writer are fully coded but never switched on in this build; files are read straight from the DVD, whole-file, unbuffered.
9. Threads: main, sound streaming, one lazy background I/O thread. No loading thread; the loading screen is redrawn from an I/O callback.
10. Display: 640x480, 32-bit, D24S8, no antialiasing, one back buffer, push buffer 2 MB. Debug build presents without vsync and caps with a sleep.
11. Timing: fixed 1/30 s simulation step (1/25 on PAL-50), accumulator giving 0, 1 or 2 ticks per drawn frame, leftover clamped to one step; no interpolation.
12. Memory: ordinary process heap plus the one 27.5 MB block plus fixed pools that fail soft; no custom general allocator, no tagged allocations.

---

## A. Sound

### A.1 Structure

| Layer | Source file (from asserts) | Key functions |
|---|---|---|
| Sound system (handles, music fades, voice-over queue) | `Sound\SoundSystem.cpp` | update 0xd7610, play 0xd80f0, stop 0xd8c30, shutdown/flush 0xd7b20 |
| 3D emitter list | `Sound\Sound3D.cpp` | init 0xdc440, add 0xdc600, remove 0xdc8f0, update 0xdc960 |
| Sound files (bank directory, name lookup) | `Sound\SoundFile.cpp` | load 0xddf50 / 0xde0c0, find by name 0xdd9d0 |
| Macro manager (voice slots, per-voice script) | `Sound\SoundMacroManager.cpp` | alloc slots 0xe1cd0, start macro 0xe2320, play by name 0xe2530, update 0xe2790 |
| Xbox device | `Sound\xbox\xbox_SoundDevice.cpp` | class `SoundDevice` (37 virtuals, table 0x4ce80c), init 0xdf550, start voice 0xe0a10 |
| Streams | `Sound\xbox\audiostream.cpp` | `CAudioStream`, `CWaveFileStream` (0xe4330, 0xe43d0, 0xe4700), `CWMAFileStream` (unused) |
| Script glue | `Sound\SoundScript_Interface.cpp` | `SoundDeviceInterface`, `ScriptSoundEffect` (playSound, playMusic, set3DData, setReverb, ...) |

### A.2 Is anything mixed on the CPU? No

The complete list of calls from game code into the DirectSound library (from the call graph) is: `DirectSoundCreate`,
an HRTF selector, `DownloadEffectsImage`, `SetAllParameters` (listener), `SetI3DL2Listener`, `CommitDeferredSettings`,
`DirectSoundCreateBuffer`, buffer `SetBufferData / SetMixBins / SetVolume / SetPosition / SetVelocity / SetMinDistance /
SetMaxDistance / SetDopplerFactor / Play / Stop / Pause / GetStatus / Release`, `DirectSoundCreateStream`, stream
`SetVolume / SetHeadroom / SetMixBins / Pause / FlushEx / Process`, `XWaveFileCreateMediaObject`, `DirectSoundDoWork`.
There is no `Lock`, no `SetFrequency`/`SetPitch`, no software mixer, no decoder call for sound effects. Mixing,
sample-rate conversion, ADPCM decoding, 3D and reverb all happen on the audio chip (ADPCM decode by the chip is per
XDK documentation, not something visible in the game code).

### A.3 Device start-up - `SoundDevice` virtual 0 (0xdf550)

1. `DirectSoundCreate(NULL, &device, NULL)`.
2. HRTF selector 0x37e593 (installs the HRTF function table at 0x38eea0). The release build links only
   `DirectSoundUseFullHRTF`, so this is very likely the full HRTF; not proven for the debug build.
3. Effects image: reads `dsstdfx.bin` whole (0xdffa0) and calls `DownloadEffectsImage` with reverb index 3 and
   crosstalk index 4 (the standard XDK effects image; used only for the I3DL2 reverb send and crosstalk).
4. Listener: `SetAllParameters` with a 0x40-byte listener block: position 0, velocity 0, front (0,1,0), top (0,0,1),
   distance factor 1.0, rolloff 1.0, doppler 1.0.
5. Creates the streaming thread: `CreateThread(0, 0, 0xdf750, this, 0, 0)` (default stack, default priority).
6. Allocates the voice slots: 0xe1cd0(0x40) = **64 slots of 0x3c bytes**.

`Media\Sound.xsb` (XACT sound bank, 632 bytes) and `Media\Wave.xwb` (XACT wave bank, 190 KB) have no string reference
anywhere in the executable and no XACT function is linked: unused leftovers from an XDK sample (like `ArialUni.Ttf`).

### A.4 Sample bank: format, memory, when loaded

Files (parsed directly):

- `Rampage.uber` (18,234 bytes): header "UBER" v1.1 with six section offsets. "PROJ" v2.1 = 139 sound effects, 12 bytes
  each (float radius scalar, flags, macro index, group byte, max-instances byte). "POOL" v1.1 = 139 macros (little
  programs of 8-byte commands). "SDIR" v1.1 = sample directory: total size 7,525,260 and 289 entries of 16 bytes
  (data offset, data size, flags with bit 0 = loop, offset of the wave-format header). Then a sorted name table.
- `Rampage.samp` (7,525,260 bytes): each sample is a 20-byte wave-format header followed by raw data.
  **288 samples, all mono Xbox ADPCM (format tag 0x69, 36-byte blocks, 64 samples per block); 287 at 22,050 Hz, one
  at 44,100 Hz; 17 looped; about 606 seconds in total; largest sample 366 KB, smallest 2.5 KB.**

Loading (once per package, i.e. at every shell or level load; unloaded at package end):

- `PackageResLoader` virtual 15 (0x1c3c80) -> 0x1c3fb0: reads `.uber` and copies it to the heap; 0x1c4140 reads the
  whole `.samp` in one read into the *top end* of the 27.5 MB bundle block as a staging area (0x192800).
- 0xde0c0 checks the four section versions, builds a sorted index, then `SoundDevice` virtual 22 (0xe03f0) allocates
  one heap block of the SDIR total size (7,525,260 bytes) and copies the bank into it (reference-counted).
- Optional second bank per language id (same function, 16-byte table entries) - not present in the prototype data.
- At package end `PackageResLoader` virtual 2 (0x1c2b70) stops all sounds (0xd7b20, polling with 50 ms sleeps), unloads
  the banks (0xd7af0) and frees the copies.

"Preload" and "cache" exist in the portable layer (0xe3480, 0xe35d0, 0xe36a0) but on Xbox the device hooks are empty:
virtual 24 returns 1, virtual 25 does nothing, virtual 26 returns 0. Nothing is loaded on demand; the whole bank is
resident for the whole level.

### A.5 Starting a sound - what one voice costs

Game code -> 0xd80f0 (needs one of **32 sound handles**, 0xd8f30) -> 0xe2530 (binary search by name, optional
per-effect instance limit: the shipped data has 255 everywhere, i.e. no limit) -> 0xe2320 (takes the first free of the
64 slots; **if none is free the sound is simply not played**, there is no priority or stealing) -> the macro runs in
the next update and its "play sample" command (opcode 0x10) calls `SoundDevice` virtual 36 (0xe0a10):

1. `DirectSoundCreateBuffer` with a 0x18-byte description: flags 0x10 (3D control) for 3D voices, 0 for 2D; buffer
   bytes 0; format pointer = the wave-format header inside the bank. **A buffer is created per play and released
   after it stops; there is no buffer pool.**
2. `SetBufferData(buffer, bank + offset, size)` - the chip plays straight out of the bank memory.
3. `SetMixBins`: 2D voice -> front left, front right, front centre. 3D voice -> the four 3D bins (6, 8, 7, 9), front
   centre (2) and the I3DL2 reverb send (10); one group type adds the LFE bin (3).
4. 3D parameters, all with the "deferred" flag while inside the frame update: position, velocity,
   **min distance = radius / 8, max distance = radius x 1.5** (constants 0x4bf97c, 0x4b19b0). Non-positional sounds of
   3D groups are placed at the listener (min = max = 1, doppler 0) and follow it (0xe05e0).
5. `SetVolume` from a 128-entry table in hundredths of a decibel (0x5731d8: 0 -> -6400, 127 -> 0).
6. `Play(0, 0, FROMSTART)` or `FROMSTART | LOOPING` when the directory flag bit 0 is set.

No pitch control exists: every sample plays at its recorded rate and the chip resamples to 48 kHz.

### A.6 Per frame on the CPU - 0xd7610 (called from `PackageApp` virtual 17, 0x1ba360, and from the loading screen)

1. Warn if more than 0.5 s passed since the last call ("Process starved").
2. Begin deferral (virtual 6, a counter).
3. Macro update 0xe2790: loop over the 64 slots; run macro commands (wait, jump, random branch, round-robin, play
   sample, stop sample, fade, spawn child macro); apply linear volume fades with `SetVolume`; when a slot's sample has
   finished (`GetStatus`) the slot is freed.
4. 3D update 0xdc960: for each registered emitter recompute velocity by finite difference (smoothed 0.1 / 0.9), update
   squared 2D distance to the listener, keep the list sorted (one neighbour swap per emitter per frame); emitters
   beyond the **16 nearest** or outside their radius have their voice stopped (one-shots are removed); inside, the
   voice is started if not playing and its position/velocity pushed (0xe2700 -> virtual 31, 0xe0820).
5. End deferral (virtual 7, 0xdf910): **one `CommitDeferredSettings` per frame**.
6. Free finished handles (32), service the two music slots (volume fades in 128-step units, end detection) and the
   voice-over queue.
7. `SoundDevice` virtual 5 (0xdf990): `DirectSoundDoWork()` under the device critical section, then release buffers
   that have stopped (virtual 4, 0xdf840).

Work per frame is therefore a few short loops (64 + up to 128 + 32 entries) and a handful of DirectSound parameter
calls for the at most 16 audible 3D voices. No audio samples are touched by the CPU. No timing figures exist in the
build for this; the cost estimate is from reading the code.

### A.7 Music

- `.wxv` files are plain RIFF WAVE: format tag 0x69 (Xbox ADPCM), **2 channels, 48,000 Hz**, 72-byte blocks,
  54,000 bytes per second. `Track01.wxv` 4.2 MB, `Track02.wxv` 6.2 MB, `Results.wxv` 0.65 MB.
- `SoundDevice` virtual 8 (0xdf9e0): two music slots; appends ".wxv" if the name has no extension; creates a
  `CWaveFileStream` (0xe4330, 0x154 bytes: 3 packets of 0x4800 bytes). If the slot is still playing it blocks the
  caller until the thread has stopped it ("Blocking while waiting for music to stop").
- Opening happens on the sound thread (`CWaveFileStream` virtual 1, 0xe43d0): `XWaveFileCreateMediaObject` on the
  file, asserts ADPCM, `DirectSoundCreateStream`, mix bins front left 0 dB, front right 0 dB, back left -6 dB, back
  right -6 dB, and **`XPhysicalAlloc(0xD800, ..., PAGE_READWRITE | PAGE_NOCACHE)`** = 3 x 18,432 bytes. One packet is
  256 ADPCM blocks = 16,384 sample frames = 0.34 s, so about one second is queued.
- Pump (`CWaveFileStream` virtual 2, 0xe4700, with 0xe3eb0 and 0xe40c0): for each free packet read 18,432 bytes from
  the file object and submit it to the stream. Looping rewinds the file object (virtual 5, 0xe47f0). End of file pads
  with silence.
- Thread loop 0xe0150: enter the device critical section; if more than 100 ms since the last `DirectSoundDoWork`,
  call it (the main thread resets that timestamp every frame, so the thread only does this while the main thread is
  stalled, e.g. during loading); service both slots (open / pump / stop + `FlushEx`); leave; `Sleep(8)`.
- `CWMAFileStream` (0xe4820, 3 x 0x2000 packets, `XWmaDecoderCreateMediaObject`) has no caller of its constructor:
  WMA music is dead code in this build. The XMV movie player is linked and wrapped at 0x2667f0-0x2668b0 (not studied).

### A.8 Limits in one place

| Limit | Value | Where |
|---|---|---|
| Voice slots (one DirectSound buffer each) | 64 | 0xdf550 -> 0xe1cd0(0x40) |
| Sound events with a handle | 32 | 0xd8f30 |
| Registered 3D emitters | 128 (0x40 bytes each) | 0xdc440 |
| Audible 3D emitters | nearest 16, and inside radius | 0xdc960 |
| Music streams | 2 | 0xdf550, 0xdf9e0 |
| Sound groups | 64 | 0xd93a0, 0xde0c0 |
| Reverb presets | 25 (index 0-24, 0x30 bytes each at 0x573410) | virtual 21, 0xe0f00 |
| Default 3D radius | 1000 x per-effect scalar (0.005 to 1.0 in the data) | 0xdc600 |

---

## B. Loading and caching

### B.1 Bundle format and the block they load into

- `.bdl` and `.cmf` are the same format ("Pipeworks Bundle File version 1.3", version word 0x514 at +0x30), not
  compressed, padded to 2,048 bytes. Header 0x60 bytes; directory 0x18 bytes with a 16-bit entry count, the entry
  table, **block A (ordinary data) and block B (GPU data, aligned to 0x80)**, followed by 0x12-byte pointer fix-ups.
  Entries are 16 bytes: id, type, name, pointer into A, pointer into B. Parser 0x226610 (in-place variant 0x226da0).
- The `.zip` beside a `.cmf` holds exactly one deflated copy of that `.cmf` (George: 475,136 -> 152,324 bytes). The
  inflate is the game's own zlib 1.1.3 plus "unzip 0.15" reading from memory (0x1947b0, 0x195480, 0x199dc0), not the
  D3DX copy. `PackageResLoader` virtual 5 (0x1c2ef0) uses the plain file unless zip is forced or the plain file is
  missing. Uncertain which of the two the monsters actually use at run time.
- **The block:** `RApp::init` (0x12060) calls `AppXbox` virtual 8 (0x1e2b70) with the constant at 0x538efc =
  **0x01B7C400 = 28,820,480 bytes (27.5 MB)**: `XPhysicalAlloc(size, any address, default alignment, PAGE_READWRITE)`.
  Debug builds add 0xAB000 when less than 0x11C7000 bytes are in use. Bundles are pushed onto it like a stack
  (`PackageResLoader`, table 0x4fe6bc): each bundle rounded up to 256 bytes, at most 32 bundles, assert "Memory quota
  exceeded", strict last-in-first-out unload (0x1c39a0). Inside a bundle 0x226210 bump-allocates, block B aligned 0x80.
- A zipped bundle is read into the top end of the block and inflated down to the stack top; a temporary heap buffer
  is used only if the ranges would overlap (0x1c2ef0, 0x1c3290).
- Without a block region the fallback is `operator new` for block A and `D3D_AllocContiguousMemory(size, 0x80)` for B.

### B.2 Reading files

- Whole-file reader 0x192800: `CreateFile` with `FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN` (0x28000000); if
  the size is not a multiple of 0x800 it reopens without sequential scan; **one `ReadFile` for the entire file**.
- Section reader `BundleReaderSynchronous` (0x229120): `CreateFile(..., FILE_FLAG_RANDOM_ACCESS)` then seek + read per
  section (header, directory, fix-ups, strings, entries, A, B) straight into the final place.
- `XSetFileCacheSize` has no caller (XDK default file cache).

### B.3 Textures and geometry: zero copy

- Bundle entry type 9 -> 0x209550 -> `GraphicsDevice` virtual 22 (0xe7a60) -> `D3DResource_Register(header in block
  A, pixels in block B)` (wrapper 0xeccf0). The D3D resource header is stored ready-made in the file; the engine's
  0x40-byte texture wrapper is constructed in the bundle memory right behind it. Textures (type 3), cube maps (5) and
  palettes (0xd) are handled this way.
- Static vertex buffers likewise: `GraphicsDevice` virtual 19 (0xe73e0) constructs the wrapper inside the bundle and
  registers it (callers 0x264470, 0x2652a0, 0x265b20, 0x265bb0).
- `XGSetTextureHeader` is used only by `GraphicsDevice` virtual 14 (0xe6c50) for textures built over caller memory.
- Runtime buffers use `CreateVertexBuffer2` (0xe7050, write-only) and 16-bit index buffers (0xe71a0).

### B.4 When things load, and the loading screen

- Level bundles (city, `_buildings`, `_scripts`, `_props`: 0x31bc0) and monster bundles (`%s_anims%s`: 0xa7e90, from
  `RWorld` virtual 11, 0xa3b10) load synchronously at level start. The sound bank loads in the same phase (A.4).
- An asynchronous path exists (`FileManager` virtual 11, 0x17b670, queueing open + read on the background I/O thread;
  queue 0x18de40-0x18e1f0 pumped each frame from `GuiManager` virtual 6 and `WorldPackage` virtual 6;
  `PackageResLoader` virtual 8 / 10 finish such a load in place). **Uncertain who, if anyone, starts such loads during
  play.** The overlapped bundle reader (0x227ef0, 0x2281c0) has no references and the script command `streamBundle`
  ends in an empty stub (0x1441f0).
- No loading thread. The loading object registers itself as the file manager's I/O callback (0xc5bd0, from 0xc4a00);
  readers call it after each read and while waiting. Its tick (0xc4c70) is limited to 1/30 s, clamps dt to 0.1 s,
  updates the progress text, runs the sound update, renders and presents (0xc5470). The previous front buffer is
  copied to the back buffer first (0xc57c0).

### B.5 Between levels

`RWorldPackage` virtual 4 (0xa53e0) -> `PackageResLoader` virtual 2 (0x1c2b70): bundles released in reverse order, a
leak check, all sounds stopped, sound banks freed. On every package start virtual 1 (0x1c2b20) **zeroes the whole
27.5 MB block** (0x1c4480). Address-range sweepers exist for leaks (0x1e9f20, 0x1ea070, 0x209b80
"TextureManager::forceFreeRange").

### B.6 Hard-disk cache: coded, not switched on

- The utility drive Z: is mounted by the XBE header flags (0x10124 = 1: mount, no format, 16 KB clusters), not by
  game code.
- `XBFileCache::init` (0x196c10) has **no caller** and no pointer to it in the image, so the lookup (0x197a80) always
  answers "not cached" and every read goes to the DVD. Whether the release build enables it is unknown (it still
  contains the string `Z:\cache_status.bin`).
- The design as coded (0x196c10-0x199b70): per file on first use, up to 0x300 files, copies named `z:\cache_%i.bin`;
  on a miss the file is read from DVD in 0x10000-byte chunks on one thread while the previous chunk is written to Z:
  on another, with a `VirtualAlloc`'d 0x20000 scratch buffer and an MD5 of the data; on a hit the file is read back in
  0x20000-byte requests and its MD5 compared. `Z:\cache_status.bin` has a 0x3c header (version 0x1012) and 0x18-byte
  records; a hit also requires matching size and a DVD write time older than the copy. Any doubt formats Z: (0x197740).

### B.7 Threads

| Thread | Created | Stack | Job |
|---|---|---|---|
| Main | CRT start-up 0xca199 | 0x10000 (XBE header) | everything, including loading and the loading screen |
| Sound streaming | 0xdf550, entry 0xdf750 | default | loop 0xe0150 (A.7) |
| Background I/O | 0x195f80, entry 0x196050 | 0x3000 | lazy; request queue (read in 0x8000 slices / write / open), one critical section, two auto-reset events; the main thread waits with poll + `Sleep(0)` + idle callback (0x1965b0) |
| XDK WMA worker | library 0x3be56b | - | not shown to be reached |

No game code sets a thread priority (`SetThreadPriority` is called only inside the D3D library).

---

## C. Frame timing and presentation

### C.1 Device creation - `GraphicsDevice` virtual 0 (0xe5020)

Called from `AppXbox` virtual 1 (0x1e2560). First `Direct3D_SetPushBufferSize(0x200000, 0x8000)` (wrapper 0xec910):
2 MB push buffer, 32 KB kick-off. Then `CreateDevice(0, HAL, 0, 0x40 = hardware vertex processing, &params, &dev)`
(0xec8e0), then `Clear` and `Swap(0)`.

| Presentation parameter | Value | Meaning |
|---|---|---|
| Back buffer | 640 x 480 | always; no 720p/1080i/PAL-576 path |
| Format | 6 | A8R8G8B8 (5 = R5G6B5 only if 16-bit were requested) |
| Back buffer count | 1 | |
| Multisample | 0 | none (0x1121 quincunx only behind an app flag nothing sets) |
| Swap effect | 1 | discard |
| Auto depth-stencil | 1, format 0x2A | D24S8 |
| Flags | 0x20 or 0x40 | interlaced, or progressive when 480p is enabled and the HD pack is present; never widescreen |
| Refresh | 60 (50 on PAL without PAL-60) | |
| Presentation interval | 0x80000000 | immediate (no vsync), because `PackageApp` virtual 1 (0x1b9f20) sets app flag 0x20 first |

Uncertain whether the release build also runs with immediate presentation; this was read from the debug build only.

### C.2 End of frame - `GraphicsDevice` virtual 6 (0xe5f20)

1. If no fence was inserted this frame, insert one (0xebe80; normally inserted at the start of world drawing, 0x1dddd0).
2. `BlockOnFence(fence)` (0xecb80): the CPU waits until the GPU has reached the start of this frame's world drawing,
   so it can run about one frame ahead and never more.
3. `Swap(0)` (the only game caller of `D3DDevice_Swap` besides device creation, wrapper 0xec930).
4. Optional clear: target + Z + stencil, colour 0xFF000000, Z 1.0, stencil 0x80 (0xe6020).

`BeginScene`/`EndScene` are empty stubs. The game never calls `BlockUntilIdle`, `BlockUntilVerticalBlank`,
`SetVerticalBlankCallback`, `SetSwapCallback` or `PersistDisplay`. `SetFlickerFilter(5)` and
`SetSoftDisplayFilter(0)` are only the D3D library's own defaults; the game never changes them. Everything is drawn
directly to the back buffer: the engine's only render-target wrapper (0xedf20) has no callers, there are no blur or
glow targets.

### C.3 Main loop and time

- Loop `_AppBase` virtual 2 (0x1e3120): `while (!quit) tick()`; tick = `_AppBase` virtual 11 (0x1e3010).
- One timer class on `QueryPerformanceCounter` (0x189870 / 0x1898e0 / 0x189990 / 0x189e40). No `GetTickCount`.
- Frame cap: remaining = minimum frame period - elapsed; if positive, `PackageApp` virtual 19 (0x1ba450) sleeps the
  whole milliseconds (no spin). It sleeps when vsync is on or when the debug setting "VSyncDebug" is on (default on).
  Minimum frame period: **1/30 s while a world is loaded** (0x1dd950), 1/60 s in app init, pause and after unload,
  1/refresh in menus.
- Simulation step: `RWorld` virtual 0 (0xa37a0) sets 30 ticks per second, 25 if the refresh rate is below 51 Hz.
- **Accumulator ("interleave") - `RWorldPackage` virtual 5 (0xa5480)**, verified: add real dt to the accumulator, then
  - accumulator <= step: draw only, no tick;
  - else subtract one step; if the rest <= step: one tick + one draw;
  - else subtract a second step, tick without drawing, tick with drawing, and clamp the accumulator to one step.
  So 0, 1 or 2 ticks per drawn frame; game speed holds down to 15 fps and slows below; no interpolation. The debug
  option "ForceNoInterleave" forces one tick per frame.
- Inside 0x1dddd0 the order is: input, draw the previous state (0xa66f0), then tick (0xa4360).
- Debug aids: fps and vertex/triangle counters (0x1e3290), a hierarchical profiler with labels DrawAndUpdate, Update,
  World, Render, Ground, PreAlpha, Alpha (0x1e42e0, 0x1e4840), a "HighFrameRate" option that drops some effects, and
  an effects limiter (0x123be0). No millisecond budget constants were found.

---

## D. Memory

- **No custom general allocator.** `operator new` (0x27e6a0) ends in `RtlAllocateHeap` on the process heap; no tagged
  `new`, no categories. `XMemAlloc` is the stock one and only library code calls it.
- **One big allocation:** the 27.5 MB bundle block (B.1). The only other game callers of `XPhysicalAlloc` are the two
  stream buffers (0xD800 and the unused 0x6000).
- **Sound bank:** one heap block of 7,525,260 bytes for the level (A.4).
- **Fixed pools, all failing soft** (creation is refused, the game carries on):

| Pool | Where | Size |
|---|---|---|
| Script objects (Rampage table 0x4c3f5c) | 0x1c9b80 | 128 x 256, 128 x 512, 16 x 2,176, 4 x 51,072 bytes (337 KB) |
| Particles | 0xf55d0 | 2,048 x 76 bytes |
| Stretch / shape particles, emitters | 0xf55d0 | 256 x 92, 128 x 108, 256 x 160 |
| Effects | stats 0xf7210 | 512 general, 256 particle effects, 13,200 graph constants |
| Material cache | 0x239900 | 173 x 224 bytes |
| Sound | A.8 | 64 voices, 128 emitters |

- Buffers of 64 KB or more go straight to `VirtualAlloc(MEM_COMMIT, PAGE_READWRITE)` (0x187750).
- Push buffer 2 MB / 32 KB kick-off (C.1). The game records no push buffers of its own (`CreatePushBuffer2`,
  `BeginPushBuffer`, `RunPushBuffer` have no game callers).
- Immediate drawing uses small static scratch buffers with `DrawVerticesUP`: particles 2,560 bytes (128 point sprites,
  0x133120, 0x136910), primitive drawer 64 primitives. The only dynamic vertex buffer is the water grid (0xc0e20).
- The one 128 MB check in the code (0x242e70) only enables a debug input journal; the game itself is budgeted for
  64 MB. The only explicit budget is the bundle-block quota; out of memory elsewhere is an assert or a null return.
- Executable image: debug 6.67 MB, release 3.08 MB (release: code 1.22 MB, data about 1.0 MB, uninitialised data
  0.42 MB). No large static arrays.
- Rough level budget that follows from the above: 27.5 MB bundles + 7.5 MB sounds + 2 MB push buffer + 3 MB image +
  about 3.7 MB of frame buffers (640x480: front, back, depth) leaves on the order of 20 MB for heap, pools and the
  kernel. This sum is an estimate, not a figure printed by the game.

---

## What the port should copy

Sound (the port today mixes up to 40 voices in software on the main thread at 48 kHz and decodes samples from disc at
first use):

1. **Take mixing off the main thread.** The original spends no main-thread time on samples at all. With no
   DirectSound in nxdk the nearest equivalent is to mix inside the audio callback / a dedicated audio thread and have
   the main thread only post start/stop/volume/position messages once per frame (their "deferred, commit once"
   pattern, 0xd7610). Driving the audio chip's hardware voices directly would be the true equivalent but needs a
   driver nxdk does not provide; not evaluated here.
2. **Load the whole sample bank at level load, never at first use.** Keep it compressed in memory (the original keeps
   7.5 MB of ADPCM resident; expanding to 16-bit PCM would be about four times that and does not fit the 64 MB
   budget), and decode inside the mixer as each voice plays.
3. **Mix cheaper sources.** The original's effects are mono 22,050 Hz with no pitch changes. Mono 22 kHz sources with
   two gains per voice are less than half the work of 48 kHz stereo sources; if the port's samples come at a higher
   rate, converting them once at load to 22,050 Hz mono matches the original's quality.
4. **Adopt their limits:** 64 voice slots, 32 sound events, only the 16 nearest 3D emitters audible and only inside
   their radius (min distance radius/8, max radius x 1.5), drop a new sound when no slot is free. The port's 40 mixed
   voices could become "16 positional + the 2D ones" with culling done before mixing.
5. **Music on its own thread:** read about 18 KB packets, keep about one second queued, sleep 8 ms between checks; no
   music file reads on the main thread.
6. Keep the "starved" guard: if the main thread stalls (loading), the audio thread keeps servicing output by itself.

Loading (the port converts each texture from PS2 format at first sight during play):

7. **No conversion during play.** The original stores textures and static vertex data in final GPU format and only
   registers them. For the port: convert offline on the PC (or at worst once at level load) into Xbox-ready data, so
   that at run time a texture is a header pointing into an already loaded block.
8. **One fixed block for level data** (they use 27.5 MB), filled like a stack at level load, zeroed/reset between
   levels, with a hard quota check. GPU data aligned to 128 bytes.
9. **Read files whole, unbuffered and sequential**, with data files padded to 2,048 bytes; one read per file.
10. Keep the loading screen single-threaded: redraw it from the reader's progress callback at most 30 times a second
    and run the sound update there too.
11. A hard-disk cache is optional: the original shipped the code but did not use it in this build. If added, copy
    their rules: per file on first use, checksum plus size/time check, wipe the cache on any doubt.

Frame (the port runs a variable-timestep loop with fixed 1/30 s sub-steps):

12. Keep the fixed 1/30 s step, and add their catch-up rule: **at most two steps per drawn frame, leftover time
    clamped to one step**, a drawn frame with zero steps allowed, no interpolation. This is what bounds a slow frame.
13. 640x480, 32-bit colour, D24S8, one back buffer, no multisampling, direct rendering to the back buffer, no
    full-screen effect targets.
14. Push buffer 2 MB with 32 KB kick-off; let the CPU run at most one frame ahead of the GPU (their fence before
    swap) instead of waiting for the GPU to go fully idle.
15. Cap gameplay at the step rate (30), menus and pause at 60.

Memory:

16. Plain heap plus fixed pools that refuse new objects when full (particles 2,048, effects 512, sound voices 64)
    instead of growing; allocations of 64 KB and more taken straight from the system.
17. Budget for 64 MB with their split as the reference: about 27.5 MB level data, 7.5 MB sound, 2 MB push buffer.

### Open points

- Release-build presentation interval (vsync or not) - not decompiled.
- Whether the release build enables the hard-disk cache.
- Whether any bundle is loaded asynchronously during play (the mechanism exists; no starter found).
- Whether monsters load from the `.zip` or the plain `.cmf`.
- Full versus light HRTF in the debug build.
