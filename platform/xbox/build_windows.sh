#!/usr/bin/env bash
# Build default.xbe on Windows against the installed nxdk (MSYS2 bash, installed LLVM).
#   C:/msys64/usr/bin/bash.exe --noprofile --norc platform/xbox/build_windows.sh [deps|game|all]
# Requirements: nxdk built at NXDK_DIR (default /c/nxdk), LLVM at LLVM_BIN, MSYS2 with
# make, git, patch and cmake (pacman -S cmake). The installed nxdk is never modified:
# the patched pbkit driver is compiled from a copy of its sources into lib/pbkit and
# selected through RAMPAGE_XBOX_PBKIT_LIBRARY.
# Output: platform/xbox/build/default.xbe. Dependencies live in platform/xbox/lib (ignored).
set -euo pipefail

NXDK_DIR="${NXDK_DIR:-/c/nxdk}"
LLVM_BIN="${LLVM_BIN:-/c/Program Files/LLVM/bin}"
export NXDK_DIR
export PATH="${LLVM_BIN}:${NXDK_DIR}/bin:/usr/bin:/mingw64/bin:${PATH}"

HERE="$(cd "$(dirname "$0")" && pwd)"
P="$HERE/patches"
L="$HERE/lib"
B="$HERE/build"
GLES_COMMIT=5a726e61f1af5c3e9682af2c833c9703cab5022e
GL4ES_COMMIT=81547d986798e876de8b434193920b606a72363f
stage="${1:-all}"
log() { echo "=== $*"; }

checkout() { # dir url commit
    [[ -d "$1/.git" ]] || git clone -q "$2" "$1"
    git -C "$1" fetch -q origin "$3" 2>/dev/null || true
    git -C "$1" checkout -q -f "$3"
    git -C "$1" reset -q --hard
    git -C "$1" clean -qfdx
    git -C "$1" submodule update --init --recursive -q
}
apply() { # dir patch
    log "patch $(basename "$2")"
    git -C "$1" apply --check "$2"
    git -C "$1" apply "$2"
}

if [[ $stage == deps || $stage == all ]]; then
    mkdir -p "$L"
    # pbkit: a private patched copy of the installed nxdk's driver sources
    rm -rf "$L/pbkit-src"; mkdir -p "$L/pbkit-src/lib"
    cp -r "$NXDK_DIR/lib/pbkit" "$L/pbkit-src/lib/pbkit"
    rm -f "$L/pbkit-src/lib/pbkit/"*.obj "$L/pbkit-src/lib/pbkit/"*.d
    git -C "$L/pbkit-src" init -q .
    for p in startup-ready-bounds dpc-scanout-fallback reset-drain-lifecycle kick-batching; do
        apply "$L/pbkit-src" "$P/nxdk-pbkit-$p.patch"
    done
    log "build patched libpbkit.lib"
    objs=()
    for s in pbkit pbkit_gamma pbkit_dma pbkit_draw pbkit_print pbkit_pushbuffer; do
        nxdk-cc -O2 -I"$L/pbkit-src/lib/pbkit" -I"$L/pbkit-src/lib" \
            -c "$L/pbkit-src/lib/pbkit/$s.c" -o "$L/pbkit-src/lib/pbkit/$s.obj"
        objs+=("$L/pbkit-src/lib/pbkit/$s.obj")
    done
    rm -f "$L/libpbkit.lib"
    nxdk-lib -out:"$L/libpbkit.lib" "${objs[@]}"

    checkout "$L/nxdk-gles11" https://github.com/Ryzee119/nxdk-gles11.git $GLES_COMMIT
    for p in default-state staging-pages lighting-normal light-cache texture-telemetry exact-palette reset-drain pushbuffer-room; do
        apply "$L/nxdk-gles11" "$P/nxdk-gles11-$p.patch"
    done
    # nxdk-gles11 includes <pbkit/...>; point it at the patched driver headers first
    checkout "$L/gl4es" https://github.com/ptitSeb/gl4es.git $GL4ES_COMMIT
    apply "$L/gl4es" "$P/gl4es-light-enable-ownership.patch"
fi

if [[ $stage == game || $stage == all ]]; then
    rm -rf "$B"
    log configure
    /usr/bin/cmake -G "Unix Makefiles" -S "$HERE" -B "$B" \
        -DCMAKE_TOOLCHAIN_FILE="$NXDK_DIR/share/toolchain-nxdk.cmake" \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_FLAGS="-I$L/pbkit-src/lib" \
        -DCMAKE_EXE_LINKER_FLAGS="-map:$B/rampage_xbox.map" \
        -DNXDK_GLES11_DIR="$L/nxdk-gles11" \
        -DFETCHCONTENT_SOURCE_DIR_GL4ES="$L/gl4es" \
        -DRAMPAGE_XBOX_EXACT_PALETTE=ON \
        -DRAMPAGE_XBOX_PBKIT_LIBRARY="$L/libpbkit.lib"
    log build
    /usr/bin/cmake --build "$B" -j"$(nproc)" --target rampage_xbox
    ls -la "$B/default.xbe"
    sha256sum "$B/default.xbe"
fi
log done
