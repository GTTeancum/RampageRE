# Startup bundle ownership branches

Run `tests/startup_bundle_lifetimes/run.sh`, optionally with `--sanitize`.
Python 3 and a C compiler are required. Set `TMPDIR` to select scratch storage;
generated source, its SHA256 manifest and binary are retained.

The test extracts the current WorldGui/CommonGUI and Wii ShellData ownership
branches directly from `main.c`. Stub reads/loaders enforce the bundle loader's
success-only input transfer contract and independently count live allocations.
It checks nine missing/invalid/successful GUI input combinations and twelve Wii
presence/read/parse/book-open combinations. Later destructors must be safe, with
no retained allocations after teardown. Real bundle allocation-failure behavior
is covered separately by `tests/bundle_lifetimes`.

This does not run the application, graphics, audio, profile logic or all startup
failures. It makes no claim that full process-shutdown cleanup is complete.
