# Problemas de toolchain específicos de este tipo de proyecto

## Rutas con espacios rompen `vita-pack-vpk`

Si el directorio del proyecto (o cualquier ancestro) tiene un espacio en el nombre, `vita-pack-vpk` (invocado
desde `vita.cmake`) puede partir mal el string de flags que le pasa CMake y fallar o generar un `.vpk` mal
armado, de forma no siempre obvia (a veces "funciona" pero el vpk queda corrupto). Workaround: compilar a
través de un symlink sin espacios, con el directorio de build también fuera de cualquier ruta con espacios:

```bash
ln -s "/ruta/con espacios/Mi Proyecto" ~/proyecto-src
mkdir ~/proyecto-build && cd ~/proyecto-build
export VITASDK=~/vitasdk && export PATH="$VITASDK/bin:$PATH"
cmake ~/proyecto-src -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.ncpu)   # o nproc en Linux
```

Alternativa más robusta si se automatiza en un script: compilar directo en un directorio temporal
(`/tmp/proyecto-build`) y `rsync`/`cp` el resultado final a la carpeta con espacios, en vez de un symlink
manual — evita tener que recordar recrear el symlink si se borra.

**Estos symlinks/directorios de build no son parte del repo** — pueden desaparecer entre sesiones (limpieza
del sistema, `~` compartido con otras cosas, etc.) sin que nadie los haya tocado a propósito. Verificar que
existan antes de asumir que un comando de build de una sesión anterior va a funcionar directo.

## `CMAKE_POLICY_VERSION_MINIMUM` con CMake nuevo

Versiones recientes de CMake exigen que el proyecto (o sus dependencias vendoreadas) declaren una versión
mínima de política más nueva que la que tienen escrita en su propio `cmake_minimum_required()`. Si el build
falla con un error de "Compatibility with CMake < X will be removed" que aborta la configuración (no solo un
warning), agregar `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` (o el valor que pida el mensaje) al invocar `cmake`
resuelve sin tener que tocar los `CMakeLists.txt` vendoreados de dependencias de terceros.

## Reconfigurar antes de asumir que un flag ya está activo

Si un build anterior quedó cacheado con un flag distinto (por ejemplo `EMULATOR_BUILD=ON` de una sesión de
pruebas en emulador, o `CMAKE_BUILD_TYPE=Release` sin logging de debug), `make` solo no lo cambia — hay que
volver a correr `cmake` con los flags nuevos sobre el mismo directorio de build para que tome efecto:

```bash
cmake . -DCMAKE_BUILD_TYPE=Debug -DEMULATOR_BUILD=OFF
```

Chequear `CMakeCache.txt` (`grep -E "CMAKE_BUILD_TYPE|EMULATOR_BUILD" CMakeCache.txt`) antes de rebuildear si
hay dudas de qué configuración quedó activa de una sesión anterior.
