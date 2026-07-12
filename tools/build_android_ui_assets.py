#!/usr/bin/env python3
"""
Convierte a RGBA8888 crudo los drawables Android (variantes "globales"/en
ingles, no las localizadas _kr/_jp/_ch) que el juego original dibuja como
ImageView/ImageButton superpuestos al GLSurfaceView (ver res/layout/main.xml
y com/gamevil/nexus2/Natives.java -- showTitleComponent/showMenuComponent/
showMenuItemComponent), y que este loader nativo no reproducia porque nunca
existio una capa Android/Java: el logo de Gamevil, el fondo de titulo y el
fondo + botones del menu principal se ven en blanco/negro sin esto.

Formato de salida (leido por loader/androidui.c): header de 8 bytes
(uint32 width, uint32 height, little-endian) + width*height*4 bytes RGBA8888,
fila por fila de arriba hacia abajo -- mismo layout que ya usa
main.c:splash_load() para app0:splash.rgba, sin necesidad de un decoder PNG
en el loader.

Uso: python3 tools/build_android_ui_assets.py
Requiere Pillow (pip install pillow). Escribe a androidui/*.rgba en la raiz
del repo -- CMakeLists.txt empaqueta esos archivos en el VPK (FILE
androidui/x.rgba androidui/x.rgba, igual que font.ttf).
"""
import os
import struct
import sys

try:
    from PIL import Image
except ImportError:
    print("Falta Pillow: pip install pillow", file=sys.stderr)
    sys.exit(1)

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(REPO_ROOT, "zenonia3", "res", "drawable")
OUT_DIR = os.path.join(REPO_ROOT, "androidui")

# Variantes "globales" (ingles) elegidas a proposito -- no las _kr/_jp/_ch.
ASSETS = [
    "ui_logo_gamevil.png",   # LOGO (ui_status == -1, antes del primer OnUIStatusChange)
    "ui_title_bg_nate.png",  # TITLE (ui_status == 1) -- fondo completo
    "ui_title_logo5.png",    # TITLE -- logo chico, centrado abajo
    "ui_menu_back0.png",     # MAINMENU (ui_status == 2) -- fondo completo
    "ui_menu_back1.png",     # MAINMENU -- franja superior
    "ui_menu_newgame.png",
    "ui_menu_continue.png",
    "ui_menu_options.png",
    "ui_menu_help.png",
    "ui_menu_about.png",
    "ui_menu_community.png",
    "ui_about_bg.png",       # ABOUT (ui_status == 5) -- fondo completo
    "ui_help_bg.png",        # HELP (ui_status == 4) -- fondo completo
    "ui_menu_back.png",      # ABOUT/HELP -- boton "back" compartido (top|right, sin margin)
    "reply_page_back_e.png", # REPLY_PAGE (ui_status == 5000, popup "valorar la app" al tocar Continuar) -- fondo completo
    "button_write_01_global.png",  # REPLY_PAGE -- boton "escribir resena"
    "button_later_01_global.png",  # REPLY_PAGE -- boton "mas tarde"
]


def convert(name):
    src = os.path.join(SRC_DIR, name)
    out = os.path.join(OUT_DIR, os.path.splitext(name)[0] + ".rgba")
    img = Image.open(src).convert("RGBA")
    w, h = img.size
    data = img.tobytes()
    with open(out, "wb") as f:
        f.write(struct.pack("<II", w, h))
        f.write(data)
    print(f"{name}: {w}x{h} -> {os.path.relpath(out, REPO_ROOT)} ({len(data)} bytes)")


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    for name in ASSETS:
        convert(name)


if __name__ == "__main__":
    main()
