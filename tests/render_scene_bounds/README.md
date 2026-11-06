# Renderer scene-bound regression

From the repository root:

```sh
./tests/render_scene_bounds/run.sh
./tests/render_scene_bounds/run.sh --sanitize
```

Requires Python 3, a POSIX shell and a host C compiler (`CC`, default `cc`). No game assets, OpenGL headers/context, nxdk toolchain or emulator are needed. The script resolves its checkout relative to itself, so it can also be invoked from another working directory. It uses a fresh temporary directory and deletes build outputs on exit; set `KEEP_TEST_OUTPUT=1` to retain generated source, extraction manifests, binaries and logs.

## Coverage

The current `src/render_gl.c` is tested with the current project headers. The suite executes 13 cases / 670 assertions in host, Xbox+low-memory and GLES+low-memory macro configurations:

- Fixed `MeshPart` and vertex storage/count reused across scenes: outside-to-inside and inside-to-outside changes, plus both unchanged controls, using both the same and different `Scene` addresses.
- Comparison with a fresh renderer and explicit expected visibility.
- Retained allocation pointer/capacity and no additional reallocation when resetting/rebuilding within capacity.
- Fresh, null, smaller, empty and regrown scenes; genuine capacity growth from one mesh to four.
- Radius-only geometry replacement, zero capacity with a nonnull allocation, stable-scene cache hits and skinned-item bypass.

The extractor compiles exact complete definitions of the renderer's private types, `rnd_create`, texture/buffer teardown, `rnd_scene`, `rnd_free` and `item_seen`. It does not implement replacement cache or culling logic. Each extracted span's original line range and SHA-256 is written to the temporary manifest. Extraction fails explicitly if a definition cannot be uniquely identified; review source changes before updating its patterns.

Only GL boundary typedefs and `glDeleteTextures`/`glFinish` are stubbed. A transparent libc `realloc` wrapper counts calls. Texture and GPU-buffer caches are empty. This is a focused host-ABI correctness regression, not a full renderer link, nxdk build, graphics-driver test or rendered gameplay check. Changes to geometry within one scene without `rnd_scene` are outside this fix's scope. Allocation-failure handling is not covered.

`--sanitize` adds AddressSanitizer and UndefinedBehaviorSanitizer. Leak detection defaults off because LeakSanitizer cannot operate under ptrace-based execution environments. On a compatible host, use `ASAN_OPTIONS=detect_leaks=1 ./tests/render_scene_bounds/run.sh --sanitize`. A run with leak detection disabled is not a leak check.

The command tests only the current checkout and should exit zero. It does not require a frozen historical baseline or external artifact directory. Historical baseline-failure evidence is intentionally kept outside this repository.
