#!/bin/bash
# ==============================================================================
# Script de inspección y desensamblado (Comandos Bash utilizados por IA / Debug)
# ==============================================================================

set -e

# Configuración de variables con valores por defecto
VITASDK_PATH="${VITASDK:-/Users/metalsyntax/vitasdk}"
export PATH="$VITASDK_PATH/bin:$PATH"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

SO_PATH="${1:-$PROJECT_ROOT/module/libDungeonHunter2.so}"
PATCH_C_PATH="$PROJECT_ROOT/source/patch.c"
DYNLIB_C_PATH="$PROJECT_ROOT/source/dynlib.c"
MAIN_C_PATH="$PROJECT_ROOT/source/main.c"

echo "=============================================================================="
echo "          EJECUTANDO COMANDOS BASH DE ANÁLISIS Y DESENSAMBLADO                "
echo "=============================================================================="
echo " SO Target: $SO_PATH"
echo " VitaSDK:   $VITASDK_PATH"
echo "=============================================================================="

if [ ! -f "$SO_PATH" ]; then
    echo "[-] Error: No se encontró el archivo .so en: $SO_PATH"
    exit 1
fi

disasm_thumb() {
    local start=$1
    local stop=$2
    local label=$3
    echo ""
    echo "[*] Disassembly ($label): $start -> $stop"
    arm-vita-eabi-objdump -d -M force-thumb --start-address="$start" --stop-address="$stop" "$SO_PATH"
}

# 1. Inspección de apertura de archivos y rotación de cadenas
disasm_thumb "0x000ff54a" "0x000ff580" "File::Open / Offset parse"

# 2. Funciones de decodificación y gráficos (SetImage4GL / DecodePNG)
disasm_thumb "0x00101b06" "0x00101b30" "SetImage4GL (Rango 1)"
disasm_thumb "0x00101aee" "0x00101b00" "SetImage4GL -> DecodePNG call"
disasm_thumb "0x00101ade" "0x00101aee" "SetImage4GL (Rango 2)"

# 3. Símbolos de decodificación en libgame.so
echo ""
echo "[*] Búsqueda de símbolos 'Decode' (readelf --dyn-syms):"
arm-vita-eabi-readelf -W --dyn-syms "$SO_PATH" | grep "Decode" || true

# 4. Verificación de cabecera Magic de cabecera de imagen MainMenu.png.jpg
PNG_SAMPLE="$PROJECT_ROOT/assets/game_res/MainMenu.png.jpg"
if [ -f "$PNG_SAMPLE" ]; then
    echo ""
    echo "[*] Inspección de bytes mágicos de $PNG_SAMPLE (xxd):"
    xxd -l 16 "$PNG_SAMPLE"
fi

# 5. Desensamblado interno de DecodePNG y llamadas a libpng
disasm_thumb "0x001029bc" "0x00102a10" "DecodePNG (create_read_struct & create_info_struct)"
disasm_thumb "0x00102a00" "0x00102a50" "DecodePNG (set_read_fn & checks)"
disasm_thumb "0x0010d9a4" "0x0010d9f0" "png_create_info_struct"
disasm_thumb "0x0010f980" "0x0010f9b0" "png_read_png (Crash point ldr r2, [r5, #4])"
disasm_thumb "0x0010ffd0" "0x00110000" "png_create_read_struct / DecodeBMP"
disasm_thumb "0x0010fd74" "0x0010fdcc" "png_create_read_struct_2 (Parte 1)"
disasm_thumb "0x0010fdca" "0x0010fdf0" "png_create_read_struct_2 (Parte 2 - setjmp & set_error_fn)"
disasm_thumb "0x0010fe38" "0x0010fe70" "png_create_read_struct_2 (Parte 3 - warning handler)"
disasm_thumb "0x0010fe80" "0x0010fec0" "png_create_read_struct_2 (Parte 4 - canary check / end)"
disasm_thumb "0x0010ff68" "0x0010ff9c" "png_create_read_struct_2 (Stack check)"
disasm_thumb "0x0010da54" "0x0010da80" "png_set_read_fn"
disasm_thumb "0x0010dba4" "0x0010dbf0" "png_warning"

# 6. Búsqueda de símbolos libpng (png_error, png_malloc)
echo ""
echo "[*] Símbolos png_error en $SO_PATH:"
arm-vita-eabi-readelf -W --dyn-syms "$SO_PATH" | grep "png_error" || true

echo ""
echo "[*] Símbolos png_malloc en $SO_PATH:"
arm-vita-eabi-readelf -W --dyn-syms "$SO_PATH" | grep "png_malloc" || true

disasm_thumb "0x0010e5ec" "0x0010e620" "png_destroy_read_struct"
disasm_thumb "0x0010e6d8" "0x0010e6f0" "png_malloc_default -> malloc@plt"

# 7. Verificación de parches e imports en el código fuente del loader
if [ -f "$PATCH_C_PATH" ]; then
    echo ""
    echo "[*] Ocurrencias de 'png_create_struct_2' en patch.c:"
    grep "png_create_struct_2" -B 2 -A 2 "$PATCH_C_PATH" || true
fi

if [ -f "$DYNLIB_C_PATH" ]; then
    echo ""
    echo "[*] Ocurrencias de 'malloc' en dynlib.c:"
    grep -i "malloc" -B 2 -A 2 "$DYNLIB_C_PATH" || true
fi

if [ -f "$MAIN_C_PATH" ]; then
    echo ""
    echo "[*] Configuración de sceLibcHeapSize en main.c:"
    grep "sceLibcHeapSize" "$MAIN_C_PATH" || true
fi

echo ""
echo "[+] Todos los comandos de inspección y desensamblado ejecutados exitosamente."
