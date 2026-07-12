# Progreso del Port de Zenonia 3 (PS Vita)

Este archivo sirve como bitácora de los problemas técnicos encontrados y las soluciones implementadas durante el port de Android a PS Vita usando SoLoader.

## 1. Crashes iniciales de libc/pthread
- **Síntoma:** Data Abort al iniciar dentro de `__cxa_guard_acquire`.
- **Causa:** El juego en Android usaba `Bionic libc`, donde `PTHREAD_MUTEX_INITIALIZER` asigna un valor estático (`0x4000` o `0x0`). VitaSDK trata este valor directamente como un puntero y falla al intentar desreferenciarlo en `pthread_mutex_lock`.
- **Solución:** Se implementó `pthread_mutex_lock_wrapper` y `pthread_mutex_unlock_wrapper` en `dynlib.c` para detectar si el mutex es `NULL` o `0x4000` e inicializarlo dinámicamente antes de bloquearlo mediante `pthread_mutex_init`.

## 2. Escalado y Resolución del Juego (Cuadro pequeño)
- **Síntoma:** El logo de Gamevil y el juego se renderizaban en un pequeño cuadro en la pantalla en lugar de abarcarla por completo.
- **Causa:** El motor estaba intentando dibujar con una resolución de 800x480 u otras internas, mientras Vita configuraba su buffer en 960x544, causando que el renderizado se viera sin escalar.
- **Solución:** 
  1. En `main.c`, se configuró `NativeInitWithBufferSize` y `NativeResize` para enviarle al motor una resolución de `800x480`.
  2. En `dynlib.c`, se interceptó `glViewport_wrapper` para forzar a que siempre se utilice la pantalla completa nativa de la Vita (`960x544`).
  3. Se interceptó `glOrthof_wrapper` para que cuando el motor solicite su espacio de 800x480 (`0, 800, 480, 0`), `vitaGL` lo escale por hardware automáticamente al viewport forzado.

## 3. Data Abort al cargar mapas (Pantalla negra)
- **Síntoma:** El juego fallaba y daba un error Data Abort en `CMvMapModule::DrawScroll` después de presionar "Start".
- **Causa (Bug de Nexus Engine):** En `CMvLayerData::PreLoad` (dirección `0x9a7b4`), el motor realiza una comprobación con `ble` sobre un puntero de mapa. En la PS Vita, las direcciones en RAM inician en `0x81xxxxxx`, lo cual el procesador en modo `signed` interpreta como negativo. Esto causa un aborto silencioso de la carga, dejando el array del mapa en NULL, lo que hace crashear a `DrawScroll` cuando intenta leerlo.
- **Solución:** Se inyectó un parche dinámico de memoria en `main.c` (mediante `kuKernelCpuUnrestrictedMemcpy`) sobre la dirección `text_base + 0x9a7b4`. Se reemplazó la instrucción Thumb `ble` (`0xdd24`) por `beq` (`0xd024`).

## 4. Texturas e Interfaz en Blanco (o Cuadros Negros)
- **Síntoma:** Las pantallas de título y menú se renderizaban con cuadros blancos (texturas corruptas/incompletas) a pesar de que el juego no crasheaba.
- **Causa:** El motor pide el formato de color `GL_UNSIGNED_SHORT_5_6_5` para texturas grandes y, al ser inicializadas, el motor no configura filtros para mipmaps mediante `glTexParameteri`. Al no haber mipmaps ni filtro lineal/cercano, OpenGL marca las texturas como `Incompletas` (dibujando cuadros blancos).
- **Solución:** Se crearon wrappers para `glTexImage2D` y `glTexSubImage2D` en `dynlib.c` que inyectan los parámetros `GL_TEXTURE_MIN_FILTER` y `GL_TEXTURE_MAG_FILTER` (fijados en `GL_LINEAR`) de manera transparente para que vitaGL renderice todo con normalidad.

---
**Estado Actual:**
- Compilación VPK estable y motor corriendo en loop.
- Modificaciones a espera de testeo final para confirmar el correcto renderizado del HUD y el mapa principal de Zenonia 3.
