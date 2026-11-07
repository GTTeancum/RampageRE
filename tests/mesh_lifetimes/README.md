# Mesh ownership regression

Run `tests/mesh_lifetimes/run.sh`, optionally with `--sanitize`. A C compiler is
required. Set `TMPDIR` to select scratch storage; generated binaries are retained.
No game data, SDL, renderer, or Xbox toolchain is used.

The test includes current production `src/mesh.c` and intercepts its allocation
calls. It checks valid PS2 skinned parts, rejection before allocation, rejection
after partial allocation, rejected parts before/after valid parts, repeated
rejection, and Wii successful/failed display lists. Each of 8,000 cases returns to
zero tracked allocations after destruction. A second empty `mesh_free` is safe.
The padded short-stream fixture isolates cleanup from independent bounds issues.

This catches losing the rejected PS2 part's buffers when `nparts` is not advanced
and the next part overwrites its slot. A separate allocation-failure suite covers all required PS2/Wii attributes,
outer part storage, multi-part unwind, and first/subsequent Wii triangle-vector
allocation. Each OOM returns an empty Mesh and releases all successful earlier
allocations. Invalid-section skipping remains separate. It does not test arbitrary
malformed-file safety and makes no claim about GPU memory,
allocator fragmentation, real-Xbox memory footprint, or whole-game lifetimes.
