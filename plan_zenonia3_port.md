# Plan de Port de Zenonia 3 para PS Vita

> **Estrategia (actualizada 2026-07-09):** Zenonia 3 usa el **mismo motor Gamevil Nexus2/"Clet"**
> que Zenonia 2 (confirmado por análisis de símbolos, ver Fase 1). En vez de repetir desde cero el
> camino de errores ya recorrido en [`../Zenonia2-vita`](../Zenonia2-vita) — que hoy tiene un port
> funcional en consola real, con menú navegable y partida iniciable — este plan **forkea esa base de
> código probada** (`so_util.c/h`, `FalsoJNI` vendorizado, `dynlib.c`, `java.c`, `main.c`,
> `CMakeLists.txt`) y la adapta a las diferencias de ABI reales que sí existen entre ambos juegos.
> `../Zenonia2-vita/port_progress.md` es lectura obligatoria: documenta ~13 bugs reales resueltos en
> hardware físico (no en teoría) para este mismo motor, y la mayoría aplica aquí sin cambios.

## 0. Qué es idéntico y qué cambió respecto a Zenonia 2 (Fase 1 — completada)

Análisis realizado sobre `zenonia3/lib/armeabi/libgameDSO.so` (stripped, ELF32 ARM) con `objdump -T`
(dynsym) y sobre `decompiled_so/out_ghidra.c` (Ghidra headless), contrastado contra
`zenonia3_java/sources/com/gamevil/nexus2/Natives.java` (jadx) y el código ya escrito de Zenonia 2.

### Idéntico (reutilizable sin cambios de lógica)
- **Misma clase JNI:** todos los métodos nativos siguen exportándose como
  `Java_com_gamevil_nexus2_Natives_*` — Gamevil no renombró el paquete del motor entre juegos, solo el
  paquete de la UI (`com.gamevil.zenonia3.*` en vez de `com.gamevil.zenonia2.*`).
- **Sin `RegisterNatives`:** `JNI_OnLoad` (línea 147630 de `out_ghidra.c`) solo guarda el `JavaVM*` en
  una global — la resolución de nativos es 100% por convención de nombre (`dlsym`), igual que Zenonia 2.
  Esto confirma que **FalsoJNI completo (no un fake-JNI hecho a mano)** sigue siendo obligatorio — ver
  la lección del bug #1 en `port_progress.md` §9.1 (memory corruption por vtable JNI incompleta).
- **Mismo mecanismo de framebuffer interno de software (RGB565 vía `glTexSubImage2D`), pero con
  resolución configurable — diferencia real, ver tabla abajo.** `getDeviceInfo()` (línea 148015) solo
  usa `di[3]=400, di[4]=240` como default perezoso la primera vez que se llama (antes de que corra
  ningún `NativeInit*`). `glDrawFrame()` (línea 148035) confirma que el ancho/alto real que se sube con
  `glTexSubImage2D(GL_TEXTURE_2D,0,0,0,w,h,GL_RGB,GL_UNSIGNED_SHORT_5_6_5,buffer)` se leen de
  `di[3]`/`di[4]` **en el momento de dibujar cada frame**, no un valor fijo compilado. Se espera que los
  3 bugs gráficos de la Fase 10 de Zenonia 2 reaparezcan igual sin importar la resolución interna:
  RGB565→RGBA8888, filtro de textura incompleta, y vértices `GL_FIXED` (Q16.16) en `glDrawArrays`.
- **Mismo pipeline GLES1 fijo:** imports idénticos — `glClearColorx`, `glTexParameterx` (punto fijo),
  `glVertexPointer`/`glTexCoordPointer`/`glColorPointer`, sin VBOs ni shaders.
- **Mismos bugs de asset-loading esperables:** `readAssets`/`readAssete` (typo real confirmado con
  `strings` — ambas cadenas existen en el binario) e `isAssetExist` siguen presentes tal cual.
- **Convención Dalvik `ArrayObject` de 16 bytes:** el motor sigue compilado contra el NDK viejo que lee
  el resultado de `readAssets` por puntero crudo, no vía `GetByteArrayElements`.

