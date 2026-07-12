# Registro de Progreso del Port de Zenonia 3 (PS Vita)

Este juego usa el mismo motor que [`../Zenonia2-vita`](../Zenonia2-vita) (Gamevil Nexus2/"Clet") —
ese proyecto ya tiene un port funcional en consola real (menú navegable, partida iniciable) y su
`port_progress.md` documenta ~13 bugs reales resueltos ahí. Este documento solo registra lo que es
**específico de Zenonia 3** (diferencias de ABI, decisiones tomadas al portar el código, y lo que se
confirme distinto en pruebas reales) — no repite la narrativa completa de Zenonia 2.

## Fase 1: Análisis Estático de Símbolos — Completada (2026-07-09)

Herramientas: `objdump -T`/`strings` del sistema (no hace falta el toolchain ARM de VitaSDK para leer
símbolos dinámicos de un ELF, solo para compilar) sobre `zenonia3/lib/armeabi/libgameDSO.so`, más
`decompiled_so/out_ghidra.c` (Ghidra headless vía `devrvk/so-decompiler`) y
`zenonia3_java/sources/com/gamevil/nexus2/Natives.java` (jadx).

**Resultado principal:** mismo motor que Zenonia 2 (misma clase `com.gamevil.nexus2.Natives`, sin
`RegisterNatives`, resolución JNI 100% por convención de nombre). Ver `plan_zenonia3_port.md` sección 0
para la tabla completa de diferencias de ABI confirmadas. Resumen:

- `NativeInit()` no existe en este `.so` → reemplazado por `NativeInitDeviceInfo(w,h)` +
  `NativeInitWithBufferSize(w,h)`.
- `setInputEvent` no existe → solo `handleCletEvent`, que pasó de 3 a 4 argumentos.
- **Hallazgo importante:** `NativeInitDeviceInfo(w,h)` escribe directo los offsets que `glDrawFrame()`
  lee como ancho/alto real del framebuffer de software subido cada frame (`out_ghidra.c:148048-148055`
  vs. `148075-148103`) — a diferencia de Zenonia 2 (fijo en 400×240), **Zenonia 3 puede rendear a la
  resolución interna que nosotros le pidamos** (hasta 1024 de ancho sin cambiar el tamaño del buffer de
  scratch que aloca el motor). Se eligió arrancar con 960×544 (nativa de Vita) — pendiente de confirmar
  en Fase 3/4 si la UI del motor (posiciones de HUD/menú) se rompe con esa resolución o si hace falta
  una resolución de teléfono real de la época (candidatos: 480×800, 800×480).
- Import list confirmada (92 símbolos vía `objdump -T | grep UND`): mismo pipeline GLES1 fijo que
  Zenonia 2 (incluye `glClearColorx`/`glTexParameterx` de punto fijo), pero **sin
  `__android_log_print`** — este build no llama logging de Android, así que no se registra en
  `dynlib.c` (la función sigue existiendo en `main.c` por si un build futuro la necesita).
- Métodos declarados `native` en `Natives.java` pero sin símbolo en el `.so` (no llamar):
  `NativeInit`, `NativeGetPlayerName`, `NativeHandleTapjoyOffer`, `NativeUnLockItem`.
- `UIListener` de Zenonia 3 agrega `OnVibrate(int)` y `OnEvent(int)` respecto a Zenonia 2 — registrados
  como no-ops en `java.c` para evitar spam de log (ambos son `void`, ya serían seguros sin registrar).

## Fase 2: Bootstrap del Loader — En progreso (2026-07-09)

Se forkeó la base de código probada de Zenonia2-vita en vez de escribir un soloader desde cero:

