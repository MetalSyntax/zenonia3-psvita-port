# Renderizado de Texto en JNI (Cocos2d-x y stb_truetype)

En los ports que usan librerías estáticas de Android que delegan el renderizado de texto de la UI de nuevo hacia Java (por ejemplo, Cocos2d-x 1.x / 2.x mediante `Cocos2dxBitmap_createTextBitmap`), se debe proveer una implementación nativa para dibujar las fuentes. Usualmente se recurre a `stb_truetype`.

Durante la reimplementación de estas funciones, es crucial considerar tres aspectos técnicos que Android hace por defecto, pero que `stb_truetype` no implementa "fuera de la caja":

## 1. Escala de Fuente y DPI (Densidad de Píxeles)
Android escala automáticamente las fuentes según el DPI de la pantalla (`sp`). El valor numérico `fontSize` que el motor solicita por JNI a menudo es visualmente muy pequeño si se traduce 1:1 a los píxeles de `stb_truetype`.
**Solución:** Aumentar artificialmente el factor de escala en la función, por ejemplo multiplicando por `1.25f` al usar `stbtt_ScaleForPixelHeight`, para emular la escala natural de Android.

## 2. Word-Wrapping y Alineación de Cajas
A diferencia de un renderizador simple por línea, la UI de los juegos pasa parámetros de `width` y `height`, junto a un valor de `alignment` (bitmask). Si el texto es largo, **no debe truncarse ni desbordarse del recuadro**:
- Se debe calcular el ancho acumulado de los glifos (`advance`).
- Si el ancho supera el `width` de la caja, el texto debe saltar de línea buscando el último espacio en blanco registrado.
- El texto debe alinearse vertical y horizontalmente según el nibble superior e inferior del parámetro `alignment` (típicamente 1=Izquierda/Arriba, 2=Derecha/Abajo, 3=Centro).

## 3. Alfa Premultiplicado para Blend OpenGL (Crucial para el Color)
Los motores de juego (Cocos2d-x) típicamente dibujan el texto en memoria como una imagen *blanca con canal alfa*, la cargan a OpenGL y luego aplican un shader con un color multiplicador (e.g. texto negro, o texto amarillo brillante) usando *Blend Functions* que asumen **Alfa Premultiplicado** (Premultiplied Alpha).

Si se escribe el búfer de píxeles sin premultiplicar (ej. `[0xFF, 0xFF, 0xFF, alpha]`), los colores en pantalla aparecerán incorrectos (blancos brillantes) o la opacidad fallará.
**La forma correcta de escribir los píxeles (ARGB o RGBA según espere el motor)** usando `stb_truetype` es asegurar que los canales de color rojo, verde y azul estén previamente multiplicados por su propio alfa:

```c
// Al procesar el mapa de bits del glifo de stb_truetype:
unsigned char a = glyph[gy * glyph_w + gx]; // 0 a 255
if (!a) continue;

jbyte *px = buf + ((size_t) dy * width + dx) * 4;
// Se aplica Alfa Premultiplicado para evitar glitches de color
px[0] = (jbyte) a; // R
px[1] = (jbyte) a; // G
px[2] = (jbyte) a; // B
px[3] = (jbyte) a; // A
```