### Diferencias reales confirmadas (no asumir, verificar en consola)
| Zenonia 2 | Zenonia 3 | Nota |
|---|---|---|
| `NativeInit()` (sin args) | **No existe** en el `.so`. Reemplazado por `NativeInitDeviceInfo(int,int)` + `NativeInitWithBufferSize(int,int)` | Java declara igual `NativeInit()`, pero no tiene símbolo exportado — es una declaración muerta, no llamarla. **Hallazgo clave:** `NativeInitDeviceInfo(w,h)` escribe `w`/`h` directo en la estructura interna que `glDrawFrame()` usa para el tamaño real del `glTexSubImage2D` de cada frame (`out_ghidra.c:148075-148103` — offsets `di+0xc`/`di+0x10`, los mismos que Zenonia 2 deja fijos en 400/240 por default). O sea: **Zenonia 3 puede renderizar a una resolución interna elegida por nosotros** (candidato: 960×544, la nativa de Vita) en vez de forzar el upscale de un buffer 400×240 fijo como en Zenonia 2 — hay que probarlo, con el riesgo de que la UI del motor tenga posiciones de HUD/menú calculadas para una resolución de teléfono específica y no para 16:9. Si la UI se rompe con 960×544, probar una resolución de teléfono real de la época (ej. 480×800 o 800×480) y dejar que vitaGL escale el quad final a pantalla completa. El buffer interno de scratch que aloca el motor es cuadrado y cachea por umbral (`di[0]=0x400` si `w` está en 513–1024, `0x800` si `w>1024`, luego `malloc(di[0]*di[0]*2)`), así que cualquier `w` hasta 1024 entra sin más cambios. **Orden de llamada confirmado con un crash real en consola (ver `port_progress.md` Fase 3.2):** `NativeInitWithBufferSize` va **antes** que `NativeInitDeviceInfo`, no al revés — el primero dispara `startClet()→GxCreateGlobalHeap()`, que crea el pool de memoria propio del motor (`Gcx_MM_Init`, separado de `malloc`/`new`) del que depende el buffer de píxeles que aloca el segundo. Llamarlos en el orden equivocado crashea con `NULL` dentro de `Gcx_MM_Alloc`. |
| `setInputEvent(type,p1,p2)` | **No existe** en el `.so` (tampoco declarada en `Natives.java` de Zenonia3) | El protocolo de "doble entrega" (inmediata + encolada) de Zenonia 2 no aplica. Solo hay `handleCletEvent`. |
| `handleCletEvent(type,p1,p2)` — 3 ints | `handleCletEvent(int,int,int,int)` — **4 ints** | Confirmado en `Natives.java` y en la firma real del JNI export (`out_ghidra.c:147900`, 4 argumentos reales tras env/jclass). Body interno: para touch (códigos `0x17/0x18/0x19`) empaqueta 3 valores en un array y llama a una función interna `handleCletEvent(tipo, 0, &array)` — sugiere `(x, y, pointerId)`; para teclas usa solo el primer valor. |
| — | `NativeAsyncTimerCallBack(TimeStemp)`, `NativeHandleInAppBiiling`, `NativeGetPublicKey`, `NativeResponseIAP` | Nuevas, relacionadas a IAP/timers — no se implementan salvo que bloqueen el arranque. |
| — (import) | `__android_log_print` **no está** en la tabla de imports de este `.so` | El build de Zenonia 3 no llama logging de Android. Un wrapper no hace daño pero no es necesario. |
| `Natives.java`: sin `OnVibrate`/`OnEvent` en `UIListener` | Sí están (`OnVibrate(int)`, `OnEvent(int)`) | Ambos `void` → seguros como no-op si no se registran (ver lección §9.10 de Zenonia2), pero se registran igual para evitar spam de log. |
| Un solo paquete `ui` | Paquete de motor `com.gamevil.nexus2.ui` + paquete de juego `com.gamevil.zenonia3.ui.ZenoniaUIControllerView` | Pendiente decompilar a fondo (Fase 5) para el mapa de teclas HAL y la máquina de estados de UI — no asumir que es idéntico al de Zenonia 2 sin confirmar con jadx. |

**Métodos declarados como `native` en `Natives.java` pero SIN símbolo en el `.so`** (no intentar
llamarlos — son o bien código muerto de una versión anterior, o funcionalidad de una actualización que
este build no incluye): `NativeInit`, `NativeGetPlayerName`, `NativeHandleTapjoyOffer`,
`NativeUnLockItem`.

---

## 1. Guía de Decompilación (ya ejecutada — reproducible)

