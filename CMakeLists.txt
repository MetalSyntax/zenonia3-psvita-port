cmake_minimum_required(VERSION 3.10)

## @brief Esto incluye el toolchain de Vita, debe ir antes de project().
## @note See `docs/CMakeLists.md:3` for detailed design rationale.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(zenonia_3)
include("${VITASDK}/share/vita.cmake" REQUIRED)

## Configuracion de la app
set(VITA_APP_NAME "Zenonia 3")
## @brief Esto incluye el toolchain de Vita, debe ir antes de project().
## @note See `docs/CMakeLists.md:18` for detailed design rationale.
set(VITA_TITLEID  "PSVZ00003")
set(VITA_VERSION  "01.00")

## @brief Esto incluye el toolchain de Vita, debe ir antes de project().
## @note See `docs/CMakeLists.md:23` for detailed design rationale.
option(ENABLE_VERBOSE_JNI_LOG "Log every FalsoJNI call (debug only, slow)" OFF)
if (ENABLE_VERBOSE_JNI_LOG)
  add_definitions(-DFALSOJNI_DEBUGLEVEL=0)
else()
  add_definitions(-DFALSOJNI_DEBUGLEVEL=2)
endif()

## @brief Esto incluye el toolchain de Vita, debe ir antes de project().
## @note See `docs/CMakeLists.md:32` for detailed design rationale.
option(HIDE_VIRTUAL_GAMEPAD "Hide the game's on-screen D-pad + action button cluster (Dpad.pzx/illusiaDpad.pzx); physical Vita buttons are already mapped" ON)
if (HIDE_VIRTUAL_GAMEPAD)
  add_definitions(-DZENONIA_HIDE_DPAD_UI=1)
endif()

## @brief Esto incluye el toolchain de Vita, debe ir antes de project().
## @note See `docs/CMakeLists.md:39` for detailed design rationale.
set(RGB565_CONVERT_MODE "NATIVE" CACHE STRING "RGB565->RGBA8888 conversion strategy: SCALAR, LUT, NEON o NATIVE (perf A/B test)")
set_property(CACHE RGB565_CONVERT_MODE PROPERTY STRINGS SCALAR LUT NEON NATIVE)
if (RGB565_CONVERT_MODE STREQUAL "SCALAR")
  add_definitions(-DRGB565_MODE_SCALAR=1)
elseif (RGB565_CONVERT_MODE STREQUAL "LUT")
  add_definitions(-DRGB565_MODE_LUT=1)
elseif (RGB565_CONVERT_MODE STREQUAL "NEON")
  add_definitions(-DRGB565_MODE_NEON=1)
elseif (RGB565_CONVERT_MODE STREQUAL "NATIVE")
  add_definitions(-DRGB565_MODE_NATIVE=1)
else()
  message(FATAL_ERROR "RGB565_CONVERT_MODE debe ser SCALAR, LUT, NEON o NATIVE (valor recibido: ${RGB565_CONVERT_MODE})")
endif()

## @brief Esto incluye el toolchain de Vita, debe ir antes de project().
## @note See `docs/CMakeLists.md:55` for detailed design rationale.
option(OPTIMIZE_NEON_FIXED "Use NEON SIMD instead of scalar division for GL_FIXED (Q16.16) vertex/color/texcoord conversion (perf A/B test)" OFF)
if (OPTIMIZE_NEON_FIXED)
  add_definitions(-DOPTIMIZE_NEON_FIXED=1)
endif()

## @brief Esto incluye el toolchain de Vita, debe ir antes de project().
## @note See `docs/CMakeLists.md:62` for detailed design rationale.
option(LOCK_FPS_30 "Cap framerate at a stable 30fps (2 vblanks) instead of vitaGL's default single-vblank vsync -- fixes the ~40-60fps jitter seen with RGB565_CONVERT_MODE=NATIVE" ON)
if (LOCK_FPS_30)
  add_definitions(-DLOCK_FPS_30=1)
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
## @brief Prueba A/B de rendimiento (ver CLAUDE.
## @note See `docs/CMakeLists.md:71` for detailed design rationale.
set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d PARENTAL_LEVEL=1 -d ATTRIBUTE2=12")

include_directories(
  ./common # para debugScreenPrintf()
  ./lib    # para <falso_jni/FalsoJNI.h>
  ./lib/vitagl/source # vitaGL vendorizado (ver bloque vitaGL_lib mas abajo), antes del vitaGL.h del sysroot de vitasdk
)

link_directories(
  ${CMAKE_CURRENT_BINARY_DIR}
)

add_executable(${PROJECT_NAME}
  ./loader/main.c
  ./loader/dynlib.c
  ./loader/so_util.c
  ./loader/java.c
  ./loader/audio.c
  ./loader/font.c
  ./loader/htmltext.c
  ./loader/htmlview.c
  ./loader/androidui.c
  ./common/debugScreen.c
  ./lib/falso_jni/FalsoJNI.c
  ./lib/falso_jni/FalsoJNI_ImplBridge.c
  ./lib/falso_jni/FalsoJNI_Logger.c
)

