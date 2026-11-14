cmake_minimum_required(VERSION 3.18)
project(RampageXbox LANGUAGES C)

# nxdk's Generic CMake toolchain leaves Release flags empty by default.
# Use conventional optimization without unsafe floating-point transformations.
add_compile_options(-O2)

# Configure through nxdk-cmake after building nxdk's SDL2 and pbkit libraries.
# Keep game assets outside this source tree and out of the executable.
if(NOT DEFINED ENV{NXDK_DIR})
  message(FATAL_ERROR "NXDK_DIR must name an initialized nxdk checkout")
endif()
set(NXDK_DIR "$ENV{NXDK_DIR}")
# Keep the installed SDK unchanged when selecting a reviewed private driver.
set(RAMPAGE_XBOX_PBKIT_LIBRARY "${NXDK_DIR}/lib/libpbkit.lib" CACHE FILEPATH
    "pbkit archive to link (must match the selected GLES reset-drain ABI)")
if(NOT EXISTS "${RAMPAGE_XBOX_PBKIT_LIBRARY}")
  message(FATAL_ERROR "RAMPAGE_XBOX_PBKIT_LIBRARY must name an existing pbkit archive")
endif()
if(NOT DEFINED NXDK_GLES11_DIR OR NOT EXISTS "${NXDK_GLES11_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "Set NXDK_GLES11_DIR to an nxdk-gles11 checkout")
endif()
set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
# nxdk compiles with -fno-builtin: every small memcpy and every sqrtf is then a library call. The header names the
# compiler builtins again, for the game and for gl4es and nxdk-gles11 below (src/xbox_builtins.h).
add_compile_options(-include "${ROOT}/src/xbox_builtins.h" -fno-math-errno)
set(NXDK_GLES11_WITH_GL4ES ON CACHE BOOL "" FORCE)
add_subdirectory("${NXDK_GLES11_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/nxdk-gles11")
# gl4es shim atof must not reach nxdk's assertion-only strtod.
target_compile_definitions(GLESv1_CM PRIVATE strtod=plat_xbox_strtod)
# The driver's arena for vertex arrays that are not in the GPU's static store: two frames' worth. The game's frames
# stage about 100 KiB at most (a test build logs "xbox staging frame_peak_kb"), so 768 KiB in place of the driver's
# 2 MiB; the stock console's 64 MiB need the rest for 1280 x 720.
target_compile_definitions(GLESv1_CM PRIVATE GLI_STAGING_ARENA_SIZE=786432)
if(TARGET GL)
  target_compile_definitions(GL PRIVATE NO_INIT_CONSTRUCTOR _WINGDI_)
endif()

set(GAME_SOURCES
 main.c shot.c render_gl.c city.c vol.c bundle.c tex.c mesh.c prb.c tree.c anim.c camera.c moves.c
 building.c climb.c ik.c people.c fx.c cloth.c sound.c army.c hud.c vehicle.c water.c edible.c game.c
 boss.c look.c shadow.c director.c footstep.c challenge.c reveal.c rail.c parade.c streetobj.c profile.c
 collide.c shell.c movie.c wrand.c texpack_xbox.c iso.c plat_xbox.c xbox_libc.c rounding_xbox.c audio_xbox_hal.c pad_xbox.c win_xbox.c settings.c postfx_xbox.c autoplay.c xbox_fastmem.c)
list(TRANSFORM GAME_SOURCES PREPEND "${ROOT}/src/")
add_executable(rampage_xbox ${GAME_SOURCES})
# The fast libc replacements must not be compiled back into calls to themselves.
set_source_files_properties("${ROOT}/src/xbox_fastmem.c" PROPERTIES COMPILE_OPTIONS "-fno-builtin")
# Private integration candidate: enable only with the reviewed exact-palette backend.
option(RAMPAGE_XBOX_EXACT_PALETTE "Use exact P8 storage for eligible immutable world textures" OFF)
if(RAMPAGE_XBOX_EXACT_PALETTE)
  if(NOT EXISTS "${NXDK_GLES11_DIR}/gles_palette_codec.h")
    message(FATAL_ERROR "Exact palette storage requires the separately reviewed backend patch")
  endif()
  target_compile_definitions(rampage_xbox PRIVATE RAMPAGE_XBOX_EXACT_PALETTE)
endif()
# Test builds only: a fixed command line (an XBE gets none), e.g. "Chicago 1 --players 4 --autoplay".
set(RAMPAGE_XBOX_TEST_ARGS "" CACHE STRING "Built-in command line for unattended test builds (empty: the front end)")
if(RAMPAGE_XBOX_TEST_ARGS)
  target_compile_definitions(rampage_xbox PRIVATE "RAMPAGE_XBOX_TEST_ARGS=\"${RAMPAGE_XBOX_TEST_ARGS}\"")
  # test builds log the driver's texture memory too (gliTextureMemoryStats)
  target_compile_definitions(GLESv1_CM PRIVATE RAMPAGE_XBOX_TEXTURE_TELEMETRY)
endif()
set_property(TARGET rampage_xbox PROPERTY C_STANDARD 99)
target_compile_definitions(rampage_xbox PRIVATE RAMPAGE_XBOX RAMPAGE_LOW_MEMORY XBOX _WINGDI_)
target_compile_options(rampage_xbox PRIVATE -fsigned-char -fno-math-errno)
# fminf / fmaxf as compares, not three library calls each (src/xbox_math.h)
target_compile_options(rampage_xbox PRIVATE "SHELL:-include \"${ROOT}/src/xbox_math.h\"")
target_include_directories(rampage_xbox PRIVATE "${ROOT}/src" "${NXDK_DIR}/lib/sdl/SDL2/include")
target_link_options(rampage_xbox PRIVATE "-include:_automount_d_drive")
target_link_libraries(rampage_xbox PRIVATE "${NXDK_DIR}/lib/libnxdk_automount_d.lib" GL GLESv1_CM "${NXDK_DIR}/lib/libSDL2.lib" "${RAMPAGE_XBOX_PBKIT_LIBRARY}")
add_custom_command(TARGET rampage_xbox POST_BUILD
 COMMAND "${NXDK_DIR}/tools/cxbe/cxbe" "-OUT:${CMAKE_CURRENT_BINARY_DIR}/default.xbe" "-TITLE:RampageRE" "$<TARGET_FILE:rampage_xbox>"
 VERBATIM COMMENT "Converting the Xbox executable to XBE")