```bash
# Java del APK (ya hecho, ver zenonia3_java/)
jadx -d "zenonia3_java" "zenonia3.apk"

# Símbolos dinámicos del .so (no requiere toolchain ARM — objdump del sistema alcanza para dynsym)
objdump -T "zenonia3/lib/armeabi/libgameDSO.so" | grep "Java_"     # exports JNI
objdump -T "zenonia3/lib/armeabi/libgameDSO.so" | grep "UND"       # imports (libc/GL/etc)
strings "zenonia3/lib/armeabi/libgameDSO.so" | grep -i readAsset   # confirmar typos reales del binario

# Pseudo-C vía Ghidra headless en Docker (ya hecho, ver decompiled_so/out_ghidra.c)
docker run --rm \
  -v "zenonia3/lib/armeabi:/input" \
  -v "decompiled_so:/output" \
  devrvk/so-decompiler /input/libgameDSO.so /output/out_ghidra.c
```

> [!NOTE]
> `arm-vita-eabi-nm`/`objdump` de VitaSDK no son necesarios para esta parte: como el `.so` es un ELF32
> ARM estándar, el `objdump`/`strings` del sistema (Xcode CLT en este Mac) ya leen su tabla de símbolos
> dinámicos sin problema — solo hace falta el toolchain ARM para *compilar* el loader, no para
> inspeccionar el binario del juego.

---

## 2. Plan del Port (Hitos y Fases)

### Fase 1: Análisis Estático de Símbolos e Inicialización JNI — ✅ Completada
Ver sección 0 arriba. Resultado: mismo motor, 4 diferencias de ABI reales identificadas y documentadas.

### Fase 2: Bootstrap del Loader (fork de Zenonia2-vita) — en progreso
1. **Copiar infraestructura genérica sin cambios:** `loader/so_util.c/h` (SoLoader con soporte
   kubridge real — `SCE_KERNEL_MEMBLOCK_TYPE_USER_RX` para el segmento de texto, imprescindible en
   hardware real, ver bug #3 de Zenonia2 §9.7) y `lib/falso_jni/*` (FalsoJNI vendorizado desde Prince
   of Persia, MIT, ~230 funciones de `JNINativeInterface` implementadas correctamente).
2. **Adaptar `loader/java.c`:** mismos handlers de `readAssets`/`readAssete`/`isAssetExist` (con el
   truco de header Dalvik de 16 bytes y `fstat` en vez de `ftell`), rutas cambiadas a
   `ux0:data/zenonia3/`. Agregar `OnVibrate`/`OnEvent` como no-ops (nuevos en Zenonia3, ver tabla).
3. **Adaptar `loader/dynlib.c`:** mismos wrappers GLES1 (RGB565→RGBA8888, filtros forzados,
   `GL_FIXED`→`GL_FLOAT` diferido a `glDrawArrays`), mismos hooks de archivo
   (`fopen_hook`/`stat_hook`/`access_hook` → `ux0:data/zenonia3/assets/`).
4. **Adaptar `loader/main.c`:** cargar `libgameDSO.so` (no `libzenonia2.so`), llamar
   `NativeInitDeviceInfo`+`NativeInitWithBufferSize` en vez de `NativeInit` inexistente, usar
   `handleCletEvent` de 4 argumentos como único canal de entrada (no hay `setInputEvent`). **No portar
   el parche binario `apply_so_patches()` de Zenonia 2 tal cual** — es un offset (`0xaec38`) específico
   del `.so` de Zenonia 2; si aparece el mismo patrón de crash (puntero de heap alto tratado como
   negativo, ver §11.1 de Zenonia2), hay que re-derivar el offset con `vita-parse-core` +
   `objdump -d` sobre *este* binario, no reusar el número.
5. **`CMakeLists.txt`/`build.sh`:** copiar la configuración de librerías/flags que ya funciona
   (`vitaGL`, `kubridge_stub`, `pthread` con `--whole-archive`, `ATTRIBUTE2=12`, `UNSAFE NOASLR`),
   `VITA_TITLEID` nuevo (no reusar `PSVZ00002`), nombre de proyecto `zenonia_3`.
6. **Primer build:** `cmake . && make` — objetivo de esta fase es que compile, no que corra. Corregir
   errores de linkeo contra la lista real de imports de la Fase 1 (92 símbolos confirmados).

