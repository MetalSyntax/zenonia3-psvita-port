# Guía paso a paso: port de un juego Android (cocos2d-x) a PS Vita vía SoLoader

Esta guía es una destilación **genérica** (sin nada específico de un juego puntual) de un port real,
llevado de cero a jugable en hardware físico. Cubre la arquitectura de "soloader" — un ejecutable Vita
nativo que carga los `.so` de Android directamente (sin recompilarlos), resolviendo sus imports contra una
tabla de funciones propia y emulando JNI con algo como FalsoJNI. Aplica sobre todo a juegos basados en
**cocos2d-x** (2.x), que es el motor más común en este tipo de ports, pero varias secciones (toolchain,
input, VPK, debugging) aplican a cualquier port por esta vía.

Para el detalle técnico profundo de cada tema, esta guía referencia los archivos en `skills/psvita-porting/
references/` incluidos en este mismo paquete — están pensados para cargarse como una Skill de Claude Code,
pero se pueden leer como documentación suelta igual.

## Fase 0 — Prerrequisitos y toolchain

1. **VitaSDK** instalado (preferentemente `-softfp`, ver `references/vitasdk_setup.md`). En Apple Silicon /
   Linux, `vdpm` es la vía estándar para paquetes binarios.
2. **`kubridge`** — plugin de kernel necesario en hardware real (no en Vita3K) para que la app en modo
   usuario pueda mapear y ejecutar el código ARM de los `.so` de Android. Se instala como plugin de taiHEN.
3. **`libshacccg.suprx`** (el compilador de shaders de Sony) presente en `ur0:data/` de la consola de
   pruebas — sin esto, el juego debe abortar con un error explícito al arrancar, no crashear en silencio
   (ver Fase 6).
4. Si el proyecto depende de **vitaGL**: revisar qué versión/commit es compatible con la versión de
   `vitasdk-softfp` instalada — la de `vdpm` puede traer una API más nueva que rompe cosas en un emulador o
   viceversa. Fijar un commit conocido y documentar por qué.
5. **Cuidado con rutas con espacios** en el path del proyecto — rompen `vita-pack-vpk`. Ver
   `references/toolchain_gotchas.md`.

## Fase 1 — Analizar el APK/OBB original

Antes de escribir una línea de código del loader:

1. Extraer el `.apk` (es un zip) y el `.obb` si existe. Buscar:
   - `libcocos2d.so`, `lib<motor>.so`, `lib<juego>.so` (o el nombre que corresponda) en `lib/armeabi/` o
     `lib/armeabi-v7a/`.
   - `AndroidManifest.xml` para el Activity/paquete principal.
   - `assets/` — buscar archivos de configuración (`appConfig.txt` o similar) y assets sueltos.
2. Con `nm -D --defined-only <lib>.so | c++filt`, listar los símbolos **exportados** de cada `.so` — ahí
   están los puntos de entrada nativos que hay que llamar desde el loader (`Java_org_cocos2dx_lib_..._native*`
   para cocos2d-x) y las funciones de callback que el motor podría necesitar de vuelta (ver Fase 4).
3. Con `strings <lib>.so | grep -i <palabra clave>`, confirmar hipótesis sobre mecánicas del juego (por
   ejemplo, si el control de movimiento es por touch/joystick virtual o por teclado — buscar
   `"joystick"`/`"dpad"`/`"keycode"`) **antes** de implementar el mapeo de input, para no adivinar.
4. Decompilar el `AndroidManifest.xml`/código Java si hace falta entender el orden real de llamadas nativas
   desde `Activity.onCreate()` (`nativeSetPaths`, `nativeInit`, etc.) — la documentación de la API pública de
   cocos2d-x no siempre coincide con lo que un build específico realmente exporta o espera.

## Fase 2 — Estructura base del loader

Ver `references/so_loading.md` para el detalle. Puntos clave:

- Un `so_module` por cada `.so` a cargar, con direcciones base incrementales (`LOAD_ADDRESS`,
  `LOAD_ADDRESS + 0x1000000`, etc.) para que no se superpongan.
