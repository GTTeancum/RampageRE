# Bundle allocation-failure regression

Run `tests/bundle_lifetimes/run.sh`, optionally with `--sanitize`. Set `TMPDIR`
for scratch storage; generated binaries are retained. This needs a C compiler,
not game assets, SDL, a renderer, or the Xbox toolchain.

The fixture includes current production `src/bundle.c` with a counting/failing
allocator. It covers all five metadata allocations, Wii conversion-map
allocation, the book walk's work/seen allocations, and work-vector growth.
Successful PS2 and Wii conversion matches pre-fix complete-buffer hashes.
Two-entry fixtures also fail after an earlier material was converted, proving
that partial Wii conversion does not transfer ownership or report success.

Across 2,900 injected allocation failures, output Bundle metadata is cleared,
the original input remains owned by the caller, and a caller free plus two safe
empty `bdp_free` calls leave zero tracked allocations. Invalid headers receive
the same ownership contract. The work graph forces growth after 65 queued
entries and catches falsely doubling capacity on failed realloc.

This is allocation ownership/failure coverage, not arbitrary malformed-file
validation. Failed Wii input may have been modified and must be discarded rather
than retried. GPU memory, libc retention and Xbox heap fragmentation are not
measured. ASan leak detection defaults off for restricted environments; explicit
allocation accounting still runs in normal and sanitized tests.
