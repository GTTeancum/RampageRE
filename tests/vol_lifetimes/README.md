# Volume ownership and growth regression

Run `tests/vol_lifetimes/run.sh`, optionally with `--sanitize`. Requires a C
compiler with POSIX `fmemopen`. Set `TMPDIR` to choose scratch storage; binaries
are retained. No game data or actual archive files are used.

The test includes current production `src/vol.c`, with counted allocations,
64-byte allocation tail canaries, and counted in-memory FILE streams. It covers
all three volume-open allocations; failed open; short header/index/names reads;
non-image input; image lookup, reported size, seek, and embedded-header failures;
successful direct/embedded archives; initial/existing Wii-vector growth failure;
path allocation failure; safe retry within the same enumeration; capacity
overflow protection; and existing PS2-before-Wii lookup precedence.

Failed `vol_open` leaves zeroed state, no owned allocation, and no open FILE.
Failed growth preserves the pointer and capacity; enumeration still skips the
failed file and may retry capacity growth for the next one. This preserves
existing partial-enumeration behavior, not all-or-nothing archive registration.
The tests do not call `vol_add_wii` repeatedly with already populated overlays;
its independent local-capacity assumptions are not changed here.

This is not arbitrary malformed-file validation or a new general `vol_close`
API. Production shutdown ownership is separate. ASan leak detection defaults
off in restricted environments; explicit allocation/FILE counts run regardless.