### Fase 3: Primera Puesta en Marcha (Vita3K primero, consola real después)
Metodología igual a `port_progress.md` §9-9.13 de Zenonia2: **un log a la vez, un bug a la vez**.
1. Log con timestamp por corrida en `ux0:data/zenonia3/logs/` (no pisar el mismo `log.txt`).
2. Instalar en Vita3K (`-B OpenGL`) para descartar crashes de lógica antes de gastar ciclos de
   transferencia FTP a la consola física.
3. Correr sin `gl_init()` primero si hace falta aislar si un crash es de JNI/carga o de vitaGL — misma
   táctica que aisló el bug de FakeJNI en Zenonia2 §9.1.
4. **Esperar y triagear en orden esta clase de síntomas** (ya vistos en Zenonia2, buscar primero antes
   de inventar teoría nueva):
   - `Prefetch Abort` en el PC de `JNI_OnLoad` → memoria del `.so` sin permiso RX (bug #3, revisar que
     `so_util.c` no se haya revertido a la variante sin kubridge).
   - `"method ID N not found!"` para un método `int`/`boolean`/etc. → registrar en
     `methodsInt[]`/`methodsBoolean[]` de `java.c` (FalsoJNI devuelve `-1` para no-encontrado en tipos
     numéricos, que el motor interpreta como "true"/"existe").
   - `GetStaticMethodID(..., "NOMBRE", ...): not found` para un método `Object` seguido de crash en
     `memcpy` → typo real del binario (`strings libgameDSO.so | grep -i nombre_parecido`), registrar
     ambas grafías apuntando al mismo handler.
   - `MALLOC FAILED FOR SIZE <número que decodifica a ASCII>` → `ftell()` devolviendo basura, usar
     `fstat` (ya viene así en el `java.c` portado).
5. **Confirmar en consola real ni bien Vita3K deje de crashear** — Vita3K puede ser una plataforma de
   prueba inestable entre sesiones largas (ver §9.4 de Zenonia2: un crash "100% reproducible" resultó
   ser degradación de la sesión del emulador, no un bug del port). No perseguir un crash de Vita3K por
   más de una sesión sin cruzarlo con hardware real.

### Fase 4: Gráficos (GL Wrappers) — riesgo alto de reaparecer igual que en Zenonia 2
1. RGB565→RGBA8888 en `glTexImage2D`/`glTexSubImage2D` (conversión en CPU, ya en el `dynlib.c` portado).
2. Forzar `GL_LINEAR` en min/mag filter en cada `glTexImage2D` (evita "textura incompleta" = blanco).
3. Forzar `glEnableClientState` para vértices/texcoords/color en sus wrappers respectivos.
4. `GL_FIXED` (Q16.16) → `GL_FLOAT` diferido hasta `glDrawArrays` (el único draw call de este motor).
5. Diagnóstico con `sceDisplayGetFrameBuf` si la pantalla queda blanca de nuevo, para confirmar si
   vitaGL está tomando el display antes de sospechar del contenido — no asumir, medir (§10.5 Zenonia2).

### Fase 5: UI de Java y Protocolo de Input — **no asumir que es igual a Zenonia 2, decompilar**
La pantalla blanca de logo/título en Zenonia 2 resultó ser una capa de `ImageView` de Android que el
loader nunca dibuja (mitigado con un splash overlay de `bg0.png`). Es razonable esperar el mismo patrón
acá, pero **el mapa de teclas HAL, los códigos `MH_*` de evento, y la máquina de estados de UI deben
reconfirmarse leyendo el Java real de Zenonia 3**, no copiarse a ciegas de Zenonia 2:
1. `grep -rn "getHalKeyCode\|MH_POINTER\|MH_KEY" zenonia3_java/sources/com/gamevil/` — confirmar si
   `NexusHal`/`Zenonia2UIControllerView` (nombres de clase del motor, quizás sin renombrar) siguen
   existiendo con los mismos valores, o si `ZenoniaUIControllerView` (paquete `zenonia3.ui`) cambió algo.
2. Documentar la resolución interna real usada para convertir touch (`convertScreenX/Y`) — Zenonia 2 usa
   400×240; confirmar que Zenonia 3 no cambió esto pese al framebuffer interno idéntico.
3. Implementar el splash overlay (mismo mecanismo que Zenonia2 §11.2) usando `g_ui_status` de
   `OnUIStatusChange` una vez que se confirme el patrón de estados.