- Orden: `so_file_load` → `so_relocate` → `resolve_imports` (contra una tabla `default_dynlib` propia,
  mapeando cada símbolo de libc/GL/etc. que el `.so` importa a una implementación real o un stub) →
  (`so_patch`, opcional — ver Fase 8) → `so_flush_caches` → `so_initialize`.
- Si hay más de un `.so` con `JNI_OnLoad` exportado, **llamarlo en todos**, no solo en el primero que lo
  tenga — cada módulo cachea su propio `JavaVM*` en un global interno, y si no se lo dan se cae la primera
  vez que ese módulo necesita un `JNIEnv` (típicamente en el subsistema de audio).

## Fase 3 — FalsoJNI y resolución de métodos nativos

Ver `references/jni_bindings.md` y `references/jni_stubs.md`.

1. Resolver a mano, con `so_symbol`, los puntos de entrada nativos del ciclo de vida
   (`nativeInit`, `nativeRender`, `nativeSetPaths`, `nativeTouches*`, `nativeKey*`, etc.) — llamarlos desde
   el `main()` del loader en el mismo orden que Android real (confirmado en la Fase 1, no adivinado).
2. Para cada método que el `.so` llama de **vuelta** hacia "Java" (vía JNI simulado) y que FalsoJNI no
   encuentra (`Failed to find [static] method id of X` en el log), hay dos casos:
   - **No-op seguro**: la acción no tiene sentido en este port (analítica, redes sociales, anuncios) y el
     motor no espera ningún callback de vuelta.
   - **Requiere disparar un callback de "completado"**: si el motor bloquea esperando que Android le avise
     que algo terminó (video, animación nativa, diálogo del sistema) — un no-op vacío lo cuelga para
     siempre. Ver el patrón exacto en `references/jni_stubs.md`.
3. No asumir que un stub "no hace nada" está mal solo porque no ves efecto — confirmar que está bien
   registrado en **ambas** tablas que suele usar este tipo de arquitectura (nombre→ID y tipo de
   retorno→implementación).

## Fase 4 — Gráficos (vitaGL / shaders)

Ver `references/optimization.md` y `references/vitasdk_setup.md`.

1. Verificar que `ur0:data/libshacccg.suprx` (o su ruta alterna) esté presente **antes** de inicializar GL —
   mostrar un error explícito si falta, no dejar que el juego llegue a un estado inconsistente.
2. Desactivar triple buffering (`vglUseTripleBuffering(GL_FALSE)`) para juegos 2D simples — reduce latencia y
   ahorra VRAM.
3. Si el juego usa shaders GLSL que hay que traducir a Cg/GXP en tiempo de build o de ejecución, cachear el
   resultado en disco (hash del shader source → `.gxp`) para evitar recompilar en cada arranque.
4. Cuidado con activar MSAA/anti-aliasing en emuladores (puede disparar una segunda inicialización de
   contexto en el emulador que no ocurre en hardware real) — condicionar por un flag de build tipo
   `EMULATOR_BUILD`.

## Fase 5 — Assets: `.apk`/`.obb` vs. archivos sueltos

Ver `references/asset_packaging.md` — **la decisión más impactante en tamaño final del port.**

1. `nativeSetPaths` suele recibir 2-3 argumentos con semántica específica (no siempre documentada
   correctamente): una carpeta a la que el motor le concatena el nombre real del `.obb` para abrirlo como
   ZIP, y una ruta directa al `.apk` para leer `assets/`. Confirmar experimentalmente cuál es cuál (probar,
   leer el log, no asumir).
2. `CCFileUtils::getFileData` (o el mecanismo equivalente del motor) casi siempre tiene un **fallback**: si
   no encuentra el archivo suelto, lo busca dentro del `.obb`/`.apk` como ZIP. Este fallback aplica a
   *cualquier* archivo, no solo a los "obvios".
3. Elegir **una sola estrategia** — o todo suelto, o todo vía `.apk`/`.obb` armados a mano (nunca ambos a la
   vez, duplica el peso en la tarjeta). Armar los ZIPs con Python en vez de copiar los originales completos
   (que suelen traer contenido irrelevante para el port).