- **Copiado sin cambios** (código genérico, no depende del motor del juego):
  `loader/so_util.c`/`so_util.h` (soloader con soporte kubridge real — memoria `USER_RX` para el
  segmento de texto, imprescindible en hardware real per el bug #3 de Zenonia2 §9.7) y
  `lib/falso_jni/*` (FalsoJNI vendorizado desde Prince of Persia).
- **`loader/java.c` adaptado:** mismos handlers de `readAssets`/`readAssete`/`isAssetExist`
  (con el truco de header Dalvik de 16 bytes y `fstat` en vez de `ftell`, ambos confirmados necesarios
  en Zenonia 2 y aplicables aquí por ser el mismo motor), rutas cambiadas a `ux0:data/zenonia3/`, y se
  agregaron `OnVibrate`/`OnEvent` como no-ops nuevos de Zenonia 3.
- **`loader/dynlib.c` adaptado:** mismos wrappers GLES1 (RGB565→RGBA8888, filtros de textura forzados,
  conversión `GL_FIXED`→`GL_FLOAT` diferida a `glDrawArrays`), tabla de imports recortada a los 92
  símbolos confirmados en la Fase 1 (se quitó `__android_log_print`, se agregaron algunos libc que
  Zenonia 2 no importaba pero Zenonia 3 sí: `fseek`, `ftell`, `sprintf`, `puts`, `close`, `fcntl`,
  `select`, `gettimeofday`, `ceil`, `exit`, `raise`, y los `pthread_*` que antes solo se enlazaban vía
  `--whole-archive` sin wrapper explícito).
- **`loader/main.c` adaptado:** carga `libgameDSO.so` (no `libzenonia2.so`); secuencia de arranque
  `JNI_OnLoad` → `NativeInitDeviceInfo(960,544)` → `NativeInitWithBufferSize(960,544)` →
  `NativeResize(960,544)` → `NativeResumeClet()` (reemplaza al `NativeInit()` único de Zenonia 2, que no
  existe aquí); input solo por `handleCletEvent` de 4 argumentos, sin la entrega inmediata que hacía
  `setInputEvent` en Zenonia 2 (no existe ese símbolo en este `.so`). **No se portó
  `apply_so_patches()`** (el parche binario de Zenonia 2 es un offset específico de ese `.so` — ver
  Fase 7 del plan si aparece un síntoma similar aquí).
- **`CMakeLists.txt`/`build.sh`:** mismo set de librerías (`vitaGL`, `kubridge_stub`, `pthread` con
  `--whole-archive`, sin `SceLibKernel_stub` por la misma razón que en Zenonia 2 — colisión de símbolos
  de `libc.a` arrastrados por `--whole-archive pthread`), `VITA_TITLEID=PSVZ00003` (distinto del
  `PSVZ00002` de Zenonia 2 para poder tener ambos instalados), proyecto `zenonia_3`.

**Pendiente de esta fase:** primer `cmake . && make` real (task siguiente). Todo lo anterior es código
escrito por analogía con un motor confirmado idéntico, pero **sin ejecutar todavía** — tratar cualquier
valor no marcado como "confirmado" en la Fase 1 (los parámetros de `NativeInitDeviceInfo`, el protocolo
de touch, los códigos HAL) como hipótesis a verificar con el primer log real, no como hecho.

## Fase 2: Primer build — Completada (2026-07-09)

`cmake . && make` corre limpio (usando un symlink en `/tmp` para evitar el problema de ruta con espacio
de `PSVITA Develop`, ver `toolchain_gotchas.md`). Se encontraron y corrigieron 2 includes faltantes al
portar el código de Zenonia 2 (que los tenía disponibles transitivamente por otro header, o simplemente
no los necesitaba):
- `loader/dynlib.c`: faltaban `<fcntl.h>`, `<sys/time.h>`/`<sys/select.h>` y `<pthread.h>` — Zenonia 2 no
  registraba `fcntl`/`gettimeofday`/`pthread_*` explícitamente en su tabla de imports (los dejaba
  resolverse solo por el `--whole-archive pthread` del link), pero Zenonia 3 sí los necesita como
  entradas explícitas en `default_dynlib[]` (import list real, distinta a la de Zenonia 2, ver Fase 1).
- `loader/main.c`: faltaba `<stdlib.h>` para `malloc`/`free` del splash (Zenonia 2 lo tenía incluido
  transitivamente por otro header que aquí no se incluye en el mismo orden).

Se limpiaron también `CMakeCache.txt`/`CMakeFiles`/`Makefile`/`cmake_install.cmake` sueltos en la raíz
del repo — residuo de un intento de build anterior con el `CMakeLists.txt` tutorial (apuntaban a
`/Users/macmini/Downloads/Zenonia2-vita`, una ruta de otra máquina — claramente de cuando este
repo se bootstrapeó copiando la carpeta de Zenonia2-vita). Ya estaban en `.gitignore`, no se perdió nada.

**Resultado:** `build/zenonia_3.vpk` (572 KB) y `build/zenonia_3.elf` (con símbolos, para
`vita-parse-core` cuando haga falta analizar un `.psp2dmp`) generados y copiados al repo.

### Próximo Paso Real
1. Conseguir/generar `zenonia3/assets/` en el layout que `dynlib.c`/`java.c` esperan
   (`ux0:data/zenonia3/libgameDSO.so` + `ux0:data/zenonia3/assets/`) para poder probar el primer
   arranque — sin esto el loader va a fallar en `so_file_load` de inmediato.
2. Primer boot en Vita3K (`build/zenonia_3.vpk`), metodología "un log a la vez" (ver Fase 3 del plan):
   bajar `ux0:data/zenonia3/logs/log_<timestamp>.txt` después de cada intento y mirar la última línea
   antes del corte, no adivinar.
3. Confirmar si `NativeInitDeviceInfo(960,544)` deja la UI del motor usable o si hace falta una
   resolución de teléfono real (ver hallazgo de Fase 1 sobre resolución interna configurable).

## Fase 3: Primera Puesta en Marcha en Consola Real — bug #1 encontrado y corregido (2026-07-09)

Primer intento real en hardware (`logs/log_1783645247.txt`, movido a `logs/` — antes quedaba en la raíz
del repo). El `.so` cargó bien (`so_file_load` ok, `num_dynsym=7179`), pero `so_resolve` reportó 3
símbolos sin resolver y el loader murió con `fatal_error`:

```
[so_util] Unresolved import: __cxa_atexit
[so_util] Unresolved import: __cxa_finalize
[so_util] Unresolved import: __cxa_atexit
[so_util] Unresolved import: __aeabi_atexit
[FATAL] Unknown symbol "__aeabi_atexit" (0x981462b4).
```

**Causa real:** los 3 símbolos SÍ estaban confirmados en la lista de 92 imports de la Fase 1
(`objdump -T libgameDSO.so | grep UND`), pero se me pasaron por alto al transcribir esa lista a la tabla
`default_dynlib[]` de `loader/dynlib.c` — un error de transcripción propio del bootstrap de la Fase 2,
no un bug del análisis. Confirmado con `comm -23` entre la lista de imports real y los nombres
efectivamente registrados en la tabla: exactamente esos 3 eran los únicos faltantes de los 92.

**Solución (`loader/dynlib.c`):** se agregaron como no-ops seguros — este loader nunca hace
`dlclose()`/`exit()` ordenado de una librería dinámica real (el proceso termina con
`sceKernelExitProcess()`), así que no hace falta ejecutar destructores estáticos de C++ registrados con
estas funciones:
```c
int __cxa_atexit(void (*func)(void *), void *arg, void *dso_handle) { return 0; }
void __cxa_finalize(void *dso_handle) {}
int __aeabi_atexit(void *arg, void (*func)(void *), void *dso_handle) { return __cxa_atexit(func, arg, dso_handle); }
```

**Nota general para el próximo bug de este tipo:** antes de investigar teoría nueva sobre un
`Unresolved import`/`Unknown symbol`, correr primero
`comm -23 <(objdump -T libgameDSO.so | grep UND | awk '{print $NF}' | sort -u) <(grep -oE '"[a-zA-Z_][a-zA-Z0-9_]*"' loader/dynlib.c | tr -d '"' | sort -u)`
contra la tabla actual de `dynlib.c` — la lista de 92 imports de la Fase 1 ya es la fuente de verdad
completa, así que cualquier símbolo faltante es casi siempre un olvido de transcripción, no un import
nuevo no detectado.

## Fase 3.2: bug #2 encontrado y corregido — orden de inicialización nativa (2026-07-09)

Con el fix de `__cxa_atexit`/`__cxa_finalize`/`__aeabi_atexit` instalado, el siguiente intento en
consola real avanzó mucho más lejos: pasó `so_resolve`, `gl_init()` (`"vitaGL inicializado."`),
`JNI_OnLoad` — y crasheó dentro de la primera llamada a `NativeInitDeviceInfo(960,544)`
(`logs/log_1783646571.txt`, corte justo después de esa línea; `logs/psp2core-1783646579-...psp2dmp`).

**Diagnóstico con `vita-parse-core`** (`~/vita-tools/vita-parse-core`, contra `build/zenonia_3.elf`):
Data abort con `PC` dentro de `SceLibKernel` (real, no nuestro código) y `LR=0x98100fbb`. Resuelto
contra `libgameDSO.so` con `objdump -T` + `arm-vita-eabi-objdump -d -M force-thumb`:

- `LR` cae exactamente después de un `blx memset@plt` dentro de `Gcx_MM_Calloc(unsigned int)`
  (offset `+0x13`), que a su vez llama a `Gcx_MM_Alloc(n)` y hace `memset(ptr, 0, n)` sobre el
  resultado. `R0` en el momento del crash era `0x0` → `Gcx_MM_Alloc` devolvió `NULL`, y el `memset`
  subsiguiente sobre un puntero nulo es lo que produce el Data Abort.
- Desensamblando `Gcx_MM_Alloc`: es un **allocator propio del motor** (slab/pool con block headers —
  `NewPtrSmall`/`NewPtrMiddle`/`FindWorstFitPtr`/`CreateBlockHeader`), completamente separado de
  `malloc`/`new`. Al principio de la función hay un chequeo explícito: si el puntero global al pool
  (leído vía GOT) es `NULL`, retorna `0` de inmediato — es decir, `Gcx_MM_Alloc` devuelve `NULL` "sanamente"
  si el pool nunca se inicializó.
- Rastreando quién inicializa ese pool: `Gcx_MM_Init(void*, unsigned int)` → `CreateMemory(buffer, size)`.
  Solo hay **un** call site en todo el binario: `GxCreateGlobalHeap()` (reserva ~3 MB, `0xc0<<14` bytes,
  desde un buffer estático propio del `.so`). Y `GxCreateGlobalHeap()` a su vez solo se llama desde
  **`startClet()`**.
- `startClet()` es invocado por `NativeInitWithBufferSize` (`if (bCletStarted == '\0') { startClet(0,0); bCletStarted = 1; }`,
  ver Fase 1) — **no** por `NativeInitDeviceInfo`. Nuestro `main.c` llamaba `NativeInitDeviceInfo` primero
  (por analogía con el orden en que Zenonia 2 no tenía este problema al no dividir la inicialización en dos
  funciones separadas), y `NativeInitDeviceInfo` sí necesita el pool ya creado para pedir el buffer de
  píxeles interno vía `getDeviceInfo() -> Gcx_MM_Calloc`.

**Causa raíz:** orden de llamada nativo incorrecto — `NativeInitDeviceInfo` se llamaba **antes** que
`NativeInitWithBufferSize`, cuando depende de que este último ya haya corrido (crea el heap propio del
motor). Es la primera vez que el orden real de dos funciones "de inicialización" con nombres separados
(en vez del `NativeInit()` único de Zenonia 2) resultó no ser arbitrario.

**Solución (`loader/main.c`):** se invierte el orden — `NativeInitWithBufferSize(960,544)` primero,
`NativeInitDeviceInfo(960,544)` después. `NativeResize`/`NativeResumeClet` sin cambios.

**Patrón para el futuro:** cuando un crash caiga dentro de una función con nombre `Gcx_MM_*`/`GxCreate*`
con un puntero `NULL` "sano" (no basura), sospechar primero de un problema de **orden de inicialización**
entre las funciones `Native*`, no de los wrappers de `dynlib.c` — este motor tiene múltiples pools/heaps
internos que dependen unos de otros en un orden específico que no está documentado en ningún lado más que
en el propio desensamblado.

## Fase 3.3: bug #3 encontrado y corregido — mutex/condvar estáticos "cero" de Bionic (2026-07-09)

Con el fix de orden de 3.2 instalado, el siguiente log (`logs/log_1783647569.txt`) muestra que el
arranque avanzó mucho más: `NativeInitWithBufferSize` corrió sin crashear, y el motor entró en la
inicialización del puente de fuentes Java (`InitializeJNIGlobalRef` → `GFA_CreateNexusFontClass`, ver
Fase 1) — se ven decenas de llamadas JNI reales (`getPhoneNumber`, `getSimSerialNumber`,
`getMacAddress`, `getDeviceID`, `getPhoneModel`, `GFA_IsInitialized`, `GFA_Init`, `GFA_SetTextSize`,
`GFA_CreateFont`, `GFA_SetColor`...). La mayoría no están registradas en `java.c` pero no son fatales
(`Object`/`void`/`boolean` no encontrados devuelven `NULL`/nada/lo que sea de forma segura salvo los
`int`, ver §9.10 de Zenonia2 — ninguno de estos es `int` así que no hay riesgo inmediato de ese patrón).
El log corta justo después de `GFA_SetColor`.

**Diagnóstico con `vita-parse-core`** (`logs/psp2core-1783647576-...psp2dmp`): Data abort **dentro de
nuestro propio `pthread_mutex_unlock`** (de la lib pthread de VitaSDK, enlazada con `--whole-archive`),
instrucción `ldr r1, [r4, #12]` con `r4=0`. `LR` resuelve (vía `objdump -T` + tabla de símbolos del
`.so`) a `std::__node_alloc_impl::_M_allocate(unsigned int&) + 0x54` — el allocador interno de nodos de
libstdc++ (usado por `std::list`/`map`/`set`), que protege su free-list con un mutex.

**Causa raíz — mismatch de ABI entre Bionic (Android) y VitaSDK:** `pthread_mutex_t` en VitaSDK es
literalmente un **puntero** (`typedef struct pthread_mutex_t_ * pthread_mutex_t;`, confirmado en
`sys/_pthreadtypes.h`) a una estructura interna que aloca `pthread_mutex_init()`. En Bionic, un mutex
estático declarado con `PTHREAD_MUTEX_INITIALIZER` (patrón común en libstdc++ para locks internos, sin
llamar nunca a `pthread_mutex_init` en tiempo de ejecución) queda en **puros ceros** — Bionic está
diseñado para tratar esos ceros como "mutex válido, destrabado" de forma nativa. En VitaSDK esos mismos
ceros son un **puntero NULL real**, y `pthread_mutex_lock`/`unlock` de PTE (pthreads-embedded) lo
desreferencian sin chequear que sea no-nulo. `pthread_cond_t` tiene exactamente el mismo problema
(también es un puntero, confirmado en el mismo header) y muy probablemente iba a crashear igual más
adelante si esto no se corregía ahora.

**Solución (`loader/dynlib.c`):** se agregaron wrappers para `pthread_mutex_lock`, `pthread_mutex_unlock`,
`pthread_mutex_destroy`, `pthread_cond_wait`, `pthread_cond_broadcast` que detectan el puntero `NULL`
(mutex/condvar nunca inicializado explícitamente) y llaman a `pthread_mutex_init`/`pthread_cond_init`
**on-demand** antes de pasar la llamada a la implementación real — en vez de asumir que el `.so` siempre
llama `pthread_mutex_init` explícitamente, cosa que Bionic no garantiza.

**Patrón para el futuro:** cualquier símbolo `pthread_*` que tome un `pthread_mutex_t*`/`pthread_cond_t*`
como argumento y no pase ya por un wrapper con este chequeo es candidato al mismo bug si el `.so` usa el
patrón `PTHREAD_MUTEX_INITIALIZER`/`PTHREAD_COND_INITIALIZER` en algún lugar (muy común en código C++
STL/libstdc++ interno, no solo en el código del juego). Si aparece un crash nuevo dentro de otra función
`pthread_*` con el primer campo del mutex/condvar en `0`, aplicar el mismo parche ahí.

## Fase 3.4: bug #4 encontrado y corregido — puente de fuentes GFA sin registrar (2026-07-09)

Con los 3 fixes anteriores instalados, el siguiente log (`logs/log_1783648648.txt`) muestra que el
arranque pasó de largo el `pthread` fix y avanzó exactamente hasta el mismo punto de antes: la
inicialización del puente de fuentes Java (`GFA_IsInitialized`, `GFA_Init`, `GFA_SetTextSize`,
`GFA_CreateFont`, `GFA_SetColor`, `GFA_SetStringFromKSC5601`, `GFA_GetWordwrapPositionEx` — ninguno
registrado, todos "not found"). El log corta en el mismo lugar exacto que la corrida anterior.

**Diagnóstico con `vita-parse-core`** (`logs/psp2core-1783648656-...psp2dmp`): Data abort dentro del
propio `.so`, `PC=LR≈0x980fc028`. Resuelto contra símbolos del `.so`:
`CGxFACharCache::findChar(char const*, int) + 0x10` — el motor intentando buscar un glyph en el cache de
caracteres de fuente, dereferenciando `this+0x3c` (puntero al header de un árbol rojo-negro interno,
`std::priv::_Rb_tree`) que vale `0`.

**Método usado para encontrar la causa exacta** (documentado para reutilizar — combina las 3 fuentes que
tenemos disponibles: `.so` real, `decompiled_so/out_ghidra.c`, y `zenonia3_java/`):
1. `arm-vita-eabi-objdump -d -M force-thumb --start-address=<LR-0x10> --stop-address=<LR+0x10>
   libgameDSO.so` — desensamblado real de `findChar`, confirma que llama a una función virtual
   (`vtable+0x24`) y luego dereferencia `this+0x3c` sin chequear `NULL`.
2. El pseudo-C de Ghidra (`decompiled_so/out_ghidra.c`, buscando el nombre demangled de la función) da la
   MISMA lógica en C legible más rápido de leer que el ensamblador — confirma `this+0x3c` como puntero a
   header de árbol.
3. La construcción del objeto (`CGxFACharCache::CGxFACharCache()` en el pseudo-C) muestra que `this+0x3c`
   se inicializa explícitamente a `0` en el constructor y **nunca se vuelve a tocar ahí** — hace falta
   encontrar quién lo puebla después.
4. `IGxFACharCache::createInstance()`/`getInstPtr()` confirman un singleton lazy (`operator new` +
   ctor real, backeado por nuestro `malloc_wrapper`, sin depender del pool interno `Gcx_MM_*` — así que
   el objeto en sí se crea bien).
5. La vtable REAL (no el placeholder que Ghidra sintetiza con nombres tipo `PTR__ClassName_1_00XXXXXX`,
   que no es una dirección válida en tiempo de ejecución) se obtuvo del símbolo dinámico exportado
   `_ZTV14CGxFACharCache` (mangling Itanium del vtable-con-RTTI) vía `objdump -T`, y su contenido real
   con `objdump -s -j .data.rel.ro --start-address=<addr>`. El puntero de vtable visible en el objeto es
   esa dirección **+8** (se saltean los 2 slots de offset-to-top/RTTI). Leyendo la entrada en el offset
   `0x24` de ahí se identificó la función virtual real: `CGxFACharCache::CharToCharUnit(char const*, int,
   int)` — no toca `this+0x3c`, así que la responsabilidad de poblarlo está en otro lado.
6. Rastreando hacia atrás desde `CGxFontAndroid::Create()` (que es quien arma cada fuente nueva) en el
   pseudo-C: llama `GFA_CreateFont(nombre, 0)` (la llamada JNI) y **solo si el handle devuelto es `>= 0`**
   llama a las funciones virtuales `addFont`/`setFont`/`setEncoding` del char cache (identificadas por
   offset de vtable: `+0xc`, `+0x10`, `+0x1c`, que resolví contra la MISMA vtable real de arriba) — esas
   son las que finalmente pueblan el árbol (`this+0x3c`).

**Causa raíz — mismo patrón exacto que `isAssetExist` en Zenonia2 §9.10, polaridad distinta:**
`GFA_CreateFont` es `int` y no estaba registrado en `java.c` → FalsoJNI devuelve `-1` por default para
métodos `int` no encontrados → el chequeo nativo `if (-1 < iVar1)` (¡`-1 < -1` es falso!) determina
correctamente que la fuente es inválida y **salta** el registro en el char cache → nadie llama nunca
`addFont`, el árbol queda en `NULL` → primer `findChar()` real (durante el dibujo/medición de cualquier
texto) crashea con Data abort.

**Solución — puente GFA completo, sin rasterizado real de glifos todavía (`loader/java.c`):**
Se leyó la implementación Java real y completa
(`zenonia3_java/sources/com/gamevil/nexus2/NexusFont.java`, respaldada por
`android.graphics.Paint`/`Canvas`/`Bitmap`, ausente de cualquier motor nativo — es la fuente de verdad
exacta de qué se espera de cada función) y se replicó su máquina de estados en C, sin backend de
rasterizado real de glifos:
- `GFA_IsInitialized`/`GFA_Init` (boolean): estado real de inicialización, no siempre `false`/`true` fijo.
- `GFA_CreateFont` (int): reutiliza slot si la familia ya existe (hasta 5, igual que el original),
  devuelve `-1` solo si de verdad no hay slots — igual semántica que Java, en vez del `-1` ciego de
  FalsoJNI para "no encontrado".
- `GFA_SetFont`/`GFA_CharWidth`/`GFA_CharHeight`/`GFA_GetAscent`/`GFA_GetDescent`/`GFA_GetCurrentFont`/
  `GFA_GetColor`/`GFA_GetStringLength` (int): valores derivados del tamaño de texto actual
  (`GFA_SetTextSize`) en vez de `-1` — aproximación monoespaciada (ancho ≈ 60% del alto) ya que no hay
  medición de fuente real todavía.
- `GFA_SetTextSize`/`GFA_SetColor`/`GFA_SetStringFromKSC5601`/`GFA_SetStringFromUnicode`/`GFA_SetString`/
  `GFA_SetTextAlign`/`GFA_SetAntiAlias`/`GFA_SetLocale`/`GFA_ReleaseFont`/`GFA_Release` (void): ya eran
  seguros sin registrar, se registran para no spamear el log y para dejar el estado (tamaño/color)
  consistente para los stubs de arriba.
- `GFA_GetWordwrapPositionEx` (int, con un `int[]` de salida): se devuelve `0` (sin cortes de línea) en
  vez de `-1` — no se implementó el algoritmo de word-wrap real todavía (necesitaría medición de texto
  real), pero `0` es un valor universalmente seguro (mismo que el original devuelve cuando el texto
  completo entra en el ancho máximo).
- **Deliberadamente NO implementado todavía** (no hay evidencia de un crash que lo requiera, y la
  representación de arrays de FalsoJNI para estos casos no está confirmada — ver metodología "un bug a la
  vez"): `GFA_DrawText`, `GFA_DrawFont`, `GFA_GetPixels16`, `GFA_GetPixels32`, `GFA_MeasureText`. Estas
  son las que rasterizan/leen píxeles reales — texto en pantalla se va a ver en blanco/vacío hasta una
  futura **Fase de Texto Real** (renderizador de fuente bitmap propio, ya que replicar
  `android.graphics.Canvas.drawText` fielmente no es viable sin esas APIs). Si aparece un crash nuevo
  llamando a alguna de estas, aplicar la misma metodología (vita-parse-core → objdump → NexusFont.java).

**Patrón general para el futuro:** cuando el motor llama a una API "puente" completa hacia Java
(`GFA_*`, o cualquier otra familia de métodos con un prefijo común) y no está en `Natives.java` sino en
otra clase, buscarla por nombre en `zenonia3_java/sources/` ANTES de adivinar semántica — casi siempre
existe la implementación Java real completa, y leerla ahorra rondas completas de prueba-en-consola.

## Fase 3.5: bug #5 encontrado y corregido — `GFA_DrawFont`/`DrawText`/`MeasureText` desreferencian el array sin chequear NULL (2026-07-09)

El fix de la Fase 3.4 funcionó de verdad: el siguiente log (`logs/log_1783649916.txt`) muestra
`GFA_CreateFont` devolviendo un handle real (`family=driod-sans`), seguido de `GFA_SetFont`,
`GFA_CharWidth`, `GFA_CharHeight`, `GFA_SetColor` — toda la secuencia de `CGxFontAndroid::Create()`
completando sin errores por primera vez. El motor siguió adelante, llamó `GFA_SetString` con un string
de error en coreano (probablemente una validación de USIM/roaming, código muerto en este contexto) y
luego con `"-"`, y finalmente `GFA_DrawFont` — **no registrado** → crash.

**Diagnóstico:** `vita-parse-core` sobre `logs/psp2core-1783649923-...psp2dmp` da `PC` dentro del propio
`libgameDSO.so`, resuelto a `GFA_DrawFont + 0x60`. Leyendo el pseudo-C de esa función en
`decompiled_so/out_ghidra.c`: llama al JNI `GFA_DrawFont()[F`, y sin chequear si el resultado es `NULL`,
hace `puVar3 = GetFloatArrayElements(env, resultado, 0); __extendsfdf2(*puVar3);` — dereferencia directa.
A diferencia de `GFA_CreateFont` (donde `-1` era una salida "segura" que el motor sabía interpretar),
**esta familia de funciones nunca chequea el resultado antes de leerlo** — no hay ningún valor "seguro"
posible salvo devolver un array real y válido.

**Solución (`loader/java.c`):** se implementaron `GFA_DrawFont`, `GFA_DrawText`, `GFA_MeasureText`
devolviendo un `float[]` real (vía `jda_alloc(n, FIELD_TYPE_FLOAT)` de FalsoJNI — el mismo mecanismo que
usa `NewFloatArray` internamente) con una aproximación monoespaciada de ancho/alto (basada en
`GFA_SetTextSize` y el largo del string activo, sin medición de fuente real todavía). Se aprovechó para
implementar también `GFA_GetPixels32`/`GFA_GetPixels16` (llamados inmediatamente después en el mismo
flujo real, per `CGxFACharCache::drawCharToCharCacheBuffer` en el pseudo-C) — estos SÍ eran seguros por
construcción incluso sin registrar (el wrapper nativo usa `GetArrayLength`+`GetIntArrayRegion`, que
copian en vez de desreferenciar un puntero crudo), pero se registraron igual con un buffer real
(dimensionado con `width`/`height` de `GFA_Init`, cacheado y reusado entre llamadas para no perder un
buffer completo por cada glifo dibujado — el motor nunca llama `DeleteLocalRef` de verdad sobre estos,
confirmado en el log: `"DeleteLocalRef(...): ignored"`).

**Texto en pantalla:** con estos stubs, el texto se va a ver **en blanco/vacío** (los píxeles devueltos
son todo ceros — sin rasterizado real de glifos) pero el motor ya no debería crashear por esto. Pendiente
una futura **Fase de Texto Real** (renderizador de fuente bitmap propio) para que el texto sea visible.

**Patrón general reforzado:** no todo método `Object` no registrado es "seguro" por default (`NULL`)
como en la lección de Zenonia2 §9.10 — depende de si el código nativo que lo consume chequea el
resultado antes de usarlo. Confirmar SIEMPRE leyendo el wrapper nativo en `out_ghidra.c` antes de asumir
que alcanza con dejarlo sin registrar.

## Fase 3.6: bug #6 encontrado y corregido — validación de operador/SIM bloqueaba TODA la construcción de subsistemas (2026-07-09)

Este fue el bug más profundo hasta ahora — 4 saltos de llamada entre la causa real y el síntoma. Con los
5 fixes anteriores, el nuevo log (`logs/latest_log_1783651205.txt`, 5572 líneas) muestra el motor
completando por primera vez toda la secuencia de dibujo de fuente y avanzando hasta
`NativeInitDeviceInfo`/`NativeResize`/`NativeResumeClet` — con logs reales de `glViewport`/`glOrthof`
confirmando que el motor llegó a configurar su proyección. Cortó justo después de
`"Llamando NativeResumeClet..."`. Antes de eso, el log muestra **cientos de repeticiones idénticas** de
`GFA_GetWordwrapPositionEx` + `GFA_SetString("My Mobile. ")` — una pista de que algo relacionado a esa
cadena de texto se estaba reprocesando en loop (benigno esta vez, terminó solo, pero es la misma cadena
que aparece en el mensaje de error que se termina de explicar abajo).

**Diagnóstico con `vita-parse-core`:** Data abort dentro del propio `.so`,
`CGsInputKey::SetReleaseKey(bool) + 0x10`, con `this` (el objeto) en `NULL`. `LR` resuelve a
`CMvApp::EvAppResume() + 0x1f` — coincide exactamente con la última línea del log
(`NativeResumeClet` dispara ese evento).

**Cadena completa rastreada (4 saltos, todos confirmados leyendo el desensamblado/pseudo-C real, no
supuestos):**
1. `EvAppResume()` lee un puntero global (`CGsSingleton<CGsInputKey>::ms_pSingleton`, confirmado por
   nombre de símbolo en `objdump -T` — la dirección del registro `R3` en el crash, `0x98148234`, coincide
   exactamente con `text_base + esa dirección`) y lo usa sin chequear `NULL`.
2. Ese singleton se construye dentro de `CMvApp::EvAppStart()` — una función enorme que además construye
   `CGsUIMgr`, `CGsSound`, `CMvSoundMgr`, `CMvScreenEffMgr`, `CMvResourceMgr`, `CMvXlsMgr`, `CMvStrMgr`,
   `CGsTouchMgr`, `CGsOemIME`, `CMvGraphics`, `CGsParticleMgrEx`, etc. — **pero todo eso está adentro de un
   `if (CGsPhoneInfoV2::InitPhoneInfo(...) != 0) { ... }` — si `InitPhoneInfo` devuelve `0`, la función
   entera se salta sin construir NADA de esto, sin crashear ahí mismo.**
3. `CGsPhoneInfoV2::InitPhoneInfo()` llama a `CGsPhoneInfoV2::CheckPhoneNumber()` — una validación de
   operador/SIM (típica de juegos móviles coreanos con DRM de carrier de la época). Si falla, dibuja un
   mensaje de error en pantalla (el string coreano "USIM카드가 정상적으로..." que ya habíamos visto en un
   log anterior, más `"My Mobile. %s"` — la MISMA cadena que se repetía en loop en este log) y devuelve
   `0`.
4. `CheckPhoneNumber()` llama a `MC_knlGetSystemProperty("PHONENUMBER"/"SIMSERIAL"/"MACADD"/"DEVICEID", ...)`
   → `getSystemProperty()` → los JNI `getPhoneNumber`/`getSimSerialNumber`/`getMacAddress`/`getDeviceID`
   (**las mismas 4 llamadas "not found" que aparecen en TODOS los logs desde el primero** — nunca las
   habíamos tocado porque el motor las consume con un chequeo de `NULL` seguro antes de copiar, así que
   nunca crashearon directamente). Sin registrar, los 4 buffers de teléfono/SIM/MAC/deviceID quedan
   vacíos. La validación real: `PHONENUMBER` debe empezar con `"01"` + un dígito (prefijo de celular
   coreano), y si no, cae a un fallback que exige `strlen(SIMSERIAL) >= 2` o `strlen(DEVICEID) > 1` —
   con los 4 vacíos, **todas** las condiciones fallan.

**Solución (`loader/java.c`):** se registraron los 4 métodos devolviendo `byte[]` con valores
plausibles vía `jda_alloc(len, FIELD_TYPE_BYTE)` (mismo mecanismo que las funciones `GFA_*` de la Fase
3.5) — `getPhoneNumber` devuelve `"01012345678"` (satisface la validación principal de una), los otros 3
devuelven valores placeholder no vacíos por si algún otro camino del motor los usa.

**Patrón general reforzado:** un método JNI "no encontrado" puede ser inofensivo en el punto exacto donde
se llama (chequeo de `NULL` correcto) y **aun así** ser la causa raíz de un crash mucho más adelante,
si su valor de retorno alimenta una decisión de negocio (aquí: "¿inicializo el motor entero o no?") que
en el flujo real de Android nunca falla. La lección de la skill `so-crash-triage` aplica en cadena: cada
salto de esta investigación (`EvAppResume` → `EvAppStart` → `InitPhoneInfo` → `CheckPhoneNumber` →
`getSystemProperty` → los 4 JNI) se resolvió leyendo el pseudo-C real de cada función intermedia, nunca
adivinando.

### Próximo Paso Real (2026-07-09, actualizado tras bug #6)
1. Instalar el VPK recompilado con los 6 fixes acumulados (`build/zenonia_3.vpk`). Este es probablemente
   el intento más prometedor hasta ahora — bajar el log nuevo:
   - Si `CMvApp::EvAppStart()` corre completo (buscar en el log actividad nueva de `CGsUIMgr`/`CGsSound`/
     `CMvResourceMgr`/etc., o simplemente que `NativeResumeClet` ya no sea la última línea) y el loop
     principal arranca (`"frame N alive..."` repitiéndose) y/o se ve el menú, el port arrancó de verdad —
     pasar a Fase 4/5 (gráficos y UI).
   - Si crashea en otro punto, seguir la misma metodología (`vita-parse-core` → `objdump -M force-thumb`
     → cruzar con `out_ghidra.c`/`zenonia3_java/`, ver la skill `so-crash-triage`) — no asumir que es la
     misma familia de bug que las anteriores.
2. La secuencia de `GFA_GetWordwrapPositionEx`/`GFA_SetString("My Mobile. ")` repetida cientos de veces
   sugiere que el motor reintenta dibujar el mensaje de error de `DrawMassage()` en un loop hasta que algo
   lo corta — con el fix de esta fase esa pantalla de error ya no debería aparecer nunca, así que ese loop
   no debería volver a verse. Si reaparece con otro texto, investigar qué otra validación lo está
   disparando.
3. Confirmar si `NativeInitDeviceInfo(960,544)` deja la UI del motor usable o si hace falta una
   resolución de teléfono real (ver hallazgo de Fase 1 sobre resolución interna configurable).

## Fase 3.7: bug #7 encontrado y corregido — `readAssets` no sirve a los dos consumidores JNI distintos que tiene el motor (2026-07-09)

El fix de la Fase 3.6 funcionó de verdad: el log nuevo (`logs/latest_log_1783652896.txt`) muestra al
motor cargando **assets reales por primera vez** (`com/light80x50.zt1`, `TouchOemIME.pzx`, ambos con
`isAssetExist`/`readAssete` exitosos) y `GFA_Init` corriendo con parámetros reales del motor
(`w=24 h=24 bpp=32`, no los valores fijos que nosotros mandábamos antes) — confirma que
`CMvApp::EvAppStart()` esta vez sí construyó subsistemas reales antes de llegar a este punto.

**Síntoma nuevo:** después de cada `readAssets` exitoso, el log muestra
`GetArrayLength(env, <ptr>)` → `"Array ... not found. Unknown array type?"` → `GetByteArrayElements(...): Could not find the array`.
El log corta durante el procesamiento de `TouchOemIME.pzx`.

**Diagnóstico con `vita-parse-core`:** Data abort dentro del propio `.so`,
`CGxZeroPZDParser::DecodeHeader(bool) + 0x12`, llamado desde `CGxPZDParser::DecodeHeader(bool) + 0x2b`
— la familia de parsers "PZx"/"PZD" (usada para `.pzx` como `TouchOemIME.pzx`). Leyendo el desensamblado
de ambas funciones: la clase derivada llama primero a la base (`CGxPZDParser::DecodeHeader`), que internamente
llama a `CGxPZxParserBase::CheckPZxType(...)` sobre un stream construido a partir del resultado de
`readAssete` — y solo si `CheckPZxType` tiene éxito, la base alloca y setea `this+20` (un puntero interno
del parser). La clase derivada, después de llamar a la base, **asume que `this+20` ya está seteado** y lo
desreferencia sin chequear `NULL` — pero como la base falló silenciosamente (sin indicarlo con un código
de error que la derivada chequee), `this+20` queda en `NULL` y crashea.

**Causa raíz:** `CheckPZxType` necesita leer los primeros bytes del archivo `.pzx` a través de un
`CGxStream`, que internamente usa las funciones JNI estándar (`GetArrayLength`/`GetByteArrayElements`)
sobre el resultado de `readAssete` — **a diferencia de `MC_knlGetResource`/`CMvResourceMgr`**, que leen
ese mismo resultado por **puntero crudo** (el truco de "ArrayObject de Dalvik" de 16 bytes, confirmado
necesario desde Zenonia 2). O sea: **el mismo método JNI (`readAssets`/`readAssete`) tiene dos
consumidores distintos dentro del motor, que esperan dos representaciones de array diferentes** — y
nuestro `Zenonia_readAssets` solo servía a uno de los dos (el puntero crudo). Cuando `GetByteArrayElements`
no encuentra el array (porque no es un `JavaDynArray` real de FalsoJNI), devuelve `NULL`/longitud 0 sin
crashear ahí mismo — el crash real ocurre más adelante, cuando el parser asume que el stream se leyó bien.

**Solución (`loader/java.c` + `loader/main.c`):** en vez de elegir una sola representación, se
interceptaron las 4 funciones de array de la tabla JNI (`GetArrayLength`, `GetByteArrayElements`,
`GetByteArrayRegion`, `ReleaseByteArrayElements` — mutables en memoria pese al tipo `const` público:
`jni_init()` las aloca con `malloc()`, confirmado leyendo `FalsoJNI.c`) para que reconozcan nuestros
propios bloques (un registro simple de punteros devueltos por `Zenonia_readAssets`, hasta 1024) y los
sirvan directamente desde el header Dalvik, cayendo al código real de FalsoJNI para cualquier otro array
genuino (`JavaDynArray*` de `jda_alloc`, usado por ejemplo en las funciones `GFA_*` de la Fase 3.5). Se
instalan con `zenonia_install_array_hooks()`, llamado en `main.c` justo después de `jni_init()`.

**Detalle técnico:** en este proyecto `JNIEnv` es literalmente `const struct JNINativeInterface*` (no
una struct con un campo `.functions` — eso es solo la variante C++ de `jni.h`, y este código compila como
C puro), así que la variable global `jni` **es** la tabla de funciones; para parchearla alcanza con
`(struct JNINativeInterface *)(uintptr_t) jni` y escribir los punteros de función directamente.

**Patrón general reforzado:** cuando un mismo método JNI se usa desde múltiples subsistemas del motor,
no asumir que todos lo consumen de la misma manera — un motor viejo puede tener rutas de código que
predatan una convención más nueva (aquí: acceso directo por puntero heredado de un NDK viejo, coexistiendo
con acceso JNI estándar en un subsistema agregado después, como el parser "PZx"). Confirmarlo leyendo el
pseudo-C de cada consumidor real antes de asumir que un solo formato de retorno alcanza para todos.

## Fase 4: bug #8 (regresión) encontrado y corregido — `glOrthof` forzado rompía el mapeo del framebuffer + lentitud por log JNI (2026-07-10)

**El juego ya llega al menú.** Los dos logs comparados (`log_1783656530.txt` — logo de Gamevil visible —
y `log_1783657108.txt` — solo un cuadro blanco chico sobre negro) terminan ambos midiendo los strings del
menú coreano ("이어하기"/"새로하기" = Continuar/Nueva partida) con el loop principal vivo: **la lógica del
juego es idéntica en ambas corridas; lo que se rompió entre un build y otro fue solo la presentación.**

**Causa raíz de la regresión visual:** entre los dos builds se cambió `glOrthof_wrapper` (`loader/dynlib.c`)
para forzar `glOrthof(0, 800, 480, 0)` "para que un quad 800x480 llene el viewport". Pero el motor manda
una proyección **centrada** (`glOrthof(-400..400, -240..240)`, visible en ambos logs) y el quad de pantalla
completa usa vértices GL_FIXED en ese MISMO sistema (`(-400<<16, -240<<16)..(400<<16, 240<<16)`, confirmado
en el log de `glVertexPointer`). Con el origen movido a la esquina, ese quad mapea a NDC x∈[-2,0], y∈[0,2]:
solo un cuadrante queda en pantalla (el "cuadro blanco chico") y además con Y invertida. Se restauró el
pass-through (que es exactamente lo que corría cuando el logo se veía bien).

**Causa raíz de la lentitud:** dos costos por frame acumulados, ninguno nuevo pero ya intolerables al
llegar al menú (que hace layout de texto GFA en loop continuo):
1. `FALSOJNI_DEBUGLEVEL=0`: el 79% del log (3579 de 4520 líneas) eran trazas `[JNI][...]` — y `game_log()`
   hace `fprintf`+`fflush` a `ux0:` por CADA línea. En el menú, cada frame genera decenas de llamadas GFA →
   decenas de escrituras sincrónicas a memory card por frame. Se bajó a nivel 2 (WARN) vía la opción
   `ENABLE_VERBOSE_JNI_LOG` de CMake (ahora default OFF; `[JNI WARN]`/`[JNI ERR]` siguen visibles, que son
   los que diagnostican bugs reales).
2. `convert_rgb565_to_rgba8888` hacía `malloc`+`free` de 1.5 MB **más** un escaneo min/max completo en cada
   upload — y el motor sube el framebuffer 800×480 entero antes de cada quad (~10 veces por frame en el
   log). Ahora usa un buffer estático reutilizable y el escaneo min/max solo corre mientras quedan logs de
   diagnóstico por emitir (10 primeras llamadas).

**Patrón para el futuro:** antes de "corregir" la proyección o el viewport, verificar en el log en qué
sistema de coordenadas vienen los VÉRTICES que el motor realmente dibuja — proyección y geometría son un
par: si la geometría ya está centrada, forzar una proyección con origen en la esquina no la "arregla", la
desplaza. Y cualquier cambio de render debe compararse contra el último log donde la imagen se veía bien
(aquí el formato del log de `glOrthof` delató en qué build se introdujo la regresión).

**Deuda conocida (no bloqueante para navegar el menú):** el texto sigue invisible (los stubs GFA devuelven
píxeles en cero — ver Fase 3.5, pendiente la Fase de Texto Real con rasterizador de fuente propio), y el
menú re-lee `menu/Title.pzx` (288 KB) desde disco en cada evento de input (comportamiento del motor;
si resulta molesto en consola, cachear en `Zenonia_readAssets`).

## Fase 4.1: bug #9 encontrado y corregido — resolución interna equivocada (el juego dibuja SIEMPRE a 400×240) + fixes de jda (2026-07-10)

El log `log_1783658068.txt` (con el fix de `glOrthof` del bug #8) confirmó: logo de Gamevil visible,
menú alcanzado (New Game + 3 slots visibles) — pero todo el arte "en pequeño" en una porción de la
pantalla, con el resto blanco/negro, y la carga seguía lenta (el nivel de log JNI seguía en 0: la
**caché de CMake** en `/tmp/zenonia3-build` conservaba `ENABLE_VERBOSE_JNI_LOG=ON` de un build anterior —
`option()` cachea; el rebuild de esta fase se hizo con la caché borrada y quedó confirmado
`FALSOJNI_DEBUGLEVEL=2` en `flags.make`).

**Causa raíz del arte "en pequeño" — el juego dibuja hardcodeado a 400×240, la Fase 1 sobreestimó la
"resolución configurable":** triple confirmación:
1. `CMvTitleState::DrawZeroGrade()` (out_ghidra.c:105397) hace `DrawFillRect(0,0,400,0xf0,blanco)` y
   centra el logo con `(400-w)>>1` / `(240-h)>>1`; `DrawTeamLogo` dibuja en `(200,120)` = centro de
   400×240. El "cuadro blanco chico sobre pantalla blanca" que se veía en consola ERA ese FillRect.
2. El Java original pasa `gameScreenWidth/Height = 400/240` a `NativeInitDeviceInfo` **y** a
   `NativeInitWithBufferSize` (`NexusGLActivity.java:85-86`; jadx renombra la constante 400 como
   `UI_STATUS_PURCHASE_PAGE`). `NativeResize` en cambio recibe el tamaño real de la GLSurfaceView.
3. `getDeviceInfo()` (out_ghidra.c:148015) defaultea `di[3]=400, di[4]=0xf0` — 400×240 es el default de
   fábrica del motor.

Lo que la Fase 1 leyó como "resolución interna configurable" solo configura el tamaño del FRAMEBUFFER
que se sube por textura — la lógica de dibujo del juego no lo consulta. A 800×480 el juego pintaba solo
el cuadrante superior-izquierdo. **El escalado a pantalla completa lo hace el motor:** `glResize(w,h)`
(llamado por `NativeResize`) arma viewport, `glOrthof(±w/2,±h/2)` y el quad con vértices ±w/2 — así que
lo correcto es `NativeInit*(400,240)` + `NativeResize(960,544)` (main.c, nuevas constantes
`SCREEN_W/SCREEN_H`). El touch pasó a mapearse al espacio de pantalla (960×544) porque en Android
`NexusTouch.java` manda las coordenadas crudas del `MotionEvent` (espacio de la view) y el motor
convierte solo. Bonus: la conversión RGB565→RGBA por frame baja de 384k a 96k píxeles (4×).

**Bug #9b — punteros jda colgados + leak por frame (los `[JNI WARN] Array 0x81349ef0 not found` del
log):** `jda_alloc()` de FalsoJNI devuelve punteros DENTRO de la tabla global `javaDynArrays[]`, y
`jda_extend()` la **realoca** al llenarse (capacidad inicial: 16) — invalidando los `JavaDynArray*`
cacheados (`g_gfa_pixels32_jda`/`16`). Además `GFA_DrawFont`/`DrawText`/`MeasureText` alocaban un jda
nuevo POR LLAMADA (decenas por frame en el menú) que el motor "libera" con `DeleteLocalRef` — ignorado
por FalsoJNI → leak + reallocs continuos. Fixes: (a) esas 3 funciones ahora reutilizan un jda
persistente cada una (el motor consume el `float[]` dentro de la misma llamada nativa —
`GetFloatArrayElements`+`Release` inmediatos, confirmado en el log); (b) capacidad inicial de la tabla
subida a 1024 en el FalsoJNI vendorizado (comentario `[Zenonia3]` en `jda_tryinit`). También se registró
`getLocaleID` (devuelve 2 = default no-coreano; el -1 de "not found" caía en el mismo default, solo
saca el `[JNI ERR]` del log).

**Lección de build:** `option()` de CMake cachea su valor — cambiar el default en `CMakeLists.txt` NO
afecta a un build dir existente. Tras cambiar una `option()`, borrar el build dir (o pasar
`-DENABLE_VERBOSE_JNI_LOG=OFF` explícito) y verificar el flag real en
`CMakeFiles/<target>.dir/flags.make`.

### Próximo Paso Real (2026-07-10, actualizado tras bug #9)
1. Instalar `build/zenonia_3.vpk`. Verificar en consola:
   - Logo/título/menú a **pantalla completa** (el motor escala 400×240 → 960×544 solo).
   - Carga notablemente más rápida (log JNI en WARN + sin re-conversión de 800×480).
   - Sin `[JNI WARN] Array ... not found` en el log nuevo.
   - Texto del menú: sigue invisible (stubs GFA sin rasterizado — próxima gran fase).
2. Si el touch aparece desplazado: probar mapeo a GAME_W/GAME_H (400×240) en vez de SCREEN_W/H — ver
   comentario en el bloque de touch de `main.c`.
3. Si el arte del menú sigue incompleto A PANTALLA COMPLETA (ya no "en pequeño"), investigar el patrón
   de puntero-con-signo estilo Zenonia 2 §11.1 en los caminos de creación de imágenes (el parche actual
   solo cubre `CMvLayerData::PreLoad`).

### Próximo Paso Real (2026-07-10, actualizado tras bug #8 — SUPERSEDIDO por el de arriba)
1. Instalar el VPK recompilado (`build/zenonia_3.vpk`) y verificar en consola:
   - El logo de Gamevil debería volver a verse, y el menú (fondo/arte de `menu/Title.pzx`) debería ocupar
     la pantalla completa — sin el cuadro chico en la esquina.
   - El arranque debería ser notablemente más rápido (sin las miles de escrituras de log JNI). El log
     nuevo va a ser mucho más corto: eso es esperado, no una falla del logging.
   - Los textos del menú van a seguir invisibles (stubs GFA sin rasterizado) — navegar "a ciegas" con el
     pad para confirmar que Continuar/Nueva partida responden. Si el menú responde, la siguiente prioridad
     es la Fase de Texto Real.
2. Si aparece un array `.pzx`/`.zt1` corrupto o con longitud sospechosa proveniente del registro Dalvik
   (por ejemplo `GetByteArrayRegion` pidiendo más bytes de los que el archivo realmente tiene), revisar
   `zenonia_register_dalvik_array`/`zenonia_is_dalvik_array` antes de sospechar de los datos del asset —
   es una estructura nueva, sin confirmar todavía en consola real con múltiples archivos simultáneos.

## Fase 5: ¡Jugable! — y las 4 piezas que faltaban: texto real, audio, touch y stat de bionic (2026-07-10)

**Hito confirmado por el usuario con el build de la Fase 4.1:** pantalla completa, logo de Gamevil
correcto, selección de personaje, cinemática y **gameplay estable con botones** (Triángulo=Skip,
X=atacar; los botones táctiles in-game se VEN — los dibuja el motor desde `Dpad.pzx`). Pendientes
reportados: textos invisibles, sin audio, táctil muerto, y algunos defectos gráficos menores en
transiciones. Esta fase implementa las tres primeras + un fix preventivo.

### 5.1 — Texto real: rasterizador GFA con stb_truetype (`loader/font.c` + reescritura GFA en `java.c`)

Zenonia 2 no necesitó esto (su motor no usa el puente de fuentes Java); en Zenonia 3 TODO el texto pasa
por `NexusFont.java` vía JNI. Implementación fiel función por función (NexusFont.java es la spec):

- **Backend** (`loader/font.c`): stb_truetype (vendorizado de Prince of Persia) sobre **`app0:font.ttf`**
  (NanumGothic Regular, licencia OFL, 2 MB — cobertura Hangul completa + Latin; el juego setea strings
  coreanos incluso con `getLocaleID=2`). Empaquetada en el VPK (`FILE font.ttf` en CMakeLists).
  `Paint.setTextSize` ≈ `stbtt_ScaleForMappingEmToPixels` (tamaño EM, NO ScaleForPixelHeight).
- **Formato de píxeles confirmado en el desensamblado** (la parte no obvia):
  `CGxFACharCache::CopyPixelsToCharCacheBuffer` (out_ghidra.c:153389) consume **solo el canal alfa**
  (`byte >> 24`) de los píxeles de `GFA_GetPixels32`, leyendo `ceil(rect[2]) × ceil(rect[3])` píxeles
  con stride = ancho del bitmap de `GFA_Init` (24×24 en este juego). El color del texto lo aplica el
  motor después — el cache de glifos es un atlas de cobertura.
- **Flujo por glifo**: `drawCharToCharCacheBuffer` → `GFA_SetString`/`SetStringFromKSC5601`/
  `SetStringFromUnicode` (según el encoding de la fuente) → `GFA_DrawFont()` (devuelve
  `{0,0,anchoMedido,charH+1}`, baseline en `charH - descent + 1`) → `GFA_GetPixels32`.
- **Encodings**: jstring llega como char* UTF-8 crudo de FalsoJNI; `SetStringFromUnicode` es UTF-16LE;
  `SetStringFromKSC5601` es EUC-KR — convertido con una tabla cp949→UCS2 generada con Python
  (`loader/ksc5601_table.h`, 17048 secuencias válidas, ~48 KB).
- **Métricas reales**: `GFA_GetAscent = -ceil(ascent())` (POSITIVO), `GFA_CharWidth` = avance del
  carácter Hangul '뷁' (U+BDC1 — el ancho de celda coreano), `GFA_GetWordwrapPositionEx` replica el loop
  exacto de breakText de Java, `GFA_DrawText`/`MeasureText` con el mismo algoritmo multilinea de
  word-wrap (BreakIterator aproximado: espacios + cada carácter CJK/Hangul corta).

### 5.2 — Audio (`loader/audio.c/h`, adaptado del de Zenonia 2 §12 con 2 diferencias reales)

1. **Los .ogg de Z3 NO son todos de una frecuencia** (60×44100 Hz + 17×16000 Hz, todos mono): el
   mezclador corre a 44100 y resamplea al vuelo las voces de 16 kHz (zero-order hold con paso
   fraccional — suficiente para SFX 2D). Cada voz tiene su buffer de staging propio (uno compartido
   descartaba frames al alternar voces).
2. **El despacho es el de `ZenoniaUIControllerView.OnSoundPlay`**, distinto del SoundMgr de Z2:
   `vol==0 && isLoop` = comando "parar BGM" (¡no un play!); IDs 1..15 = SFX (SoundPool, se apilan en 4
   voces); el resto = BGM (`isLoop`) o stream one-shot. Ganancia = `(vol/10)/10` (NexusSound.setVolume).
   La firma real es `OnSoundPlay(sndID, vol, isLoop)` — los logs viejos etiquetaban `vol` como "loop".

**Mapeo ID→archivo (trampa de recursos Android):** el motor pide `R.raw.s000 + sndID`, y los resource
IDs son consecutivos **en orden alfabético de los archivos existentes** — la numeración original tiene
huecos (falta s010, s019, …), así que `sndID` NO es el número del nombre de archivo. Los .ogg se copian
renombrados por índice ordinal a `ux0_data/zenonia3/sound/sNNN.ogg` (verificado contra el registro SFX
de `Zenonia3Launcher`: índice 10 = s011.ogg original ✓). **Subir esa carpeta por FTP a
`ux0:data/zenonia3/sound/`.** `OnStopSound` pasó de no-op a `audio_stop_all()`.

### 5.3 — Táctil: coordenadas de JUEGO, no de pantalla (+ evento MOVE)

`UIFullTouch.java` (la ruta táctil in-game real de Z3) convierte a espacio de juego —
`x*gameScreenWidth/viewWidth` — ANTES de `setTouchEvent(23/25/24, x, y, pointerId)`. Nuestro loader
mandaba coordenadas 960×544 a un juego que espera 400×240: todos los taps caían fuera del área de
cualquier botón (por eso "se ven los botones pero no responden"). Ahora: panel 1920×1088 → 400×240,
y se agregó el evento 25 (MOVE) mientras el dedo se arrastra (Android lo manda en cada ACTION_MOVE).

### 5.4 — Fix preventivo: `struct stat` de bionic (portado de Zenonia 2 §12.4)

El `stat_hook` de Z3 seguía llenando el `struct stat` de newlib; el motor (compilado contra bionic) lee
`st_mode` en offset 16 y `st_size` en 48. En Z2 esto crasheaba al CARGAR PARTIDA GUARDADA con
`MALLOC FAILED FOR SIZE <puntero>`. Portado el `bionic_stat_t` con traducción campo a campo (con
`_Static_assert` de offsets) antes de que el síntoma aparezca aquí — el usuario ya puede jugar, así que
guardar/cargar es lo próximo que va a ejercitar.

### Instalación (build de esta fase)
1. Instalar `build/zenonia_3.vpk` (ahora pesa ~1.6 MB: incluye `font.ttf`).
2. Subir `ux0_data/zenonia3/sound/` → `ux0:data/zenonia3/sound/` por FTP (77 archivos, ~14 MB).
3. Verificar: música del título + SFX al navegar; textos visibles en menú/diálogos (coreano e inglés);
   táctil respondiendo en los botones in-game; guardar y cargar partida sin crash.
4. Si el texto se ve pero desalineado/cortado: revisar la elección de
   `stbtt_ScaleForMappingEmToPixels` vs `ScaleForPixelHeight` en `font.c` (el tamaño relativo
   Paint↔stb es la única aproximación no confirmada contra hardware todavía).
5. Si un SFX suena "equivocado": revisar el mapeo por índice de `ux0_data/zenonia3/sound/` (Fase 5.2)
   antes de tocar audio.c — la hipótesis del índice ordinal está verificada solo contra los 15 SFX.

## Fase 6: Ocultar la cruceta/botones táctiles nativos (2026-07-10)

**Pedido:** con los botones físicos de la Vita ya mapeados 1:1 (`btn_map` en `main.c`), la cruceta y el
botón de acción que el motor dibuja sobre la pantalla (visibles desde la Fase 5, "los botones táctiles
in-game se VEN — los dibuja el motor desde `Dpad.pzx`") son redundantes y tapan parte de la pantalla de
juego. Se pidió ocultarlos sin tocar el resto de la interfaz (barra de vida/maná, minimapa, iconos de
skill, etc.).

**Hallazgo (`decompiled_so/out_ghidra.c`):** `GVUIPlayerController::GVUIPlayerController()` (línea
~28609) hace `GVUIController::SetResource(this, "Dpad.pzx")` — la cruceta Y sus hasta 5 botones (el de
acción incluido) son **frames distintos de un único atlas compartido**, armados en
`InitialPlayerPadSet()` (línea ~28464). No son archivos separados: no se puede ocultar el botón de
acción sin la cruceta por nombre de archivo, pero tampoco hace falta — ambos forman un solo cluster
visual ("el gamepad virtual"), y el resto del HUD vive en recursos totalmente distintos (`com/Keypad.pzx`,
`dynamic/SkillIcon*.pzx`, etc.), así que ocultar el atlas completo de `Dpad.pzx`/`illusiaDpad.pzx`
(variante de cruceta, probablemente modo compañero/mascota — mismo mecanismo, mismo substring "dpad")
no afecta a nada más.

**Solución — hide a nivel GL, no a nivel asset:**
- `loader/java.c`: `zenonia_name_has_dpad()` detecta el substring "dpad" (case-insensitive) en cualquier
  nombre pasado a `Zenonia_readAssets`. Al matchear, marca `g_zenonia_hide_dpad_pending = 2` (ventana de
  hasta 2 texturas, por si el atlas se sube en más de una llamada a `glTexImage2D`).
- `loader/dynlib.c`: `glBindTexture_wrapper` trackea la textura actualmente bindeada;
  `glTexImage2D_wrapper` etiqueta esa textura como "oculta" mientras la ventana de arriba siga abierta;
  `glEnable_wrapper`/`glDisable_wrapper` trackean si `GL_TEXTURE_2D` está activo; `glDrawArrays_wrapper`
  saltea el draw real (sin tocar `pending_fixed_*` ni ningún otro estado GL) si la textura bindeada está
  en la lista de ocultas. El touch de esa zona **sigue registrado** (hitbox invisible, no se tocó
  `main.c`) — solo se oculta el dibujo, consistente con seguir usando los botones físicos.
- No se tocó ningún asset ni el binario del juego — todo el mecanismo es reversible sin recompilar el
  `.so` (que ni siquiera se modifica).

**Cómo reactivar la cruceta/botón en pantalla:** el toggle es la opción de CMake
`HIDE_VIRTUAL_GAMEPAD` (default `ON`, ver `CMakeLists.txt`). Recompilar con:
```bash
cmake -DHIDE_VIRTUAL_GAMEPAD=OFF .
make
```
o cambiar el default a `OFF` en `CMakeLists.txt` antes de compilar. Con la opción en `OFF`,
`ZENONIA_HIDE_DPAD_UI` no se define y todo el código de arriba queda inactivo (los wrappers involucrados
vuelven a comportarse exactamente igual que antes de esta fase).

**Verificado:** compila limpio (`build.sh`, sin warnings nuevos en `dynlib.c`/`java.c` más allá de uno
preexistente en `translate_path`). Pendiente de confirmar en consola real que la cruceta/botón
efectivamente desaparecen y que el resto del HUD (vida/maná, minimapa, skills) sigue visible.

## Pendiente (2026-07-10): pantallas de menú/título poco visibles

Reporte del usuario: no se ve bien la pantalla del **menú** (New Game/Continuar) ni la **pantalla previa**
(título), lo suficiente como para poder tocarlas. El log más reciente sin documentar
(`logs/log_1783659461.txt`) muestra la secuencia completa `OnUIStatusChange: 1` (TITLE) →
`OnUIStatusChange: 2` (MAINMENU, carga `menu/Title.pzx`/`menu/ZeroGrade.pzx` con éxito) →
`OnUIStatusChange: 14` (FULLTOUCH/in-game) navegada **solo con el pad físico** (`touch.reportNum=0`
durante todo el tramo 1/2 — el usuario nunca intentó tocar ahí, consistente con no poder ver dónde tocar).
Los uploads de textura logueados durante esa carga muestran datos no triviales (`min=0x20c2 max=0xffff`,
no todo blanco/negro), así que el framebuffer de software SÍ recibe contenido real — no se pudo confirmar
solo con el log de texto si el problema es una pantalla en blanco/negro sólido, colores corridos, o el
fondo correcto pero sin botones/texto encima. `splash_draw()` (main.c) es un no-op confirmado (no existe
`app0:splash.rgba` en el repo ni se empaqueta en el VPK), así que no es la causa.

**Siguiente paso real:** falta describir qué se ve exactamente en esas dos pantallas (negro sólido,
blanco sólido, colores incorrectos, o fondo bien pero sin elementos interactivos) y, si es posible, un
log fresco de esa corrida — sin eso, cualquier cambio de render sería adivinar contra la propia regla del
proyecto ("un log a la vez, no adivinar", ver Fase 8 del plan / notas de metodología repetidas en este
archivo).

**Actualización — confirmado por el usuario:** logo en **blanco sólido**, título/menú en **negro sólido**
(sin crash). Rastreado en el pseudo-C (`decompiled_so/out_ghidra.c:105397`/`105434`):
`CMvTitleState::DrawZeroGrade()`/`DrawTeamLogo()` hacen `CGsGraphics::DrawFillRect(0,0,400,0xf0,BLANCO)`
(eso es lo que se ve) y **luego** llaman a una función virtual sobre un objeto obtenido de
`CMvResourceMgr` que debería dibujar el logo/arte encima, con un **fade de alpha por contador** (`< 0x10`
frames → alpha parcial, después → opaco). El log ya confirma que el quad de pantalla completa SÍ recibe
contenido real (textura RGB565 512×512, UVs `(0.00,0.47)-(0.78,0.47)` = exactamente `240/512`×`400/512`,
o sea la sub-región correcta del atlas) y en los primeros frames logueados tiene un
`glColorPointer` con alpha≈50% (`(88,18,16,129)`) — consistente con el fade recién arrancando, pero **no
se pudo confirmar si ese fade realmente termina en opaco** (los logs de color/blend estaban topados a los
primeros 10-20 llamados, que ya pasaron para cuando se llega al menú).

**Instrumentación agregada (sin tope mientras `ui_status` esté en 0-2, ver `zenonia_verbose_ui()` en
`loader/dynlib.c`):** `glColorPointer_wrapper` (alpha real de cada draw), `glBlendFunc_wrapper` (nuevo,
antes pasaba directo sin loguear), `glTexEnvf_wrapper` (ya logueaba, se le agregó `ui_status`). Build
compilado y verificado sin errores (`build/zenonia_3.vpk`).

**Pedido al usuario:** instalar este VPK, reproducir el logo blanco → menú negro una vez, y subir el
`ux0:data/zenonia3/logs/log_<timestamp>.txt` resultante. Con eso se puede confirmar si el alpha llega a
255/opaco (el fade "no está pegado", la causa es otra — revisar `GsLoadPzx`/parser PZx para
`Title.pzx`/`ZeroGrade.pzx` específicamente) o si se queda en un valor parcial para siempre (confirmaría
el fade roto, y el fix sería encontrar qué contador/timer nativo no avanza — candidato: depende de un
delta-time o callback que este loader no esté alimentando igual que Android).

## Fase 6.1: causa real encontrada — el logo/título/menú NUNCA los dibujó el .so, son overlays Android (2026-07-10)

El usuario señaló `zenonia3/res/drawable/` y confirmó que ahí están **todos** los elementos de
logo/título/menú/loading/botones (usar las variantes "globales"/en inglés, no las `_kr`/`_jp`/`_ch`).
Revisando `zenonia3_java/resources/res/layout/main.xml`: el logo de Gamevil (`ui_logo_gamevil`), el fondo
de título (`gb_titleImg` → `ui_title_bg_nate`), el logo chico de título (`ui_title_logo5`), y el fondo +
6 botones del menú (`menuBack0`/`menuBack1`, `ui_menu_newgame`/`continue`/`options`/`help`/`about`/
`community`) son **`ImageView`/`ImageButton` de Android superpuestos al `GLSurfaceView`** — nunca los
dibujó el `.so` nativo. `com/gamevil/nexus2/Natives.java` (`showTitleComponent()`,
`showMenuComponent()`, `showMenuItemComponent()`) los muestra/oculta y posiciona vía JNI callbacks desde
el motor (`OnUIStatusChange` → `changeUIStatus()` → `setUIState()` en `ZenoniaUIControllerView.java`).
**Esto invalida la hipótesis del "fade de alpha pegado" de la sección anterior** — no había ningún fade
nativo fallando: lo que faltaba dibujar nunca fue responsabilidad del `.so` en primer lugar. Este loader,
al no tener ninguna capa Java/Android, nunca reprodujo esos overlays — de ahí el blanco (logo, solo el
`DrawFillRect` del `.so` de fondo) y negro (título/menú, sin ningún fondo del `.so` en esos estados).

**Posiciones NO inventadas:** `Natives.showMenuItemComponent()` calcula el margen de cada botón como
`offset * displayWidth/400` y `topMargin * displayHeight/240` — la MISMA base lógica 400×240
(`GAME_W`/`GAME_H`) que ya usa este loader para todo lo demás. La rama que reescala tamaño
(`displayWidth >= 1024`) no aplica en Vita (960 < 1024), así que los botones quedan a su tamaño nativo de
PNG (82×61) — no es una aproximación, es lo que hacía el código original en ese branch.

**Solución implementada:**
- `tools/build_android_ui_assets.py` (nuevo, requiere Pillow): convierte los 11 PNG "globales" necesarios
  a RGBA8888 crudo (header de 8 bytes width+height + píxeles) en `androidui/*.rgba` — mismo formato que ya
  usaba `splash_load()` para `app0:splash.rgba`, sin necesitar un decoder PNG en el loader. Correr de
  nuevo si se reemplazan esos PNG.
- `loader/androidui.c`/`.h` (nuevo): carga esos 11 `.rgba` una vez (`androidui_load()`, llamado en
  `main.c` junto a `splash_load()`) y los dibuja (`androidui_draw(ui_status, SCREEN_W, SCREEN_H)`, llamado
  en el loop principal) según el estado: `ui_status==-1` (antes del primer `OnUIStatusChange`) → logo
  completo; `==1` (TITLE) → fondo + logo chico centrado abajo; `==2` (MAINMENU) → fondo + franja superior
  + los 6 botones en las posiciones exactas de arriba.
- `CMakeLists.txt`: nueva fuente `loader/androidui.c` + 11 `FILE androidui/*.rgba` empaquetados en el VPK
  (mismo mecanismo que `font.ttf`).
- **Sin confirmar (marcado en el código):** el margen superior de `ui_menu_back1` (franja decorativa) está
  hardcodeado en el XML como `"142px"` (no calculado en Java como los demás) — se aproximó escalando sobre
  la altura nativa del propio PNG (320) en vez de 240. No bloqueante: `ui_menu_back0` ya cubre toda la
  pantalla de fondo.
- Estos overlays son **solo visuales** — no tocan el touch/input (los botones de menú ya se navegan por
  pad físico, ver `btn_map` de `main.c`; el toque directo sobre estos botones dibujados requeriría además
  registrar sus hitboxes, no incluido en esta fase).

**Verificado:** compila limpio (`build.sh`, sin warnings en `androidui.c`), VPK generado (~1.87 MB, antes
~1.6 MB) con los 11 `androidui/*.rgba` confirmados adentro (`unzip -l`). Pendiente de probar en consola
real: ¿se ven logo/título/menú ahora?, ¿posiciones de los botones correctas?, ¿franja de `ui_menu_back1`
bien ubicada?

## Fase 6.2: confirmado en consola (logo/título/menú se ven) + 3 bugs nuevos encontrados y corregidos (2026-07-11)

El usuario confirmó que con el VPK de la Fase 6.1 **el logo/título/menú ya se ven**. Reportó 3 problemas
nuevos, los 3 diagnosticados con evidencia real (no adivinados) y corregidos:

**1. El touch no funciona en el menú.** Esperado — la Fase 6.1 solo agregó el *dibujo* de los overlays,
no su hit-testing. Confirmado en `Natives.java` (`showMenuItemComponent()`): cada `ImageButton` de menú
tiene su propio `OnClickListener` que manda un evento nativo directo, **sin pasar por el sistema de touch
del motor**:
- `community` → `handleCletEvent(2/3, -3, 0, 0)` (tecla `HAL_KEY_LEFT`)
- `options` → `-1` (`HAL_KEY_UP`)
- `newgame` → `-5` (`HAL_KEY_OK`)
- `help` → `-2` (`HAL_KEY_DOWN`)
- `about` → `-4` (`HAL_KEY_RIGHT`)
- `continue` → **un único evento distinto**: `handleCletEvent(NexusHal.REPLY_YESNO, NexusHal.FIRST_MOVE_REPLY_PAGE, 0, 0)`
  (constantes reales de `NexusHal.java`: `REPLY_YESNO=19450815`, `FIRST_MOVE_REPLY_PAGE=20010913`)

  También en `Natives.showTitleComponent()`: el fondo de TITLE (`gb_titleImg`, pantalla completa) tiene un
  `OnTouchListener` que manda `handleCletEvent(2/3, -16, 0, 0)` (`HAL_KEY_BACK`) ante **cualquier** toque —
  o sea, tocar en cualquier parte del título avanza al menú, igual que ya lo hacía el botón físico ✕
  (mapeado a `HAL_KEY_OK`, otra tecla que también dispara el mismo avance).

  **Detalle importante:** en Android, estos `ImageView`/`ImageButton` están POR ENCIMA del
  `GLSurfaceView` en el `FrameLayout` y consumen el toque — el motor nunca ve un evento de puntero crudo
  ahí, solo la tecla sintética. Reproducir esto reenviando el toque crudo ADEMÁS del evento sintético
  habría sido incorrecto (doble input).

  **Solución (`loader/main.c`):** se agregó `androidui_menu_hit_test()` (`loader/androidui.c`, reutiliza
  las mismas 6 posiciones de botón de la Fase 6.1) para saber sobre qué botón cae un toque nuevo en
  `ui_status==2`; en `ui_status==1` cualquier toque nuevo dispara directamente `HAL_KEY_BACK`. En ambos
  casos se **suprime** el reenvío normal de `MH_POINTER_*` para el resto de ese gesto (variable
  `last_touch_suppressed`) — si el toque no cae en ningún botón del menú, se reenvía normal (el fondo del
  menú no tiene listener propio en el original, así que ahí sí debe llegar como puntero crudo al motor).

**2. Triángulo (SKIP) + Círculo (BACK) en el menú → pantalla negra.** Causa: `ui_status` puede llegar a
`104` (`UI_STATUS_EXIT`), que en Android dispara `NexusGLActivity.myActivity.finishApp()`
(`ZenoniaUIControllerView.setUIState()`, case 104) — una llamada puramente de Android que este loader no
tenía forma de reproducir. Sin manejarlo, el loop principal seguía corriendo para siempre sin dibujar
nada útil (pantalla negra permanente). **Solución (`loader/main.c`):** `if (g_ui_status == UI_STATUS_EXIT) break;`
en el loop principal — el proceso ahora sale limpio en vez de quedar colgado en negro. Círculo (`BACK`)
en el menú dispara ese flujo de salida nativo — comportamiento esperado del motor (equivalente a "salir");
no es un bug de esta fase, ahora simplemente el loader lo respeta en vez de colgarse.

**3. Falta `ui_title_touchthescreen.png` en el título.** Investigado (`grep` sobre
`zenonia3_java/sources/` completo y sobre los símbolos exportados de `libgameDSO.so`): el recurso existe
en `res/drawable/` y tiene un ID declarado en `public.xml`/`R.java`, pero **no aparece referenciado en
ningún `.java` decompilado ni en el `.so`** — a diferencia de los demás overlays (`ui_logo_gamevil`,
`gb_titleImg`, etc.), que si tienen un `findViewById`/`setVisibility` real en `Natives.java`. Conclusión:
es un asset **no cableado a ningún código en esta versión decompilada del APK** (probablemente arte
muerto de una build anterior, o de un flujo condicional no incluido en este release) — no se agregó,
para no inventar una posición/lógica de aparición sin evidencia real. Si en consola aparece necesario
igual, avisar para revisar de nuevo con un log fresco (podría estar detrás de una condición que no se ve
en el jadx, aunque no hay indicio de eso hasta ahora).

**Verificado:** compila limpio (`build.sh`), sin warnings nuevos en `androidui.c`/`main.c` (los 2
warnings de `implicit-function-declaration` en `main.c` son preexistentes, no relacionados). Pendiente de
confirmar en consola: ¿responden los 6 botones del menú al toque?, ¿tocar el título avanza al menú?, ¿el
juego ahora cierra limpio en vez de quedar en negro al salir desde el menú?

## Fase 6.3: pantalla negra en "Continuar" y "About" -- mismo patrón que 6.1/6.2, 2 estados de UI nuevos sin arte (2026-07-11)

Reporte del usuario: pantalla negra al tocar **Continuar** y al tocar **About** en el menú, con
`log_1783799903.txt` de esa corrida. El log confirma que **no hay crash** (no hay `.psp2dmp` de esta
corrida, y el log sigue vivo después de ambos toques) -- es el mismo bug de fondo que la Fase 6.1: un
`ui_status` que `androidui_draw()` (`loader/androidui.c:181`, antes solo cubría `-1/1/2`) no sabe
dibujar, así que se ve negro aunque el `.so` siga funcionando y navegable con los botones físicos.

Cruzando el log con `ZenoniaUIControllerView.java:setUIState()` (`zenonia3_java/sources/com/gamevil/
zenonia3/ui/ZenoniaUIControllerView.java`):
- **Continuar** manda `handleCletEvent(REPLY_YESNO=19450815, FIRST_MOVE_REPLY_PAGE=20010913, 0, 0)` (ya
  confirmado en Fase 6.2) -> `OnUIStatusChange: 5000` (`UI_STATUS_REPLY_PAGE`, línea 418 de ese archivo)
  -> `Natives.showReplyMoveComponent()`. **No es "continuar la partida"** -- es un popup nativo (2
  `ImageButton` reales, no WebView) de "valorar la app" que Android superpone antes de dejar seguir. El
  log confirma que SÍ es navegable ya (presionar Cruz ahí mandó `OnUIStatusChange: 14`, entrando al
  juego), solo que invisible.
- **About** manda `HAL_KEY_RIGHT` (-4) -> `OnUIStatusChange: 5` (`UI_STATUS_ABOUT`) ->
  `Natives.showAboutView(0)` -> fondo `aboutBg`/`ui_about_bg.png` + un `WebView` (`AboutWebView`) que
  carga `file:///android_asset/html/about.html` encima. El log confirma que Círculo (`HAL_KEY_BACK`, ya
  mapeado en `btn_map` de `main.c`) ya vuelve al menú desde ahí -- de nuevo, navegable, solo invisible.

**Solución (mismo mecanismo de la Fase 6.1, extendido a estos 2 estados):**
- 5 PNG nuevos convertidos con `tools/build_android_ui_assets.py` (variantes "globales"): `ui_about_bg.png`
  (fondo de About, 480x320), `ui_menu_back.png` (botón "back" de About, `uiback` en el XML, gravity
  `top|right` sin margin -- posición no inventada, sale de leer `main.xml`), `reply_page_back_e.png`
  (fondo de Continuar/REPLY_PAGE, 400x240), `button_write_01_global.png`/`button_later_01_global.png`
  (los 2 botones de esa pantalla). Posiciones de estos últimos 2: `Natives.java` define
  `REPLY_OFFSET_TOPMARGIN_0=170`, `REPLY_OFFSET_LEFTMARGIN_0=20` (escribir reseña),
  `REPLY_OFFSET_LEFTMARGIN_1=180` (más tarde) -- misma base lógica 400x240 que todo lo demás.
- `loader/androidui.c`/`.h`: `androidui_draw()` ahora también dibuja `ui_status==5` y `ui_status==5000`;
  2 funciones de hit-test nuevas (`androidui_about_hit_test()`, `androidui_reply_hit_test()`), mismo
  patrón que `androidui_menu_hit_test()` de la Fase 6.2.
- `loader/main.c`: el bloque de touch (antes solo interceptaba `ui_status==1/2`) ahora también
  intercepta `ui_status==5` (toque en el botón back -> `HAL_KEY_BACK` press+release, igual que Círculo) y
  `ui_status==5000` (toque en "escribir reseña"/"más tarde" -> `REPLY_YESNO` con
  `YES_MOVE_REPLY_PAGE=20010911`/`NO_MOVE_REPLY_PAGE=20010912`, constantes reales de `NexusHal.java`).
- `CMakeLists.txt`: los 5 `.rgba` nuevos empaquetados en el VPK (mismo mecanismo que los de la Fase 6.1).
- **Límite conocido, no bloqueante:** el texto real de `about.html` (créditos/versión, adentro del
  `WebView`) NO se reproduce -- no hay parser HTML en este loader. Solo se restituyó el fondo + el botón
  de salida, para que la pantalla no se vea negra y sea navegable. Si hace falta el texto real, sería un
  trabajo aparte (parsear ese HTML puntual y dibujarlo como texto plano con el rasterizador GFA ya
  existente, `loader/font.c`).

**Verificado:** compila limpio (`build.sh`, sin warnings nuevos), VPK generado con los 5 `androidui/*.rgba`
nuevos confirmados adentro (`unzip -l`). Pendiente de confirmar en consola real: ¿se ven los fondos de
Continuar/About en vez de negro?, ¿los botones nuevos responden al toque en las posiciones correctas?

## Fase 6.4: cruceta/botones que seguian visibles (fix real), texto real de ABOUT/HELP, y HELP dejaba de verse negro (2026-07-11)

El usuario confirmó que pudo continuar el juego (Fase 6.3 funcionó), pero reportó 3 pendientes: (1) no se ve
el texto de créditos/versión en ABOUT (esperado, esa fase solo puso el fondo), (2) la pantalla **HELP**
sigue en negro (no se había tocado ese `ui_status` todavía), y (3) la cruceta + botones de acción
"virtuales" (el `HIDE_VIRTUAL_GAMEPAD` de CMake, ON por default desde antes de esta sesión) **seguían
apareciendo** pese al toggle, y pidió poder ocultarlos (reversible) para jugar sin depender de la pantalla
táctil.

**1. La cruceta/botones nunca se ocultaron -- el mecanismo por textura estaba roto de raíz.** El log
(`log_1783799903.txt`) confirmó `Dpad.pzx` cargándose y activando el flag de "próxima textura a ocultar",
pero **nunca aparece un `glTexImage2D` después de ese `readAssets`** -- solo `glTexSubImage2D` contra un
atlas compartido (`w=512 h=512`) creado mucho antes. O sea: el motor empaqueta la cruceta/botones en el
MISMO atlas GL que otros elementos (título, HUD), así que "ocultar la próxima textura subida" no tenía
ninguna textura nueva que etiquetar -- el flag se prendía y nunca se consumía. Diagnóstico confirmado
cruzando esto con el pseudo-C real: `GVUIController::Draw()`/`PointerPress()`/`PointerMove()`/
`PointerRelease()` (`decompiled_so/out_ghidra.c:26330` y siguientes) son un simple `for (i in
0..*(this+0x19c)) { dibujar/tocar hijo[i] }`, y `GVUIPlayerController::GVUIPlayerController()`
(`out_ghidra.c:28609`, única instancia de esa clase en todo el juego, confirmado por symbol table -- no
existe `_ZN20GVUIPlayerController4DrawEv`, hereda el `Draw()` de la base) construye la cruceta + hasta 5
botones y llena ese contador. **Fix real (`loader/dynlib.c`,
`zenonia_install_hide_dpad_hook()`):** se hookea (`hook_addr`/`so_symbol`, mecanismo ya existente en
`so_util.c` pero sin uso previo en este proyecto) el constructor `_ZN20GVUIPlayerControllerC2Ev`, se deja
correr tal cual (arma todo normal), y **después** se pone `*(int*)(this+0x19c) = 0` -- Draw/touch de esa
UNICA instancia dejan de iterar nada, sin tocar ningún otro `GVUIController` (barra de vida/mana,
minimapa, etc., cada uno con su propio contador). El mecanismo viejo (texturas etiquetadas por
`glBindTexture_wrapper`/`g_hidden_textures`) se eliminó por completo -- estaba muerto, no solo ineficaz.
El toggle sigue siendo `-DHIDE_VIRTUAL_GAMEPAD=OFF` en cmake (mismo default ON).

**2. HELP (`ui_status==4`) es arquitectónicamente idéntico a ABOUT.** Confirmado en
`Natives.showHelpComponent()`/`ZenoniaUIControllerView.setUIStateHelp()`: mismo fondo+WebView+botón "back"
compartido (`uiback`, posición idéntica) que ABOUT, solo cambia el drawable de fondo (`ui_help_bg.png`,
480x320, agregado a `tools/build_android_ui_assets.py`/VPK) y el HTML (`help_eng.html` en vez de
`about.html`). `androidui.c` ahora trata `ui_status==4` igual que `5` (mismo bloque, fondo distinto).

**3. Texto real de ABOUT/HELP: HTML parseado y rasterizado en runtime (no hay WebView).** Nuevo par de
módulos:
- `loader/htmltext.c`/`.h`: extractor de texto plano especializado para estos 2 HTML reales del APK
  (`zenonia3/assets/html/about.html`/`help_eng.html`, empaquetados tal cual en el VPK vía
  `app0:html/*.html`) -- tablas simples `<tr><td>texto</td></tr>`, entidades básicas, bytes no-ASCII
  (bullets euc-kr/mojibake del HTML original) descartados. Verificado corriendo el extractor NATIVO en la
  máquina de desarrollo (sin dependencias de Vita) contra los 2 archivos reales: `about.html` -> 87 líneas
  lógicas limpias (créditos completos), `help_eng.html` -> 89 líneas (manual del juego) -- ambos legibles,
  sin texto roto.
- `loader/htmlview.c`/`.h`: wrapea cada línea al ancho real en píxeles del panel (mismo rect que Android
  le daba a `aboutWebView`/`helpWebView`: `leftMargin=1/400, topMargin=5/240, width=300/400,
  height=146/240`, igual para las 2 pantallas) usando `gfa_font_break_text` (Paint.breakText real, ya
  existía en `font.h`), rasteriza TODO el documento una sola vez con `gfa_font_draw_line` (mismo
  NanumGothic que ya usa el puente GFA) a una única textura GL alta, y la dibuja mostrando solo la ventana
  de scroll actual (cambiando las UV, no la textura). Blend especial (`GL_ONE, GL_ONE_MINUS_SRC_ALPHA`) en
  vez del `GL_SRC_ALPHA` normal de `androidui.c` porque `gfa_font_draw_line` escribe alpha premultiplicado.
  Título ("ABOUT"/"HELP") antepuesto a mano (calca `Natives.showHelpAboutTitleTextComponent()`).
- **Scroll:** sin capa Android no hay gesto táctil de WebView -- se reusa D-pad arriba/abajo mientras
  `ui_status` sea 4 o 5 (`loader/main.c`, consumiendo el input en vez de reenviarlo al motor, que de todos
  modos no hace nada con UP/DOWN en esos 2 estados).
- **Límite conocido, documentado a propósito:** solo se soporta el idioma por default (ni coreano ni
  japonés) -- `about_kor/jpn.html`/`help_kor/jpn.html` usan otra codepage (euc-kr/shift-jis) que
  `htmltext_extract` descarta a propósito (sin decoder real, no hay forma confiable de mapear esos bytes a
  un glifo). No bloqueante para este port (idioma real usado: inglés/global).

**Verificado:** compila limpio (`build.sh`, sin warnings nuevos). VPK confirmado con `ui_help_bg.rgba`,
`html/about.html` y `html/help_eng.html` adentro (`unzip -l`). El extractor de HTML se probó de forma
aislada (compilado nativo, sin Vita) contra los 2 archivos reales del APK -- salida verificada línea por
línea. Pendiente de confirmar en consola real: ¿la cruceta/botones ya no aparecen?, ¿se lee el texto de
ABOUT/HELP y responde el scroll con D-pad?, ¿HELP ya no se ve negro?

## Fase 6.5: panel de texto de ABOUT/HELP reposicionado -- el rect de Android no alineaba con el arte real (2026-07-11)

El usuario confirmó que ya no hay pantallas negras ni cruceta/botones virtuales (Fase 6.4 funcionó), pero
el texto de ABOUT/HELP se veía chico, pegado arriba a la izquierda, y superpuesto con el marco/título del
propio fondo.

**Causa:** el rect que usaba `htmlview_draw()` salía de `ZenoniaUIControllerView.onInitialize()`
(`leftMargin=1/400, topMargin=5/240, width=300/400, height=146/240`, los margenes que Android le daba al
`WebView` real) -- pero ese cálculo es independiente del arte de `ui_about_bg.png`/`ui_help_bg.png`
usado en este port especificamente (probablemente un drawable de un bucket de densidad distinto al que
Android eligió en el dispositivo original contra el que se tunearon esos márgenes). Medido con Pillow
directo sobre el PNG real (480x320, buscando el borde del cuadro gris oscuro): el cuadro real está en
`x=[27,452] y=[43,300]` -- notablemente más grande y más abajo que el rect viejo. Además el título
("ABOUT"/"HELP") se dibujaba DOS veces para ABOUT: una vez horneado en el propio PNG (banner superior,
confirmado buscando los pixeles de texto más brillantes: bbox `x=[123,358] y=[18,39]`, centro `(240,28)`)
y otra vez como primera línea de mi contenido scrolleable -- de ahí el "texto superpuesto".

**Fix (`loader/androidui.c`):**
- Rect del panel recalculado directo en el espacio de píxeles del PNG (480x320, que al dibujarse fitXY a
  pantalla completa mapea 1:1 a fracciones de pantalla) en vez del espacio 400x240 de Android:
  `INFO_BOX_LEFT_PX=40, INFO_BOX_TOP_PX=52, INFO_BOX_W_PX=400, INFO_BOX_H_PX=235` (con margen de ~13-14px
  contra el borde real medido, para no tocar el marco decorativo). Verificado visualmente dibujando este
  rect sobre el PNG real con Pillow antes de tocar código Vita -- el cuadro calza con el área gris.
- Tamaño de fuente subido de 13 a 15px (`INFO_FONT_PX`) y el padding interno del texto (`PANEL_PAD_X` en
  `htmlview.c`) de 6 a 10px -- más legible en el panel más grande.
- `htmlview_load()` ya NO antepone un título como primera línea (parámetro `title` eliminado de su firma) --
  ABOUT ya lo trae horneado en el PNG, no hace falta duplicarlo.
- HELP (cuyo banner viene VACÍO en `ui_help_bg.png` -- el título real era un `TextView` de Android aparte,
  `Natives.showHelpAboutTitleTextComponent()`) ahora dibuja "HELP" a mano como una etiqueta de una sola
  línea (`htmlview_make_label()`, función nueva en `htmlview.c`/`.h`, reusa el mismo tipo `htmlview`),
  centrada en la posición exacta del banner (medida igual que la de "ABOUT": centro `(240,28)` sobre
  480x320) -- no en el flujo del contenido scrolleable.
- Nueva función `htmlview_native_size()` para poder centrar esa etiqueta a mano (tamaño real de su textura,
  no un rect fijo).

**Verificado:** compila limpio (`build.sh`, sin warnings nuevos). Geometría confirmada visualmente
(overlay con Pillow sobre los 2 PNG reales antes de tocar el código -- el rect calculado coincide con el
cuadro gris real en ambas imágenes, y el centro del título calza sobre el banner). Pendiente de confirmar
en consola real: ¿el texto ahora se ve centrado, del tamaño correcto, y sin superponerse con el marco?,
¿se lee bien "HELP" en el banner de esa pantalla?

## Fase 6.6: colores/reglas reales de encabezado en ABOUT/HELP -- el usuario aportó capturas del Android original (2026-07-11)

El usuario reportó que el texto "seguía en la misma posición arriba a la izquierda" pese a la Fase 6.5, y
adjuntó 2 fotos reales del juego Android original (ABOUT en coreano, HELP en inglés) pidiendo que se vea
"así". Cruzando esas fotos con el HTML real: **HELP se ve alineado a la IZQUIERDA** (no centrado) con
encabezados de sección en celeste/negrita + una línea horizontal fina arriba y abajo de cada uno (ej.
"Game Introduction", "Short Keys"), y texto de cuerpo blanco simple debajo -- es decir, la posición ya
estaba bien (Fase 6.5), lo que faltaba era el estilo real (colores + reglas), y sin eso el bloque de texto
plano blanco se "leía" como si no hubiera cambiado.

**Fix:** `loader/htmltext.c`/`.h` ahora detecta, al abrir un `<td class="style3">` (encabezado de sección,
ej. "Contact"/"Credits"/"Game Introduction") o `<td class="style2b">` (etiqueta, ej. "Executive Producer",
"Web page") -- únicamente si la celda arranca una línea nueva -- y antepone un byte de control
(`HTMLTEXT_STYLE_HEADER`/`HTMLTEXT_STYLE_LABEL`) a esa línea de salida. Verificado corriendo el extractor
de forma aislada (nativo, sin Vita) contra los 2 archivos reales -- confirmado que "Game Introduction",
"Short Keys", "Class", "Ability", "Status Info" (help_eng.html) y "Contact"/"Credits"/"Executive
Producer"/"Web page"/etc. (about.html) quedan marcados correctamente.

`loader/htmlview.c` interpreta esos marcadores: colores reales sacados del CSS del HTML original
(`.style3` = `#00b0f0` celeste, `.style2b` = `#FD9E35` naranja, `.style2`/cuerpo = blanco -- blanco en vez
del `#999999` gris real de la hoja de estilos porque ese gris estaba pensado para fondo claro, y acá el
fondo es oscuro) y dibuja una regla horizontal solida arriba y abajo de cada encabezado (`draw_hrule()`,
directo en el buffer ARGB antes de rasterizar el texto -- calca `.style3 { border-top: solid; border-bottom:
solid; border-color: 00b0f0 }` del CSS real).

**Límite conocido, no bloqueante:** los resaltados INLINE dentro de una misma línea mezclada con texto
normal (`style13` amarillo -- ej. "ZENONIA 3"/"Chael" en la foto de referencia -- y `style14` naranja
usado para las etiquetas cortas tipo "HP:"/"Strength:" dentro de help_eng.html) NO se distinguen -- el
marcador es por LINEA completa, no por tramo de texto dentro de una línea, y esos casos vienen mezclados
en la misma celda HTML junto con texto `style2` normal. Requeriría trackear rangos de color dentro de una
línea (parser + rasterizador con "runs" de color), no implementado en esta pasada.

**Pendiente de reportar por el usuario, sin poder diagnosticar mas sin evidencia nueva:**
1. **El botón "back" (`ui_menu_back.png`, esquina superior derecha de ABOUT/HELP) no responde al toque.**
   El código (`androidui_backbtn_hit_test()` en `androidui.c`, llamado desde `main.c`) parece correcto en
   una relectura completa -- hitbox de 49x49px nativos en una pantalla de 960x544 (~5% del ancho), bastante
   chico, podría ser un problema de precisión más que un bug real. Sin un log fresco no se puede confirmar
   cuál de las 2 cosas es.
2. **Navegar el menú con cruceta física entra directo a "Opciones" (con UP) pero después el touch dejó de
   responder.** Esto es esperado en parte: `Options` -> `HAL_KEY_UP` es la MISMA tecla que el
   `OnClickListener` del botón "Options" real en Android (confirmado en Fase 6.2) -- o sea, la cruceta NO
   mueve un cursor visual, activa directamente el botón correspondiente (así funcionaba el Android
   original, sin sistema de foco/cursor). Lo que sí es un bug real a investigar: por qué se deshabilita el
   touch una vez adentro de "Opciones" -- no se sabe todavía qué `ui_status` reporta esa pantalla ni si nuestro
   código de `main.c` la está tratando por error como uno de los estados ya interceptados (1/2/4/5/5000).
   **Se necesita un log fresco de esta secuencia exacta** (abrir menú -> cruceta arriba -> intentar tocar)
   para diagnosticar sin adivinar.

**Verificado:** compila limpio (`build.sh`, sin warnings nuevos). El extractor de estilos se probó de
forma aislada (nativo, sin Vita) contra los 2 archivos reales -- marcadores confirmados en las líneas
correctas.
