cmake_minimum_required(VERSION 3.10)

## Esto incluye el toolchain de Vita, debe ir antes de project()
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
# ID unico de 9 caracteres. Distinto del de Zenonia 2 (PSVZ00002) para poder
# tener ambos juegos instalados a la vez.
set(VITA_TITLEID  "PSVZ00003")
set(VITA_VERSION  "01.00")

## Log verbose de cada llamada JNI de FalsoJNI. Imprescindible para el
## primer arranque (Fase 3 del plan: metodologia de "un log a la vez"), pero
## debe apagarse antes de medir rendimiento real -- cada linea implica un
## sceIoOpen+sceIoWrite+sceIoClose (ver Fixes_Log de Prince of Persia
## #12-13, y plan_zenonia3_port.md seccion de riesgos heredados).
## Apagado desde que el juego llega al menu: con el nivel 0, el 79% del log
## eran lineas [JNI][...] (3579 de 4520 en log_1783657108) y el arranque
## tardaba minutos por el fflush por linea. Con nivel 2 (WARN) siguen
## visibles [JNI WARN]/[JNI ERR], que son los que diagnostican bugs reales.
## Reactivar (=0) solo si hace falta rastrear una llamada JNI puntual.
option(ENABLE_VERBOSE_JNI_LOG "Log every FalsoJNI call (debug only, slow)" OFF)
if (ENABLE_VERBOSE_JNI_LOG)
  add_definitions(-DFALSOJNI_DEBUGLEVEL=0)
else()
  add_definitions(-DFALSOJNI_DEBUGLEVEL=2)
endif()

## Oculta la cruceta nativa + sus hasta 5 botones de accion
## (GVUIPlayerController, la unica instancia de esa clase en todo el juego --
## ver decompiled_so/out_ghidra.c:28609/28466 y la nota completa en
## loader/dynlib.c junto a zenonia_install_hide_dpad_hook()). Se hookea su
## constructor y se pone en 0 el contador interno de "objetos activos" que
## GVUIController::Draw()/PointerPress()/Move()/Release() usan como limite de
## su loop -- no dibuja NI recibe touch, sin tocar ningun otro controller
## (barra de vida/mana, minimapa, etc., cada uno con su propia instancia). La
## Vita ya tiene D-Pad/botones fisicos mapeados 1:1 (ver btn_map en
## loader/main.c), asi que no se pierde ninguna funcionalidad real.
## Para reactivar la cruceta/botones en pantalla: pasar
## -DHIDE_VIRTUAL_GAMEPAD=OFF a cmake (o cambiar el default de abajo a OFF) y
## recompilar.
option(HIDE_VIRTUAL_GAMEPAD "Hide the game's on-screen D-pad + action button cluster (Dpad.pzx/illusiaDpad.pzx); physical Vita buttons are already mapped" ON)
if (HIDE_VIRTUAL_GAMEPAD)
  add_definitions(-DZENONIA_HIDE_DPAD_UI=1)
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
# ATTRIBUTE2=12 pide el presupuesto de memoria extendido -- sin esto,
# SceShaccCg (compilador de shaders Cg de vitaGL) puede fallar al iniciar.
# Copiado de la config confirmada funcionando en Zenonia 2 / Prince of Persia.
set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d PARENTAL_LEVEL=1 -d ATTRIBUTE2=12")

include_directories(
  ./common # para debugScreenPrintf()
  ./lib    # para <falso_jni/FalsoJNI.h>
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

target_link_libraries(${PROJECT_NAME}
  vitaGL
  vitashark
  mathneon
  m
  # Decodificador OGG Vorbis (Tremor, punto fijo) para loader/audio.c
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
  # Fuente TTF para el rasterizado GFA (loader/font.c): NanumGothic, licencia
  # OFL (cobertura Hangul completa + Latin -- el juego setea strings coreanos)
  FILE font.ttf font.ttf
  # Overlays Android (logo/titulo/menu) que el .so nativo no dibuja -- ver
  # loader/androidui.c. PNG reales del APK (variantes "globales"/en ingles),
  # empaquetados TAL CUAL bajo drawable/ y decodificados en runtime con
  # stb_image -- no requieren ningun paso de conversion previo.
  FILE zenonia3/res/drawable/ui_logo_gamevil.png drawable/ui_logo_gamevil.png
  FILE zenonia3/res/drawable/ui_title_bg_nate.png drawable/ui_title_bg_nate.png
  FILE zenonia3/res/drawable/ui_title_logo5.png drawable/ui_title_logo5.png
  FILE zenonia3/res/drawable/ui_menu_back0.png drawable/ui_menu_back0.png
  FILE zenonia3/res/drawable/ui_menu_back1.png drawable/ui_menu_back1.png
  FILE zenonia3/res/drawable/ui_menu_newgame.png drawable/ui_menu_newgame.png
  FILE zenonia3/res/drawable/ui_menu_continue.png drawable/ui_menu_continue.png
  FILE zenonia3/res/drawable/ui_menu_options.png drawable/ui_menu_options.png
  FILE zenonia3/res/drawable/ui_menu_help.png drawable/ui_menu_help.png
  FILE zenonia3/res/drawable/ui_menu_about.png drawable/ui_menu_about.png
  FILE zenonia3/res/drawable/ui_menu_community.png drawable/ui_menu_community.png
  FILE zenonia3/res/drawable/ui_about_bg.png drawable/ui_about_bg.png
  FILE zenonia3/res/drawable/ui_help_bg.png drawable/ui_help_bg.png
  FILE zenonia3/res/drawable/ui_menu_back.png drawable/ui_menu_back.png
  FILE zenonia3/res/drawable/reply_page_back_e.png drawable/reply_page_back_e.png
  FILE zenonia3/res/drawable/button_write_01_global.png drawable/button_write_01_global.png
  FILE zenonia3/res/drawable/button_later_01_global.png drawable/button_later_01_global.png
  # HTML reales del APK original que Android mostraba en un WebView sobre
  # ABOUT/HELP -- parseados a texto plano y rasterizados en runtime por
  # loader/htmltext.c/htmlview.c (ver androidui.c). Empaquetados tal cual,
  # sin conversion (son texto plano).
  FILE zenonia3/assets/html/about.html html/about.html
  FILE zenonia3/assets/html/help_eng.html html/help_eng.html
)

## VPK seguro (libre de distribucion, sin assets con copyright)
vita_create_vpk(${PROJECT_NAME}_safe_dist.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE sce_sys/pic0.png sce_sys/pic0.png
  FILE sce_sys/livearea/contents/bg0.png sce_sys/livearea/contents/bg0.png
  FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
  FILE font.ttf font.ttf
)
