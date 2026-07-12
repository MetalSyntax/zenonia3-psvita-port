# `.apk`/`.obb` vs. archivos sueltos: entender el fallback real antes de elegir

Los juegos Android empaquetados con cocos2d-x reparten sus assets entre el `.apk` (código + algunos
recursos), el `.obb` (los assets grandes, como un ZIP separado) y, en el port, opcionalmente una carpeta de
archivos ya descomprimidos. **No asumas de antemano** cuánto hace falta de cada uno — medilo.

## El mecanismo real: `CCFileUtils::getFileData` cae a ZIP si no encuentra el archivo suelto

`cocos2d::CCFileUtils::getFileData(const char* file, const char* mode, unsigned long* size)` es la función
que el motor usa para leer casi cualquier asset (texturas, `.plist`, animaciones, sonidos, localización,
config). Su comportamiento real (confirmado con logs de ejecuciones reales, no solo leyendo el código):

1. Intenta abrir el archivo **suelto** (`fopen()` directo) bajo la carpeta de datos del port.
2. Si eso falla, abre el segundo argumento de `nativeSetPaths` (`apkFilePath`, tratado como carpeta + nombre
   de archivo conocido del `.obb`) **como un ZIP**, y busca el archivo adentro con un prefijo específico de
   "bucket" de resolución (algo como `Data_960_576/<ruta>`, elegido según cómo el juego detecta la
   resolución de pantalla — no necesariamente el mismo string que loguea como `s_strResourcePath`).
3. El tercer argumento de `nativeSetPaths` (`apkSourceDir`) se abre **directamente** como ZIP para leer
   `assets/<archivo>` — típicamente solo se usa para `assets/appConfig.txt` al arrancar.

Este fallback **aplica a cualquier archivo**, no solo a los "obvios" (localización, logo de splash) — se
confirmó pidiendo texturas, `.plist` de sprites, animaciones y efectos por igual. No asumas que solo un
puñado de archivos especiales usan la ruta ZIP.

## Cómo decidir la estrategia sin adivinar

Dos estrategias válidas, no las mezcles (si tenés la carpeta suelta completa Y el `.apk`/`.obb` completos al
mismo tiempo, estás duplicando el peso en la tarjeta de memoria sin necesidad):

- **Todo suelto** (sin `.apk`/`.obb`): requiere que TODOS los archivos que el motor pide existan bajo la
  carpeta de datos con la ruta exacta que arma `getFileData` — si algo falta, `fopen` devuelve NULL y (según
  el motor) puede crashear en vez de caer al ZIP, porque el `.apk`/`.obb` mínimos que le diste no lo
  contienen.
- **Todo vía `.apk`/`.obb`** (sin carpeta suelta): más simple de razonar, porque **un solo `.obb` armado a
  mano cubre todo lo que el fallback pediría**. Armalo así, no copiando los archivos originales completos
  (que suelen traer contenido de plataformas/resoluciones que no usás, "assets/" completo de Android, etc.):

```python
import zipfile, os

with zipfile.ZipFile("main.obb", "w", zipfile.ZIP_STORED) as z:  # STORED: la mayoría del contenido
    for root, dirs, files in os.walk("Data"):                    # (png, ogg) ya viene comprimido -- DEFLATE
        for fname in files:                                      # solo agrega carga de CPU al leerlo en runtime
            full = os.path.join(root, fname)
            rel = os.path.relpath(full, "Data")
            z.write(full, f"Data_960_576/{rel}")   # el prefijo real se confirma viendo el log, no se asume
```

## Cómo confirmar (no asumir) qué prefijo/bucket usa el motor

Mirá el log de una ejecución real: cuando el motor cae al ZIP, suele loguear algo como
`fullPath = Data_960_576/Texture/Menu/buttons.png` justo antes de intentar abrir el `.obb`. Ese string **es**
el prefijo real — no el que aparece en `s_strResourcePath` (que puede ser otro nombre completamente distinto
y no usarse para las rutas de archivo reales).

## Reducir el `.apk` a lo mínimo necesario

El `.apk` de un juego Android típico trae cientos de archivos (manifest, `classes.dex`, recursos de Android,
`.so` para otras arquitecturas) que el port nunca toca. Si solo se necesita `assets/appConfig.txt`, un `.apk`
mínimo de una sola entrada (unos cientos de bytes) sirve igual — confirmá con un diff de contenido contra el
original que el texto es idéntico, no asumas que "algo tan chico" está bien solo por el tamaño.

## Auditoría antes de duplicar

Antes de armar el ZIP completo, medí qué tan grande es realmente la carpeta suelta en bytes de contenido
(`os.path.getsize` sumado, no `du -sh` — `du` cuenta overhead de bloques de filesystem en muchos archivos
chicos y puede reportar el doble del contenido real).
