# Herramientas de porting Android → PS Vita

Recopiladas durante el port de Prince of Persia Classic. Todas asumen un proyecto so-loader
(vitasdk + FalsoJNI) similar a este; ajustar rutas/IPs marcadas abajo antes de reusar.

## Build y despliegue

- **`build_and_install.sh`** — build completo vía symlink sin espacios (workaround del bug de
  `vita-pack-vpk` con rutas que contienen espacios) + instalación. Ajustar `PROJECT_DIR`/nombre del `.vpk`.
- **`manage_vita.py`** — herramienta de despliegue y debug por FTP (VitaShell): sube el `.vpk`/`eboot.bin`,
  descarga el log más reciente de `ux0:data/<juego>/logs/`. Editar `VITA_IP`, `VITA_PORT`,
  `LOCAL_VPK_PATH`, `VITA_DATA_DIR` al inicio del archivo.
- **`scripts/deploy_and_launch_vita3k.sh`** — para pruebas en el emulador Vita3K (macOS): copia el
  `eboot.bin` recién compilado directo al directorio de la app instalada (sin reinstalar el `.vpk`
  completo) y relanza el emulador. Requiere el symlink `~/popc-src` y build dir descritos en el script.
- **`extras/scripts/get_dump.sh`** — descarga el `.psp2dmp` (core dump) más reciente de la consola por FTP.
  Uso: `./get_dump.sh <IP-de-la-vita>`.

## Automatización de clics en Vita3K (macOS)

Vita3K usa una UI Qt que **no** responde a clics sintéticos de accesibilidad (`osascript`/AppleScript) —
hace falta inyectar eventos reales de mouse a nivel de sistema operativo vía Quartz.

- **`scripts/click_helper.py`** — clic (simple o doble) en una coordenada de pantalla dada.
- **`scripts/hold_click.py`** — mueve, presiona y mantiene el botón del mouse por N segundos.
- **`scripts/mousedown_only.py`** / **`scripts/mouseup_only.py`** — presionar/soltar por separado
  (para simular un drag entre dos invocaciones).
- **`scripts/key_helper.py`** — presiona una tecla del teclado por su nombre (mapa de keycodes de macOS).

Requieren `pip install pyobjc` (para el módulo `Quartz`).

## Decompilación

- **`decompile_all.sh`** — corre Jadx (classes.dex → Java) y devrvk/so-decompiler (.so → pseudo-C) vía
  Docker. Ajustar las rutas de entrada (`classes.dex`, los `.so`) al layout del proyecto nuevo.

## Testing y utilidades varias

- **`extras/tests/run_tests.sh`** — corre el test suite del lado del host (no en la consola) para la
  lógica de audio (resample/decode) contra el mismo código que compila la build de Vita. Adaptar a los
  módulos del proyecto nuevo si no es audio lo que se está verificando.
- **`clean_macos.sh`** — borra archivos basura `._*` que macOS genera en unidades no-HFS+ (USB/red), que
  pueden confundir herramientas de empaquetado si se cuelan en el build.
- **`scripts/translate_docs.py`** — traduce archivos Markdown en lote con `deep_translator`
  (Google Translate). Requiere `pip install deep-translator`.