## vitaGL vendorizado en lib/vitagl/ (submodulo de Rinnegatamante/vitaGL) en
## vez del libvitaGL.a global de vitasdk: la version prebuilt del SDK no
## permite elegir flags de compilacion, y NO_SPLASHSCREEN=1 (-DSKIP_SPLASHSCREEN)
## es justamente uno de esos flags -- quita el logo animado de vitaGL al
## bootear (splashscreen.c), que tapaba el logo de Gamevil del juego.
## Mismo patron ya probado en el port hermano Dungeon-Hunter-2-vita.
##
## SOFTFP_ABI=1 es OBLIGATORIO: el toolchain de este vitasdk (mirror softfp)
## compila el loader con -mfloat-abi=softfp, y el libvitaGL.a prebuilt que
## funcionaba antes de vendorizar venia construido con ese ABI. Sin el flag,
## vitaGL no define HAVE_SOFTFP_ABI y toma caminos de codigo incompatibles ->
## pantalla negra permanente desde el arranque (sonido/input OK, cero crashes).
## Misma causa raiz confirmada en consola en Zenonia4-vita (port_progress.md
## Fase 124); alli el parche de system_app_mode NO alcanzaba por si solo.
set(VITAGL_MAKE_FLAGS_STR "SOFTFP_ABI=1 NO_SPLASHSCREEN=1")
separate_arguments(VITAGL_MAKE_FLAGS UNIX_COMMAND "${VITAGL_MAKE_FLAGS_STR}")
set(VITAGL_STAMP "${CMAKE_BINARY_DIR}/vitagl.stamp")

## @note custom_target (no add_custom_command con OUTPUT) para que esto se
## reevalue en cada build -- un stamp basado en OUTPUT sin DEPENDS solo corre
## una vez por arbol de build y no vuelve a disparar si VITAGL_MAKE_FLAGS_STR
## cambia despues. build_vitagl.sh compara contra VITAGL_STAMP y solo hace
## `make clean` cuando los flags realmente cambiaron (si no, make de abajo
## es incremental).
add_custom_target(vitaGL_lib
  COMMAND rm -f "${VITAGL_STAMP}.tmp"
  COMMAND echo "${VITAGL_MAKE_FLAGS_STR}" > "${VITAGL_STAMP}.tmp"
  COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/build_vitagl.sh" "${VITAGL_MAKE_FLAGS_STR}" "${VITAGL_STAMP}" "${CMAKE_SOURCE_DIR}/lib/vitagl" "${CMAKE_SOURCE_DIR}/patches"
  COMMAND make -C "${CMAKE_SOURCE_DIR}/lib/vitagl" ${VITAGL_MAKE_FLAGS}
  BYPRODUCTS "${CMAKE_SOURCE_DIR}/lib/vitagl/libvitaGL.a"
  COMMENT "Building vitaGL with ${VITAGL_MAKE_FLAGS_STR}..."
)
add_dependencies(${PROJECT_NAME} vitaGL_lib)

## vitashark (Rinnegatamante/vitaShaRK) tambien vendorizado como submodulo,
## por la misma razon que vitaGL: el libvitashark.a prebuilt de vdpm viene
## del mismo mirror stale que el toolchain base (vitasdk-softfp/packages, sin
## builds nuevos desde 2024-04-25) y le falta shark_set_shader_association_path,
## que el vitaGL vendorizado de arriba SI llama (vgl.c) -- sin esto el link
## final tira "undefined reference".
add_custom_target(vitashark_lib
  COMMAND make -C "${CMAKE_SOURCE_DIR}/lib/vitashark"
  BYPRODUCTS "${CMAKE_SOURCE_DIR}/lib/vitashark/libvitashark.a"
  COMMENT "Building vitaShaRK..."
)
add_dependencies(${PROJECT_NAME} vitashark_lib)

target_link_libraries(${PROJECT_NAME}
  "${CMAKE_SOURCE_DIR}/lib/vitagl/libvitaGL.a"
## @brief vitaGL trae un preprocesador de shaders en C++ (expression.cpp):
## aunque el resto del proyecto es C, hace falta el runtime de C++ (new/
## delete, std::string, __cxa_*/__gxx_personality_v0) para resolverlo.
## Ver el comentario junto a game_cxa_type_match en loader/dynlib.c sobre
## por que los stubs de C++ del shim del juego NO usan los nombres reales.
  stdc++
  "${CMAKE_SOURCE_DIR}/lib/vitashark/libvitashark.a"
  mathneon
  m
## @brief Intrinsics NEON (vld1q_s32/vcvtq_f32_s32/vmulq_f32) en vez del loop escalar para la conversion GL_FIXED (Q16.
## @note See `docs/CMakeLists.md:105` for detailed design rationale.
  vorbisidec
  ogg
  -Wl,--whole-archive pthread -Wl,--no-whole-archive
  SceDisplay_stub
  SceCtrl_stub
  SceTouch_stub
  SceAudio_stub
  SceAudioIn_stub
  SceAppMgr_stub
  SceAppUtil_stub
  SceSysmodule_stub
  ScePower_stub
  SceGxm_stub
  SceShaccCg_stub
  SceShaccCgExt
  SceCommonDialog_stub
  SceKernelDmacMgr_stub
  taihen_stub
  kubridge_stub
)

## Crear archivos de Vita
vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE NOASLR)
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE sce_sys/pic0.png sce_sys/pic0.png
  FILE sce_sys/livearea/contents/bg0.png sce_sys/livearea/contents/bg0.png
  FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
## @brief Decodificador OGG Vorbis (Tremor, punto fijo) para loader/audio.
## @note See `docs/CMakeLists.md:138` for detailed design rationale.
  FILE font.ttf font.ttf
## @brief Decodificador OGG Vorbis (Tremor, punto fijo) para loader/audio.
## @note See `docs/CMakeLists.md:141` for detailed design rationale.
)
