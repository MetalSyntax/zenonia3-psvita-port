# Depuración de Errores y Análisis de Coredumps

Los crashes en la PS Vita suelen manifestarse con códigos de error como `C2-12828-1` (Acceso no válido a memoria/Segmentation Fault).

## 1. Uso de Logs del Sistema (Logging)
* En tiempo de desarrollo, asegúrate de activar la macro de depuración de FalsoJNI (`FALSOJNI_DEBUGLEVEL=0` o similar) y usar `sceClibPrintf` para volcar trazas a través de red o mediante plugins de captura de logs como `PrincessLog`.
* Registra todas las llamadas a funciones de Android importadas que devuelvan valores no inicializados o nulos.

## 2. Análisis de Coredumps (`vita-parse-core`)
Cuando la consola genera un archivo de volcado `.dmp` tras un crash:
1. Copia el archivo `.dmp` desde `ux0:data/` en la Vita a tu ordenador.
2. Utiliza la herramienta `vita-parse-core` pasándole el binario ELF resultante de la compilación para localizar la instrucción exacta que generó el error:
   ```bash
   python3 vita-parse-core coredump_xxx.dmp build/so_loader.elf
   ```
3. Esto devolverá el stack trace con números de línea exactos en tu código fuente o dentro del `.so` relocalizado.
