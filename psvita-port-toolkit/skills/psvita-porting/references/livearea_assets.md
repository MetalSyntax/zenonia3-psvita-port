# Restricciones y Especificaciones para Recursos del LiveArea de PS Vita

Para generar un paquete `.vpk` que sea legítimamente instalable en la PS Vita y no arroje errores, es vital apegarse a especificaciones estrictas para las imágenes del **LiveArea**. Los errores comunes (como el infame `0x8010113D` durante la instalación desde VitaShell) usualmente provienen de formatos de imagen o metadatos no soportados.

## 1. Dimensiones Exigidas por el Sistema
La PS Vita rechaza cualquier VPK que incluya imágenes fuera de las siguientes dimensiones para el LiveArea (`sce_sys/livearea/contents/`):

- **icon0.png** (Ícono principal de la burbuja): `128 x 128`
- **bg0.png** (Imagen de fondo del LiveArea): `840 x 500`
- **pic0.png** (Arte principal o *"puerta"*): `960 x 544`
- **startup.png** (Pantalla de carga o Splash): `280 x 158`

## 2. Formato y Compresión de Imagen (Evitando Error `0x8010113D`)
Aunque las dimensiones estén correctas, VitaShell fallará al procesar el archivo si la estructura interna del PNG es pesada o tiene metadatos adicionales incompatibles. Las reglas inquebrantables son:

1. **Color Indexado a 8-Bits:** Convierte siempre las imágenes a paletas de 8 bits (256 colores) a través de herramientas como `pngquant` o guardando explícitamente en modo Color Indexado en editores de imagen. El formato 32-bits (RGBA) es más propenso a fallos y consume innecesaria memoria de la consola.
2. **Sin Entrelazado (Non-Interlaced):** El entrelazado de PNGs está prohibido.
3. **Despejado de Perfiles de Color y Metadatos:** Herramientas nativas como `sips` en macOS insertan perfiles `CgColorSpace` y metadatos de visualización incompatibles. Es estrictamente necesario eliminarlos. 

**Solución rápida vía Python (Pillow):**
```python
from PIL import Image
img = Image.open('icon0.png')
# Convierte a modo de 8-bits Indexado (P) sin metadatos residuales
img = img.convert('P', palette=Image.ADAPTIVE, colors=256)
img.save('icon0_fixed.png', format='PNG', optimize=True)
```

## 3. Limpieza de Entornos macOS (Archivos AppleDouble)
Al comprimir un VPK en macOS, el sistema a menudo introduce archivos de metadatos ocultos (los que comienzan con `._`, p. ej., `._icon0.png`). Si VitaShell intenta extraer e interpretar este archivo `._` como una imagen, la instalación fallará fatalmente (`0x8010113D`).

**Acción Correctiva:** Ejecutar siempre un escaneo de limpieza recursiva antes de lanzar `vita_create_vpk` o empaquetar en zip:
```bash
find . -name "._*" -type f -delete
```
