# Depuración en hardware real, sin plugins de red

En consola real (a diferencia de Vita3K) no siempre es práctico depender de un plugin de captura de red
(PrincessLog, etc.) para ver `sceClibPrintf`. La técnica más simple y confiable: **el propio loader escribe
su log a un archivo en la tarjeta de memoria**, que después se baja por FTP con VitaShell — sin plugins.

## Logging a archivo, uno nuevo por ejecución

No uses un único `log.txt` que se acumula (o que queda pisado/viejo entre pruebas — es fácil confundirse
bajando el archivo equivocado). Generá un nombre nuevo por ejecución con el timestamp real:

```c
#include <psp2/rtc.h>   // o <time.h> si el loader usa newlib con USE_SCELIBC_IO

#ifdef DATA_PATH
static char log_file_path[256] = {0};
if (log_file_path[0] == '\0') {
    sceIoMkdir(DATA_PATH "logs", 0777);   // sceIo NO crea directorios intermedios solo
    time_t t = time(NULL);                 // ya viene correcto vía el puente de newlib/RTC
    sceClibSnprintf(log_file_path, sizeof(log_file_path), "%slogs/log_%u_.txt", DATA_PATH, (unsigned int)t);
}
int fd = sceIoOpen(log_file_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
if (fd >= 0) {
    sceIoWrite(fd, buffer, strlen(buffer));
    sceIoClose(fd);
}
#endif
```

Puntos importantes:
- `sceIoMkdir` falla silenciosamente si la carpeta ya existe — no hace falta chequear el error.
- Generar el nombre **una sola vez** (guardado en una variable `static`), no en cada llamada al logger, o
  cada línea termina en un archivo distinto.
- Si el proyecto ya tenía un log de una versión vieja del loader en una ruta distinta, **borrarlo** de la
  consola — si no, es fácil bajar por error el archivo viejo y diagnosticar sobre datos stale (pasó en este
  proyecto: dos "capturas nuevas" seguidas resultaron ser exactamente el mismo archivo viejo).

## Método iterativo: un fallo a la vez, no adivinar en bloque

Cuando un asset no carga o el motor llama a algo no implementado, el log casi siempre dice exactamente qué
faltó (`fopen(ruta, modo): 0x0` para NULL, o `[JniHelper] Failed to find static method id of X`). La forma
eficiente de avanzar:

1. Arrancar el juego, capturar el log, ver **la última línea antes del corte** (crash) o el patrón que se
   repite sin avanzar (freeze/colgada).
2. Arreglar **solo eso** — no tratar de adivinar de antemano toda la lista de cosas que podrían faltar.
3. Recompilar, volver a probar, repetir.

Esto es mucho más rápido que intentar portar "todo de una" a ciegas, porque cada iteración en consola real es
cara (rebuild + subir archivos + probar a mano) — cada iteración debe resolver exactamente el problema que el
log ya mostró, no una hipótesis sin confirmar.

## Diferenciar crash (queda un rastro) de freeze/colgada (no queda nada)

- **Crash real** (Data abort, acceso a memoria inválido): el log se corta abruptamente después de la última
  línea, y en algunos casos la consola genera un volcado de memoria (`.dmp`) que se puede analizar con
  `vita-parse-core` contra el `.elf` de la build para sacar la instrucción exacta que falló.
- **Freeze/colgada** (no responde a input, pero no hay crash): el log también se corta, pero puede que se
  repita un patrón (el motor reintentando algo) antes de cortarse — señal de que algo está esperando un
  evento/callback que nunca llega, no de un acceso a memoria inválido. Ver
  [jni_stubs.md](jni_stubs.md) para el patrón típico de este tipo de bug.

## Warnings sospechosos aunque no sean fatales

`[JniHelper] Failed to find static method id of X` no es fatal por sí solo (el motor sigue corriendo), pero
casi siempre es la pista de la siguiente pantalla rota o colgada — no descartarlo solo porque el juego "no se
cayó" en ese momento.

## Analizar un `.psp2dmp` con `vita-parse-core` — cuando el log de texto no alcanza

Si el log se corta sin ninguna pista (ni un `fopen` fallido, ni un warning de JNI justo antes), y sobre todo
si el crash parece "no tener sentido" desde el log (o es corrupción de heap, que aparece lejos de donde
realmente empezó el problema), hace falta el volcado real de la consola, no seguir adivinando desde el log.
La consola genera un `psp2core-<timestamp>-<offset>-eboot.bin.psp2dmp` en `ux0:data/` cuando crashea —
bajarlo por FTP con VitaShell.

```bash
git clone https://github.com/xyzz/vita-parse-core ~/vita-tools/vita-parse-core
cd ~/vita-tools/vita-parse-core
python3 -m venv venv
./venv/bin/pip install "pyelftools==0.24"   # la version moderna no sirve, le falta py3compat
```

Con Python 3.10+, la 0.24 falla igual por un import viejo — parchear una línea del paquete ya instalado:

```bash
# en venv/lib/python*/site-packages/elftools/construct/lib/container.py:
# from collections import MutableMapping
# ->
# from collections.abc import MutableMapping
```

Uso — el segundo argumento es el **ELF plano sin firmar** (el que produce el linker antes de la conversión a
`.velf`, típicamente llamado igual que el ejecutable de CMake, ej. `so_loader`, generado con `-g`) — **no**
`.velf` ni `eboot.bin`:

```bash
export VITASDK=~/vitasdk && export PATH="$VITASDK/bin:$PATH"
./venv/bin/python3 main.py ruta/al/volcado.psp2dmp ruta/al/build/so_loader
```

Esto da `PC`/`LR`/registros del thread que crasheó. Si la dirección cae dentro del propio `so_loader`
(rango tipo `0x81xxxxxx`), la herramienta ya la resuelve a símbolo sola. Si cae dentro de uno de los `.so`
cargados dinámicamente (el motor del juego, no el loader — rango tipo `0x99xxxxxx`/`0x9Axxxxxx` según los
`LOAD_ADDRESS` que use el proyecto), hay que resolverla a mano restando la base de carga de ese módulo y
buscando el símbolo más cercano *por debajo* del offset resultante:

```python
import subprocess
out = subprocess.run(["arm-vita-eabi-nm", "-D", "--defined-only", "libcocos2d.so"],
                      capture_output=True, text=True).stdout
syms = sorted(
    (int(l.split()[0], 16), l.split(None, 2)[2])
    for l in out.splitlines() if len(l.split(None, 2)) == 3
)
offset = 0x990a3fd2 - 0x99000000   # direccion del crash menos la base del modulo
sym = max((a, n) for a, n in syms if a <= offset)
print(sym)
```

Un `Data abort` con `R0=0`/`R3=0` casi siempre es un puntero nulo. Un `PC` que cae dentro de código de
newlib (`_free_r`, `malloc`, etc.) con un `LR` que no tiene ningún sentido como dirección real es corrupción
de heap descubierta tarde — mirar la pila alrededor de `SP` en el volcado para encontrar direcciones que sí
resuelvan a símbolos reales del motor, esa es la pista de dónde empezó el problema de verdad (no dónde
finalmente crasheó).
