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

cmake "$SRC_DIR" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
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

echo "Build exitoso: $PROJECT_DIR/build/$VPK_NAME"
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
