#!/bin/bash
set -e

# Configuracion
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="/tmp/zenonia3-build"
SRC_DIR="/tmp/zenonia3-src"
VPK_NAME="zenonia_3.vpk"

echo "================================================================"
echo "  Script de Build Automatico para Zenonia 3 (PS Vita)"
echo "================================================================"

echo "[1/4] Preparando entorno de compilacion..."
# Evitamos problemas de rutas con espacios ("PSVITA Develop") usando un
# directorio temporal (ver psvita-porting skill, toolchain_gotchas.md).
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
fi
# Necesario aunque VITASDK ya venga seteado del entorno: el build de vitaGL
# vendorizado (lib/vitagl, ver CMakeLists.txt) corre su propio Makefile con
# `arm-vita-eabi-gcc` sin ruta absoluta, a diferencia del resto del proyecto
# que usa CMAKE_C_COMPILER con ruta absoluta y no depende de PATH.
export PATH="$VITASDK/bin:$PATH"

rsync -a --exclude '.git' --exclude 'build' --exclude '.*' "$PROJECT_DIR/" "$SRC_DIR/"

echo "[2/4] Ejecutando CMake y Make..."
cd "$BUILD_DIR"

cmake "$SRC_DIR" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.ncpu)

echo "[3/4] Exportando archivos generados..."
mkdir -p "$PROJECT_DIR/build"
cp "$VPK_NAME" "$PROJECT_DIR/build/$VPK_NAME"
if [ -f "eboot.bin" ]; then
    cp "eboot.bin" "$PROJECT_DIR/build/eboot.bin"
fi
if [ -f "zenonia_3" ]; then
    cp "zenonia_3" "$PROJECT_DIR/build/zenonia_3.elf"
fi

echo "Build exitoso: $PROJECT_DIR/build/$VPK_NAME"

read -p "Quieres transferir el .vpk a tu PS Vita por FTP ahora? (s/n) [s]: " INSTALL_VPK
INSTALL_VPK=${INSTALL_VPK:-s}

echo "[4/4] Instalacion en PS Vita..."
if [ "$INSTALL_VPK" = "s" ] || [ "$INSTALL_VPK" = "S" ]; then
    echo "Asegurate de que VitaShell este abierto con el servidor FTP activado en tu PS Vita."
    DEFAULT_VITA_IP=$(sed -n 's/.*"vita_ip"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$PROJECT_DIR/.psvita-toolkit.json" 2>/dev/null | head -1)
    read -p "Ingresa la direccion IP de tu PS Vita [${DEFAULT_VITA_IP:-deja en blanco para cancelar}]: " VITA_IP
    VITA_IP=${VITA_IP:-$DEFAULT_VITA_IP}
    if [ ! -z "$VITA_IP" ]; then
        echo "Enviando VPK a ftp://$VITA_IP:1337/ux0:/vpk/$VPK_NAME ..."
        curl -T "$PROJECT_DIR/build/$VPK_NAME" "ftp://$VITA_IP:1337/ux0:/vpk/$VPK_NAME"
        echo "Archivo transferido. Recuerda instalar el .vpk desde VitaShell en ux0:/vpk/ y copiar los assets a ux0:/data/zenonia3/"
    else
        echo "Transferencia a PS Vita cancelada."
    fi
else
    echo "Instalacion omitida por el usuario."
fi