## Fase 6 — Arrancar y diagnosticar en consola real

Ver `references/hardware_debugging.md` — **el flujo de trabajo que hace posible todo lo demás.**

1. Logging a archivo (uno nuevo por ejecución) en la propia tarjeta de memoria — no depender de plugins de
   captura de red.
2. Método iterativo: un fallo a la vez (`fopen(...): 0x0`, `Failed to find method id`, freeze), arreglarlo,
   recompilar, reprobar. No tratar de adivinar de antemano toda la lista de lo que podría faltar.
3. Cuando el log no alcanza (crash sin ninguna pista, o corrupción de memoria que aparece lejos de su causa
   real), analizar el `.psp2dmp` que genera la consola con `vita-parse-core` — instrucciones completas de
   setup (incluye el parche necesario para Python moderno) en `references/hardware_debugging.md`.

## Fase 7 — Input: touch y botones físicos

Ver `references/input_handling.md` — tres bugs reales, ya resueltos, que cuestan mucho tiempo si se
reinventan desde cero:

1. El touch está **apagado por default** en la Vita — `sceTouchSetSamplingState()` es obligatorio antes de
   que `sceTouchPeek()` reporte nada.
2. **Nunca pasarle al motor el ID crudo del touch de la Vita** — es un contador de 8 bits sin techo durante
   toda la sesión, no un rango chico como los `pointer id` de Android. Mapear cada touch real (y cualquier
   touch sintético que se agregue, ver punto 3) a un slot chico y estable (0 a `CC_MAX_TOUCHES-1`, típicamente
   5 en cocos2d-x/Android) — nunca inventar un slot "extra" fuera de ese rango, por más que "seguro no
   choca": es la misma clase de bug (escritura fuera de los límites del array interno del motor).
3. Si el juego original solo entiende movimiento por drag de un joystick táctil virtual (nada raro en juegos
   de Android de esa época — confirmarlo con `strings`/`nm` antes de asumirlo), mapear los botones físicos
   (D-Pad, etc.) sintetizando touches en la posición de ese joystick, compitiendo por el mismo pool de slots
   que los dedos reales — no un mecanismo aparte.

## Fase 8 — Empaquetado final del `.vpk`

Ver `references/vpk_packaging.md` y `references/livearea_assets.md`.

1. Las 4 imágenes de LiveArea (`icon0.png`, `pic0.png`, `startup.png`, `bg0.png`) deben ser PNG **indexado**
   de 8 bits, tamaños exactos — verificar los chunks `IHDR` a mano si hay dudas, no confiar solo en la
   extensión del archivo.
2. `template.xml` de LiveArea: usar `style="a1"` (estándar de homebrew) salvo que se sepa exactamente por qué
   hace falta otro.
3. Si la instalación falla con un código de error cerca del final (ej. `0x8010113D`), no asumir la causa más
   citada en foros sin verificarla — puede ser otra cosa (revisar cualquier archivo de configuración de
   LiveArea/paquete tocado recientemente).

## Fase 9 — Checklist rápido para un port nuevo

- [ ] Toolchain instalado, `kubridge` y `libshacccg.suprx` confirmados en la consola de pruebas.
- [ ] `.so` analizados con `nm`/`strings`, puntos de entrada nativos identificados.
- [ ] Loader carga los `.so`, resuelve imports, llama `JNI_OnLoad` en todos los módulos que lo exportan.
- [ ] `nativeSetPaths`/equivalente llamado con la semántica correcta (confirmada, no asumida).
- [ ] Primer frame renderiza (shaders/GL funcionando).
- [ ] Estrategia de assets elegida y decidida (una sola, no las dos).
- [ ] Logging a archivo por ejecución funcionando — este es el que va a sostener todo el resto del debugging.
- [ ] Touch sampling activado, IDs de touch mapeados a slots chicos y estables.
- [ ] Botones físicos mapeados donde el motor realmente los escucha (confirmado con `strings`/`nm`, no
      adivinado).
- [ ] `.vpk` instala sin errores en consola real (LiveArea válido).
- [ ] Loop de "un bug a la vez guiado por el log" hasta que el juego sea jugable de punta a punta.