### Fase 6: Audio y Efectos de Sonido (pendiente en ambos ports — territorio nuevo)
1. Captura de `OnSoundPlay(id, vol, loop)` / `OnStopSound()` (ya interceptados como stub en `java.c`).
2. Decompilar `NexusSound.java` (paquete `com.gamevil.nexus2.ui`, compartido con Zenonia2) para
   entender el mapeo id→archivo real de Zenonia 3 (`.mmf`/`.ogg`, ubicación en `assets/`).
3. Mapear a `sceAudioOut`/`SceAudio_stub` con hilos dedicados; usar `Tremor` si el formato es Ogg Vorbis.

### Fase 7: Parches Binarios Puntuales (bajo demanda, no preventivo)
El bug de Zenonia2 §11.1 (`cmp rX,#0; ble` sobre un puntero de heap tratado como `long` con signo,
porque el heap de Vita vive en direcciones altas/negativas y el de Android en direcciones bajas) es un
patrón de motor viejo que **puede reaparecer en cualquier función de este binario**, no solo en
`CMvMap`. Si aparece un `NULL` "imposible" en datos que debieron cargarse:
1. `vita-parse-core` sobre el `.psp2dmp` contra el `.elf` con símbolos de este build.
2. `objdump -d` de la función señalada por LR, buscar `cmp rX, #0` seguido de `ble`/`bgt` (con signo)
   inmediatamente antes/después de donde se debería haber asignado el dato.
3. Parchear en memoria con `kuKernelCpuUnrestrictedMemcpy` después de `so_relocate`/`so_resolve` y antes
   de `so_flush_caches`, verificando los bytes originales antes de escribir (nunca a ciegas).

---

## 3. Riesgos ya conocidos que se heredan de Zenonia 2 (releer antes de "descubrirlos" de nuevo)
- `pthread` necesita `-Wl,--whole-archive ... --no-whole-archive` porque FalsoJNI usa
  `pthread_mutex_t`; eso arrastra objetos de `libc.a` que duplican símbolos de `SceLibKernel_stub` — no
  linkear `SceLibKernel_stub` si no hace falta una API específica de él.
- `ENABLE_VERBOSE_JNI_LOG` (log de cada llamada JNI) es indispensable para el primer arranque pero
  **debe apagarse** antes de medir rendimiento real (cada línea es `sceIoOpen`+`sceIoWrite`+`sceIoClose`).
- `vglInitExtended` devolviendo `GL_FALSE` a 960×544 es el resultado sano (esa resolución nunca cae en
  el fallback), no tratarlo como fallo de init.
- Rutas con espacio en `PSVITA Develop`: usar un symlink en `/tmp` o `$HOME` para el directorio de build
  (`build.sh` de Zenonia2 ya resuelve esto, portado sin cambios).
- `libshacccg.suprx` debe estar en `ur0:data/` en la consola de destino (requisito de `vitaGL`, no de
  este port) — confirmar antes de reportar un crash de shaders como bug propio.

## 4. Riesgos nuevos confirmados en Zenonia 3 (no presentes o no detectados en Zenonia 2)
- **Mutex/condvar estáticos de Bionic en cero.** `pthread_mutex_t`/`pthread_cond_t` en VitaSDK son
  punteros (`struct pthread_mutex_t_ *`) a una estructura que aloca `pthread_mutex_init`/`cond_init`.
  Bionic permite mutex/condvar estáticos con `PTHREAD_MUTEX_INITIALIZER`/`PTHREAD_COND_INITIALIZER` que
  quedan en ceros sin llamar nunca a esas funciones (Bionic trata los ceros como "válido, destrabado"
  de forma nativa) — patrón común en el propio libstdc++ interno (node allocator de STL), no solo en
  código del juego. En VitaSDK esos ceros son un puntero `NULL` real y la implementación de PTE
  (pthreads-embedded) lo desreferencia sin chequear, crasheando. **Confirmado con un crash real** (ver
  `port_progress.md` Fase 3.3) dentro del node-allocator de libstdc++, no en código del motor. Fix
  aplicado: wrappers de `pthread_mutex_lock/unlock/destroy` y `pthread_cond_wait/broadcast` que detectan
  el puntero `NULL` y llaman `pthread_mutex_init`/`pthread_cond_init` on-demand antes de usar el real.
  Si aparece un crash nuevo dentro de otra función `pthread_*` con el mismo patrón (primer campo del
  mutex/condvar en `0`), aplicar el mismo parche ahí.
