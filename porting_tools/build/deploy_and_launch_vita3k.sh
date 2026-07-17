#!/bin/bash
# Compila (proceso manual, vía symlink sin espacios), despliega el eboot.bin +
# fuente empaquetada directo en el directorio de la app instalada en Vita3K
# (sin reinstalar el .vpk completo), relanza el emulador limpio y hace doble
# clic real (no accesibilidad) en el ícono del juego en la biblioteca.
#
# Requiere que el directorio fuente temporal /tmp/zn3-src y el directorio de build
# /tmp/zn3-build ya estén configurados (el script build_and_install.sh hace esto).
#   mkdir -p /tmp/zn3-build && cd /tmp/zn3-build && cmake /tmp/zn3-src -DCMAKE_BUILD_TYPE=Debug
#
# Uso: ./scripts/deploy_and_launch_vita3k.sh
set -e

VITASDK="${VITASDK:-$HOME/vitasdk}"
export VITASDK
export PATH="$VITASDK/bin:$PATH"

BUILD_DIR="/tmp/zn3-build"
APP_FS="$HOME/Library/Application Support/Vita3K/Vita3K/fs/ux0/app/SOLOADER0"
FONT_SRC="$(cd "$(dirname "$0")/../.." && pwd)/extras/fonts/DejaVuSans.ttf"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "[1/4] Compilando en $BUILD_DIR..."
cd "$BUILD_DIR"
make -j"$(sysctl -n hw.ncpu)"

echo "[2/4] Desplegando eboot.bin + fuente en $APP_FS..."
cp "$BUILD_DIR/eboot.bin" "$APP_FS/eboot.bin"
if [ -f "$FONT_SRC" ]; then
    cp "$FONT_SRC" "$APP_FS/DejaVuSans.ttf"
fi
rm -f "$HOME/Library/Application Support/Vita3K/Vita3K/logs/SOLOADER0 - [so-loader].log"

echo "[3/4] Relanzando Vita3K limpio..."
pkill -9 -x Vita3K 2>/dev/null || true
sleep 1
open -a Vita3K
sleep 3
osascript -e 'tell application "System Events" to tell process "Vita3K" to set frontmost to true'
sleep 1

echo "[4/4] Doble clic real en el ícono del juego..."
ROW_X=$(osascript -e '
tell application "System Events"
    tell process "Vita3K"
        set p to position of row 1 of table 1 of window 1
        return item 1 of p
    end tell
end tell')
ROW_Y=$(osascript -e '
tell application "System Events"
    tell process "Vita3K"
        set p to position of row 1 of table 1 of window 1
        return item 2 of p
    end tell
end tell')
# offset fijo (fila->título) medido en esta sesión; ajustar si Vita3K cambia su layout
CLICK_X=$((ROW_X + 60))
CLICK_Y=$((ROW_Y + 38))
python3 "$SCRIPT_DIR/click_helper.py" "$CLICK_X" "$CLICK_Y" 2

echo "Listo. Log en vivo:"
echo "  tail -f \"$HOME/Library/Application Support/Vita3K/Vita3K/logs/SOLOADER0 - [so-loader].log\""
