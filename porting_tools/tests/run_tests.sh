#!/bin/bash
# Host-side pre-build test suite for the audio implementation.
# Run from anywhere; exercises the exact audio_path.h and minimp3 the Vita
# build compiles. Exits non-zero on any failure.
set -e

TESTS_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$TESTS_DIR/../.." && pwd)"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

echo "== [1/3] Extrayendo las rutas de audio que el juego puede pedir =="
{ strings "$PROJECT_DIR/bin/libgame_logic.so"; \
  strings "$PROJECT_DIR/bin/libcocos2d.so"; \
  strings "$PROJECT_DIR/bin/libcocosdenshion.so"; } \
  | grep -E '^Extra/Audio/' | sort -u > "$WORK_DIR/requested_audio.txt"
wc -l "$WORK_DIR/requested_audio.txt"

echo "== [2/3] Test del sanitizador de rutas + existencia de los assets =="
c++ -std=gnu++20 -Wall -o "$WORK_DIR/test_audio_path" "$TESTS_DIR/test_audio_path.cpp"
"$WORK_DIR/test_audio_path" "$WORK_DIR/requested_audio.txt" "$PROJECT_DIR/ux0_data/popclassic"

echo "== [3/3] Decodificando todos los .mp3 con el minimp3 vendorizado =="
cc -O2 -Wall -Wno-unused-value -o "$WORK_DIR/test_mp3_decode" "$TESTS_DIR/test_mp3_decode.c"
find "$PROJECT_DIR/ux0_data/popclassic/Data/Audio" -name '*.mp3' -print0 \
  | xargs -0 "$WORK_DIR/test_mp3_decode"

echo "✅ Todas las pruebas de host pasaron"
