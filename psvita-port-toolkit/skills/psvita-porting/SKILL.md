---
name: psvita-porting
description: Guía de desarrollo y habilidades para realizar ports de Android a PS Vita usando cargadores de librerías dinámicas (.so) como SoLoader Boilerplate o falsos entornos JNI. Activar cuando se realicen ports, depuración de llamadas JNI de Android o configuraciones del SDK de Vita (vitasdk).
---

# Desarrollo de Ports de Android para PS Vita (SoLoader)

Esta habilidad contiene directrices detalladas, flujos de trabajo comunes y referencias para adaptar ejecutables y librerías dinámicas de Android para que funcionen de forma nativa en la PS Vita.

## Referencias y Subdocumentos

- **Guía de Carga Dinámica y Relocalización (.so)**: [references/so_loading.md](references/so_loading.md)
  - Cómo mapear múltiples módulos, definir offsets de direcciones de memoria en `main.c` y usar `so_file_load`.
- **Implementación de FalsoJNI y Enlaces JNI de Cocos2d-x**: [references/jni_bindings.md](references/jni_bindings.md)
  - Mapeo de llamadas JNI nativas del motor (Touches, KeyDown, nativeInit, nativeRender).
- **Ajustes de Compilación con VitaSDK**: [references/vitasdk_setup.md](references/vitasdk_setup.md)
  - Modificación de `CMakeLists.txt`, dependencias de linkeo (`vitaGL`, `mathneon`, `kubridge_stub`) y uso de `softfp`.
- **Renderizado de Texto en C con stb_truetype**: [references/text_rendering.md](references/text_rendering.md)
  - Resolución de problemas de tipografía, escalado, saltos de línea (word wrap) y uso vital del Alfa Premultiplicado para evitar errores de color.
- **Creación Segura de Recursos del LiveArea**: [references/livearea_assets.md](references/livearea_assets.md)
  - Reglas estrictas (dimensiones, PNGs 8-bit indexados, eliminación de metadatos `._`) para evitar el error de instalación `0x8010113D` en VitaShell.

## Lecciones confirmadas en hardware real (esta sesión de debugging)

- **Depuración en consola real sin plugins de red**: [references/hardware_debugging.md](references/hardware_debugging.md)
  - Logging a archivo por ejecución, método iterativo de "un fallo a la vez", y cómo leer un `.psp2dmp` con `vita-parse-core` (setup completo, incluyendo el parche necesario para Python moderno).
- **`.apk`/`.obb` vs. archivos sueltos**: [references/asset_packaging.md](references/asset_packaging.md)
  - El fallback real de `CCFileUtils::getFileData` a ZIP, y cómo armar `.apk`/`.obb` mínimos o completos con Python en vez de duplicar el peso en la tarjeta.
- **Stubs de JNI que cuelgan el motor**: [references/jni_stubs.md](references/jni_stubs.md)
  - Cuándo un no-op alcanza y cuándo hay que disparar el callback de "completado" real del motor (patrón `onVideoCompleted` y similares).
- **Input táctil y de botones**: [references/input_handling.md](references/input_handling.md)
  - `sceTouchSetSamplingState` apagado por default, por qué nunca hay que pasarle al motor el ID crudo de `SceTouchReport` (corrompe el heap), y cómo mapear botones físicos a touches sintéticos sin usar un slot "extra".
- **Errores de instalación del `.vpk` no relacionados al gameplay**: [references/vpk_packaging.md](references/vpk_packaging.md)
  - Complementa a `livearea_assets.md` con la metodología general para diagnosticar estos errores.
- **Gotchas de toolchain**: [references/toolchain_gotchas.md](references/toolchain_gotchas.md)
  - Rutas con espacios, `CMAKE_POLICY_VERSION_MINIMUM`, y symlinks/directorios de build que pueden desaparecer entre sesiones.
