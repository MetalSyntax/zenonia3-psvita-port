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
    export PATH="$VITASDK/bin:$PATH"
fi

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

echo "Para que plataforma quieres configurar la instalacion? (1: Vita3K, 2: PS Vita)"
read -p "Opcion [1]: " PLATFORM_OPTION
PLATFORM_OPTION=${PLATFORM_OPTION:-1}

read -p "Quieres instalar o transferir el .vpk automaticamente? (s/n) [s]: " INSTALL_VPK
INSTALL_VPK=${INSTALL_VPK:-s}

echo "[4/4] Instalacion..."
if [ "$INSTALL_VPK" = "s" ] || [ "$INSTALL_VPK" = "S" ]; then
    if [ "$PLATFORM_OPTION" = "1" ]; then
        VITA3K_APP="/Applications/Vita3K.app/Contents/MacOS/Vita3K"
        if [ -x "$VITA3K_APP" ]; then
            echo "Lanzando Vita3K y cargando VPK..."
            "$VITA3K_APP" -B OpenGL "$PROJECT_DIR/build/$VPK_NAME" > /dev/null 2>&1 &
            echo "Listo! El juego se esta abriendo."
        else
            echo "Vita3K no encontrado en la ruta por defecto (/Applications/Vita3K.app)."
            echo "=== INSTRUCCIONES PARA VITA3K ==="
            echo "1. Ve a 'File -> Install .vpk' y selecciona 'build/zenonia_3.vpk'."
            echo "2. Copia todo el contenido de assets/ del juego a la ruta virtual del emulador:"
            echo "   (Normalmente en: ~/.local/share/Vita3K/ux0/data/zenonia3/)"
        fi
    elif [ "$PLATFORM_OPTION" = "2" ]; then
        echo "Asegurate de que VitaShell este abierto con el servidor FTP activado en tu PS Vita."
        read -p "Ingresa la direccion IP de tu PS Vita (ej: 192.168.1.100) (Deja en blanco para cancelar): " VITA_IP
        if [ ! -z "$VITA_IP" ]; then
            echo "Enviando VPK a ftp://$VITA_IP:1337/ux0:/vpk/$VPK_NAME ..."
            curl -T "$PROJECT_DIR/build/$VPK_NAME" "ftp://$VITA_IP:1337/ux0:/vpk/$VPK_NAME"
            echo "Archivo transferido. Recuerda instalar el .vpk desde VitaShell en ux0:/vpk/ y copiar los assets a ux0:/data/zenonia3/"
        else
            echo "Transferencia a PS Vita cancelada."
        fi
    else
        echo "Opcion no valida. Instalacion cancelada."
    fi
else
    echo "Instalacion omitida por el usuario."
fi
