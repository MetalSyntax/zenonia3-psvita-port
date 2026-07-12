# Errores de instalación del `.vpk` que no tienen nada que ver con el gameplay

Un `.vpk` puede fallar a instalar en consola real (típicamente cerca del final, ~99%) por razones que no
son un bug del port en sí, sino de cómo está armado el paquete de LiveArea. No asumir que un error de
instalación implica que el código del loader está mal.

## Error `0x8010113D`

Causa más citada por la comunidad (verificado buscando el código exacto): las imágenes de LiveArea
(`icon0.png`, `pic0.png`, `startup.png`, `bg0.png`) no están en formato PNG indexado de 8 bits. **Pero
confirmá el formato real antes de asumir que es eso** — parseando a mano el chunk `IHDR` de cada PNG (no
solo confiando en la extensión o en herramientas que no distinguen bien color type):

```python
import struct
with open(path, "rb") as f:
    data = f.read()
# IHDR empieza en el byte 16 (8 de firma PNG + 8 de "longitud+tipo" del primer chunk)
w, h, bitdepth, colortype, comp, filt, interlace = struct.unpack(">IIBBBBB", data[16:16+13])
# colortype == 3 significa indexado (paleta) -- lo que se busca
```

Si ya son indexados y el error persiste, **revisar `template.xml` de LiveArea**: `style="a1"` es el estándar
para homebrew (fondo + gate de arranque simple); otros valores de `style` (por ejemplo `"psmobile"`, legado
de PlayStation Mobile) traen requisitos de validación distintos y pueden fallar justo en el paso de
registrar el LiveArea/bubble — que es exactamente cuándo pasa este error. Si de todas formas las imágenes no
eran indexadas, `pngquant --force --output out.png 256 in.png` las convierte sin cambiar dimensiones ni
contenido visual.

## Metodología general para estos errores

1. Extraer el `.vpk` (es un zip) y verificar que **todos** los archivos esperados estén presentes, con
   tamaño no-cero (`sce_sys/param.sfo`, `eboot.bin`, los 4 PNG de LiveArea, `template.xml`).
2. Buscar el código de error exacto — suele haber threads de la comunidad (PSX-Place, GBAtemp, issues de
   VitaShell en GitHub) con la causa específica, aunque no siempre con confirmación técnica sólida — tratarlo
   como hipótesis a verificar, no como la causa confirmada.
3. Revisar cualquier archivo de configuración de LiveArea/paquete que se haya tocado recientemente sin
   commitear — un cambio de un solo atributo (como el `style` de arriba) puede ser la causa real aunque no
   sea la explicación "popular" para ese código de error.
