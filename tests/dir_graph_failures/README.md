# DirGraph allocation-failure regression

Copy this directory to `tests/dir_graph_failures/` only when the reader patch is integrated. The suite compiles the complete selected `director.c`, with tracked malloc/free used only inside that file. It also compiles the selected current moves, bundle, VOL and random sources. It never uses a cached object file.

Run after integration:

```sh
TMPDIR=/absolute/writable/private/output tests/dir_graph_failures/run.sh
TMPDIR=/absolute/writable/private/output tests/dir_graph_failures/run.sh --sanitize
```

`SOURCE_DIR` selects the repository `src` directory. `DIRECTOR_SOURCE_DIR` optionally selects a private director.c/director.h overlay while all other code remains current. No production source is changed. Each run creates a fresh retained directory under TMPDIR.

The default synthetic matrix uses 1, 2, 7, 31 and 255-state graphs. All seven allocation positions fail ten times per graph size, followed immediately by successful construction/use/free in the same process: 350 fault/recovery pairs. Four bounded malformed/absent cases retain the original rejection behavior, including zero allocation attempts for invalid short-header/count cases. No integer-wrap/corrupt-offset safety is claimed.

The complete current-source healthy reference differs only by removing the three known constructor checks, with exact-match assertions. Current source without those checks is rejected. Healthy array, search and AI-frame output hashes must match reference. The injected allocator initializes successful blocks to 0xA5 to make initially uninitialized search scratch deterministic in this comparison; gameplay is not asserted to use that byte pattern.

The main.c constructor assignment and frame dispatch are extracted on every run into minimal test structs. Counting wrappers call the actual AI and move-step routines. Tests cover every constructor failure, the existing have_ag=false ordinary move-step fallback, a successful AI entry, and the ev==0 short circuit. They also exercise the safe bounded dir_ai_destroy goal queue after failure. This is source-extracted boundary testing, not a complete Viewer/game integration run.

Optional real assets are read in place, never copied. Supply a JSON array containing the VOL path, member names, then any standalone Wii bundle paths:

```sh
DIR_GRAPH_ASSET_ARGS='["/path/RAMPAGE.VOL", "George.cmp", "Lizzie.cmp", "Ralph.cmp", "/path/Boris.cmg"]' \
TMPDIR=/absolute/writable/private/output tests/dir_graph_failures/run.sh --sanitize
```

Each real asset adds a healthy reference comparison and seven fault/recovery pairs. No proprietary game assets ship with the suite. The private validation report separately pins the four real assets used for the reviewed candidate.

Outputs include complete generated sources, input source hashes, caller excerpts, compiler/run commands and results. Existing compiler warnings in moves.c and vol.c are retained. ASan/UBSan runs disable LeakSanitizer because the execution environment does not support it. Zero owner/byte assertions cover only intercepted DirGraph allocations, not the entire process, external subsystems, GPU or audio.
