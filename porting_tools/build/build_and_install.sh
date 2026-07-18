#!/bin/bash
set -e

# Configuración
PROJECT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="/tmp/zn3-build"
SRC_DIR="/tmp/zn3-src"
VPK_NAME="zenonia_3.vpk"

echo "================================================================"
echo "  Script de Build Automatico para Zenonia 3 (PS Vita)"
echo "================================================================"

echo "[1/3] Preparando entorno de compilacion..."
# Evitamos el bug de vita-pack-vpk con rutas que contienen espacios
# ("PSVITA Develop") usando un directorio temporal en /tmp -- ver
# PORTING_PLAN.md Fase 0 / psvita-porting skill, toolchain_gotchas.md.
mkdir -p "$BUILD_DIR"
mkdir -p "$SRC_DIR"

if [ -z "$VITASDK" ]; then
    if [ -d "/usr/local/vitasdk" ]; then
        export VITASDK="/usr/local/vitasdk"
    elif [ -d "$HOME/vitasdk" ]; then
        export VITASDK="$HOME/vitasdk"
    else
        echo "Error: La variable de entorno VITASDK no esta definida y no se encontro en rutas por defecto."
        exit 1
    fi
    export PATH="$VITASDK/bin:$PATH"
fi

# Excluye todo lo que .gitignore ya marca como derivado/propietario (apk
# original, extraccion, decompilados, dumps de la app instalada) ademas del
# historial de git y builds viejos.
rsync -a \
    --exclude '.git' --exclude 'build' --exclude '.*' \
    --exclude 'decompiled' \
    --exclude 'com.*' \
    --exclude '*.apk' --exclude '*.zip' \
    "$PROJECT_DIR/" "$SRC_DIR/"

echo "[2/3] Ejecutando CMake y Make..."
cd "$BUILD_DIR"

read -p "¿Build de depuracion (logging detallado, DEBUG_SOLOADER)? [S/n] " DEBUG_OPTION
if [[ "$DEBUG_OPTION" =~ ^[nN]$ ]]; then
    BUILD_TYPE="Release"
else
    BUILD_TYPE="Debug"
fi

# Prueba A/B de rendimiento (ver CLAUDE.md seccion de optimizaciones /
# CMakeLists.txt). RGB565_CONVERT_MODE y OPTIMIZE_NEON_FIXED son 2 ejes
# independientes -- pasarlos siempre explicitos por -D pisa la cache de CMake
# de $BUILD_DIR sin necesidad de borrarla (a diferencia de cambiar el default
# en CMakeLists.txt sin -D, que option() cachea y no recoge -- ver "Leccion
# de build" en port_progress.md).
echo ""
echo "Modo de conversion RGB565->RGBA8888 (framebuffer de software, prueba A/B):"
echo "  1) SCALAR - division entera por pixel (baseline original)"
echo "  2) LUT    - tabla precomputada, mismo resultado exacto (default)"
echo "  3) NEON   - SIMD con expansion de bits (~1 LSB de diferencia visual, imperceptible)"
echo "  4) NATIVE - sin conversion, sube RGB565 directo a la GPU (SIN CONFIRMAR EN HARDWARE)"
read -p "Opcion [1-4, default 2]: " RGB565_CHOICE
case "$RGB565_CHOICE" in
    1) RGB565_MODE="SCALAR" ;;
    3) RGB565_MODE="NEON" ;;
    4) RGB565_MODE="NATIVE" ;;
    *) RGB565_MODE="LUT" ;;
esac

read -p "¿Usar NEON para la conversion GL_FIXED (Q16.16) de vertex/color/texcoord arrays? [S/n] " NEON_FIXED_OPTION
if [[ "$NEON_FIXED_OPTION" =~ ^[nN]$ ]]; then
    NEON_FIXED="OFF"
else
    NEON_FIXED="ON"
fi

read -p "¿Cap de framerate estable a 30fps (2 vblanks, arregla el jitter ~40-60fps de NATIVE)? [S/n] " LOCK_FPS_OPTION
if [[ "$LOCK_FPS_OPTION" =~ ^[nN]$ ]]; then
    LOCK_FPS_30="OFF"
else
    LOCK_FPS_30="ON"
fi

echo "Configuracion elegida: BUILD_TYPE=$BUILD_TYPE RGB565_CONVERT_MODE=$RGB565_MODE OPTIMIZE_NEON_FIXED=$NEON_FIXED LOCK_FPS_30=$LOCK_FPS_30"

cmake "$SRC_DIR" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DRGB565_CONVERT_MODE="$RGB565_MODE" -DOPTIMIZE_NEON_FIXED="$NEON_FIXED" -DLOCK_FPS_30="$LOCK_FPS_30"
make -j$(sysctl -n hw.ncpu)

echo "[3/3] Exportando archivos generados..."
mkdir -p "$PROJECT_DIR/build"
cp "$VPK_NAME" "$PROJECT_DIR/build/$VPK_NAME"
if [ -f "eboot.bin" ]; then
    cp "eboot.bin" "$PROJECT_DIR/build/eboot.bin"
fi
# El ELF con simbolos es imprescindible para simbolizar un .psp2dmp con
# vita-parse-core; /tmp se borra al reiniciar, asi que se archiva junto al VPK.
if [ -f "zenonia_3" ]; then
    cp "zenonia_3" "$PROJECT_DIR/build/zenonia_3.elf"
fi

# Copia extra etiquetada con la variante A/B elegida (ademas de la copia
# "zenonia_3.vpk" de siempre) -- para poder instalar/comparar varias corridas
# sin que una se pise a la otra en build/.
VPK_TAGGED_NAME="zenonia_3_${RGB565_MODE}_neonfixed-${NEON_FIXED}_fps30-${LOCK_FPS_30}.vpk"
cp "$VPK_NAME" "$PROJECT_DIR/build/$VPK_TAGGED_NAME"

echo "Build exitoso: $PROJECT_DIR/build/$VPK_NAME"
echo "Copia etiquetada para A/B: $PROJECT_DIR/build/$VPK_TAGGED_NAME"
echo "eboot.bin exportado a: $PROJECT_DIR/build/eboot.bin"

VITA3K_APP="/Applications/Vita3K.app/Contents/MacOS/Vita3K"
if [ -x "$VITA3K_APP" ]; then
    read -p "¿Deseas instalar y ejecutar el VPK en Vita3K ahora? [s/N] " INSTALL_VITA3K
    if [[ "$INSTALL_VITA3K" =~ ^[sS]$ ]]; then
        echo "Instalando VPK y lanzando el emulador (backend OpenGL)..."
        "$VITA3K_APP" -B OpenGL "$PROJECT_DIR/build/$VPK_NAME" > /dev/null 2>&1 &
        echo "Listo."
    else
        echo "Omitiendo instalacion automatica en Vita3K."
    fi
else
    echo "Vita3K no encontrado en la ruta por defecto (/Applications/Vita3K.app)."
    echo "Puedes instalar el archivo $PROJECT_DIR/build/$VPK_NAME manualmente en tu emulador o consola."
fi
