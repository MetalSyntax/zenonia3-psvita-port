# Configuración de CMake y Enlaces de Compilación con VitaSDK

La compilación exitosa del cargador de librerías `.so` requiere el uso de librerías del SDK de Vita optimizadas con la ABI de punto flotante en software (`softfp`).

## Banderas del Compilador

Configura las banderas del compilador en tu `CMakeLists.txt` para forzar el uso de `softfp` y habilitar optimizaciones matemáticas rápidas de ARM NEON:

```cmake
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -O3 -g -ffast-math -mfloat-abi=softfp -Wno-deprecated")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=gnu++20 -Wno-write-strings -Wno-psabi")
```

## Librerías Requeridas en el Linker

Para ports basados en Cocos2d-x y OpenGL ES, debes enlazar las siguientes librerías:

* **vitaGL**: Wrapper de OpenGL a GXM (API nativa de la Vita).
* **vitashark** / **SceShaccCg**: Para compilar y procesar shaders dinámicos de OpenGL.
* **kubridge**: Kernel plugin puente que permite a aplicaciones en modo usuario mapear y ejecutar binarios dinámicos.
* **mathneon**: Optimizaciones matemáticas para el hardware de la Vita.
* **openal / vorbisfile / ogg**: Soporte para reproducción de música y efectos de audio espacial.

Ejemplo en `CMakeLists.txt`:
```cmake
target_link_libraries(${CMAKE_PROJECT_NAME}
    -Wl,--whole-archive pthread -Wl,--no-whole-archive
    stdc++
    vitaGL
    vitashark
    SceShaccCg_stub
    mathneon
    kubridge_stub
    openal
    vorbisfile
    ogg
    ...
)
```
