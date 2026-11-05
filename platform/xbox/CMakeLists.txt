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
if(NOT DEFINED NXDK_GLES11_DIR OR NOT EXISTS "${NXDK_GLES11_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "Set NXDK_GLES11_DIR to an nxdk-gles11 checkout")
endif()
set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(NXDK_GLES11_WITH_GL4ES ON CACHE BOOL "" FORCE)
add_subdirectory("${NXDK_GLES11_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/nxdk-gles11")
# gl4es shim atof must not reach nxdk's assertion-only strtod.
target_compile_definitions(GLESv1_CM PRIVATE strtod=plat_xbox_strtod)
if(TARGET GL)
  target_compile_definitions(GL PRIVATE NO_INIT_CONSTRUCTOR _WINGDI_)
endif()

set(GAME_SOURCES
 main.c shot.c render_gl.c city.c vol.c bundle.c tex.c mesh.c prb.c tree.c anim.c camera.c moves.c
 building.c climb.c ik.c people.c fx.c cloth.c sound.c army.c hud.c vehicle.c water.c edible.c game.c
 boss.c look.c shadow.c director.c footstep.c challenge.c reveal.c rail.c parade.c streetobj.c profile.c
 collide.c shell.c movie.c wrand.c texpack_xbox.c iso.c plat_xbox.c xbox_libc.c rounding_xbox.c pad_xbox.c win_xbox.c settings.c postfx_xbox.c autoplay.c)
list(TRANSFORM GAME_SOURCES PREPEND "${ROOT}/src/")
add_executable(rampage_xbox ${GAME_SOURCES})
set_property(TARGET rampage_xbox PROPERTY C_STANDARD 99)
target_compile_definitions(rampage_xbox PRIVATE RAMPAGE_XBOX RAMPAGE_LOW_MEMORY XBOX _WINGDI_)
target_compile_options(rampage_xbox PRIVATE -fsigned-char)
target_include_directories(rampage_xbox PRIVATE "${ROOT}/src" "${NXDK_DIR}/lib/sdl/SDL2/include")
target_link_options(rampage_xbox PRIVATE "-include:_automount_d_drive")
target_link_libraries(rampage_xbox PRIVATE "${NXDK_DIR}/lib/libnxdk_automount_d.lib" GL GLESv1_CM "${NXDK_DIR}/lib/libSDL2.lib" "${NXDK_DIR}/lib/libpbkit.lib")
add_custom_command(TARGET rampage_xbox POST_BUILD
 COMMAND "${NXDK_DIR}/tools/cxbe/cxbe" "-OUT:${CMAKE_CURRENT_BINARY_DIR}/default.xbe" "-TITLE:RampageRE" "$<TARGET_FILE:rampage_xbox>"
 VERBATIM COMMENT "Converting the Xbox executable to XBE")
