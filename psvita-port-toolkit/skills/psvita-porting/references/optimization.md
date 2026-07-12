# Optimización de Rendimiento y Prevención de Bugs Gráficos

El hardware de la PS Vita tiene limitaciones específicas de memoria de video y procesamiento de punto flotante.

## 1. Prevención de Overheads de GPU
* **Gestión de Shaders**: La carga y compilación dinámica de shaders GLSL puede causar micro-tirones (stuttering). Habilita siempre la caché de shaders en disco en el cargador:
  ```cmake
  add_definitions(-DDUMP_COMPILED_SHADERS)
  ```
* **Triple Buffering**: Para la mayoría de juegos basados en Cocos2d-x, desactivar el triple buffer reduce la latencia de entrada y ahorra memoria VRAM valiosa:
  ```c
  vglUseTripleBuffering(GL_FALSE);
  ```

## 2. Caché y Sincronización de Memoria
* Debido a que el cargador lee código ejecutable de Android directamente en la RAM de usuario de la PS Vita, es crítico forzar el vaciado de las cachés de instrucciones antes de llamar a funciones del `.so`:
  ```c
  so_flush_caches(&my_module);
  ```
* Asegúrate de no realizar asignaciones de memoria excesivas o repetitivas dentro del bucle principal de renderizado (`nativeRender`).
