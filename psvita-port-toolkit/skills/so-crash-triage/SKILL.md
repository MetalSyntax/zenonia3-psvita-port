---
name: so-crash-triage
description: Metodología para diagnosticar la causa REAL de un crash en un port de Android a PS Vita (soloader + FalsoJNI) cruzando 3 fuentes — el .so real (objdump/nm), su pseudo-C decompilado con Ghidra, y el Java del APK decompilado con jadx — en vez de adivinar. Parte de un log de consola + un .psp2dmp y termina en la línea exacta de código nativo que hay que corregir. Confirmada en múltiples bugs reales resueltos en consola física (no solo teoría).
---

# Triage de crashes en ports Android→Vita (soloader) cruzando .so real + decompilado + APK

Esta skill documenta el **proceso repetible** para pasar de "el juego crasheó, tengo un log y un
`.psp2dmp`" a "esta línea exacta del `.so` es la causa, y esto es lo que hay que registrar/parchear en
el loader" — sin adivinar. Se destiló de varios bugs reales resueltos así en un port de PS Vita (motor
Gamevil Nexus2/Clet, pero el método no es específico de ese motor).

## Cuándo usar esta skill

- Estás depurando un crash real (consola física o Vita3K) de un port basado en `so_util`/FalsoJNI, y
  tenés: un log de la corrida (`game_log`/similar) y un `.psp2dmp` de esa misma corrida.
- Tenés (o podés generar) 3 cosas sobre el binario del juego: el `.so` original, un dump decompilado con
  Ghidra headless (`decompiled_so/out_ghidra.c` o similar), y el APK decompilado con `jadx` (fuente Java
  original, si el motor tiene una capa de callbacks JNI hacia Java).
- Ver también la skill `psvita-porting` para los patrones de bugs YA conocidos en esta clase de ports
  (jni_stubs, asset_packaging, etc.) — esta skill es el **procedimiento de investigación** para
  encontrar un bug nuevo que esos patrones no cubren todavía.

## Herramientas necesarias

- `vita-parse-core` (Python, requiere el toolchain de VitaSDK en el `PATH`) — parsea el `.psp2dmp` contra
  el `.elf` del loader (compilado con símbolos, no el `.velf`/`self`) y da threads, PC/LR, registros, y un
  volcado de stack.
- `arm-vita-eabi-objdump` de VitaSDK (**no** el `objdump` del sistema para desensamblar) — el `objdump`
  del sistema (Xcode CLT en Mac, por ejemplo) alcanza para leer la tabla de símbolos dinámicos de un ELF
  ARM (`objdump -T`) sin necesitar el toolchain cruzado, pero **para desensamblar instrucciones hace
  falta el toolchain de VitaSDK** con la bandera `-M force-thumb` (el `.so` es casi seguro Thumb-2, y sin
  esa bandera el desensamblador decodifica basura).
- El pseudo-C de Ghidra headless (`docker run devrvk/so-decompiler ...` u otro método) — mucho más rápido
  de leer que el ensamblador para entender lógica de control de flujo y accesos a campos de struct.
- `jadx` sobre el APK — la fuente de verdad Java cuando el motor tiene una capa de callbacks nativo↔Java
  (JNI). Buscar por nombre de método ANTES de adivinar semántica.

## El procedimiento, paso a paso

### 1. Leer el log primero — no el dump

El log (con `fflush` después de cada línea) dice exactamente **qué estaba pasando** justo antes del
corte. Mirar la ÚLTIMA línea real antes de que se corte, no solo "crasheó". Si el log muestra intentos de
`GetStaticMethodID(..., "Nombre", ...)`/`CallStatic*MethodV` fallando ("not found") justo antes del
corte, ya hay una hipótesis fuerte: un método JNI sin registrar. Anotar el **tipo de retorno** del método
(`Z`=boolean, `I`=int, `[F`=float[], `V`=void, etc. — viene en la firma JNI del log) porque determina si
el "no encontrado" es peligroso o no (ver sección de patrones abajo).

### 2. Parsear el `.psp2dmp` con `vita-parse-core`

```bash
export VITASDK=~/vitasdk   # o la ruta real de tu instalación
export PATH="$VITASDK/bin:$PATH"
cd ~/vita-tools/vita-parse-core   # o donde lo tengas
source venv/bin/activate
python3 main.py <archivo>.psp2dmp <loader>.elf
```

Esto da: tipo de excepción (Data abort / Prefetch abort), `PC`, `LR`, todos los registros, y un volcado
de stack. **El `.elf` debe ser el que generó EXACTAMENTE ese build** (mismo VPK instalado en esa corrida)
— si no coinciden, las direcciones no van a resolver a nada con sentido.

### 3. Ubicar PC y LR — decidir cuál de los dos importa

- Si `PC` cae dentro del rango de direcciones del `.so` del juego (`text_base` + offset, normalmente
  `0x98000000`-algo en estos loaders), **ese** es el frame que importa: fue el `.so` el que ejecutó la
  instrucción que falló.
- Si `PC` cae dentro de un módulo real de Sony (`SceLibKernel@1 + 0x...`) o dentro del propio loader
  (`zenonia_3@1 + 0x...` o el nombre que tenga tu ejecutable), **`LR` es el frame relevante**: significa
  que el `.so` (o tu loader) llamó a una función del sistema/de tu propio código, y ESA función falló por
  culpa de un argumento inválido que le pasó el `.so` (puntero `NULL`, tamaño corrupto, etc.) — hay que
  mirar qué le pasó el `.so` en el call site de `LR`, no perseguir el `PC`.

### 4. Resolver la dirección contra los símbolos del `.so`

```bash
objdump -T libgameDSO.so | grep -v UND | awk '{print $1, $NF}' | sort > syms.txt
```

(El `objdump` del sistema alcanza para esto — es solo lectura de tabla de símbolos, no desensamblado.)
Buscar el símbolo con la dirección más alta que sea `<=` la dirección buscada (un pequeño script Python
de 10 líneas alcanza, o `nm -n` + revisar a ojo si la lista no es gigante). Eso da **la función y el
offset dentro de ella** donde cayó `PC`/`LR`.

### 5. Desensamblar esa función con el toolchain correcto

```bash
arm-vita-eabi-objdump -d -M force-thumb --start-address=<inicio_función> --stop-address=<fin_función> libgameDSO.so
```

Leer la instrucción exacta en el offset del crash y las 5-10 instrucciones anteriores. Buscar:
patrones `ldr rX, [rY, #N]` inmediatamente antes de la instrucción que falló (casi siempre es
"dereferenciar un puntero que resultó ser `NULL`" — mirar de dónde vino ese puntero, unas líneas para
atrás, para saber si es el resultado de una llamada a función).

### 6. Cruzar con el pseudo-C de Ghidra — mismo lugar, mucho más rápido de leer

```bash
grep -n "NombreDeLaFuncion" decompiled_so/out_ghidra.c
```

Buscar por el nombre demangled (Ghidra generalmente ya lo demanglea, o aparece como comentario arriba de
la función). El pseudo-C muestra la MISMA lógica que el ensamblador pero con nombres de campos de struct
inferidos y control de flujo legible — mucho más rápido para entender "¿qué se supone que iba a pasar
aquí?" que seguir saltos de ensamblador a mano.

**Ojo con las direcciones de vtable placeholder de Ghidra.** Si el pseudo-C muestra algo como
`*(void***)this = &PTR__NombreClase_1_00XXXXXX`, esa dirección `00XXXXXX` casi seguro **no es una
dirección real** del binario (es un símbolo sintético que Ghidra inventa cuando no puede resolver la
vtable real) — no la uses para nada. Para conseguir la vtable REAL:

```bash
objdump -T libgameDSO.so | grep "_ZTV<NombreDeClaseMangled>"   # ej: _ZTV14CGxFACharCache
```

Esa dirección es la tabla vtable-con-RTTI real (formato Itanium ABI: slot 0 = offset-to-top, slot 1 =
puntero a typeinfo, slot 2 en adelante = punteros a función reales). El puntero de vtable que el objeto
guarda en runtime es esa dirección **+8** (salteando los 2 primeros slots). Volcar el contenido real:

```bash
objdump -s -j .data.rel.ro --start-address=<addr> --stop-address=<addr+0x50> libgameDSO.so
```

Cada palabra de 4 bytes (little-endian) a partir de `<addr>+8` es un puntero a función real — resolvé
cada uno contra la lista de símbolos del paso 4 para saber qué método virtual es cada offset de vtable.

### 7. Si es una llamada JNI hacia Java, buscar la implementación Java REAL antes de adivinar

```bash
grep -rln "NombreDelMetodo" ruta/al/apk_decompilado/sources/
```

Casi siempre existe la clase Java real que implementa esa familia de métodos (aunque no esté en la clase
"principal" tipo `Natives.java` — puede estar en una clase de utilidad separada). Leerla te da:
- La semántica exacta esperada (qué hace, qué formato de datos espera/devuelve).
- Si hay estado compartido entre métodos de la misma familia (ej. "el texto actual" seteado por un método
  y leído por otro) que hay que replicar con una variable estática propia.
- Los casos borde reales (ej. "si ya está inicializado, no hacer nada") que el código nativo puede estar
  asumiendo.

Si el método **no existe en ningún lado del Java decompilado**, es señal de que ese camino de código es
legado/muerto en el APK real (una versión vieja del SDK que nunca se actualizó) — de todos modos hay que
hacer que no crashee, pero no hace falta implementarlo con fidelidad total.

### 8. Antes de escribir el fix: leer el WRAPPER NATIVO completo, no solo la firma

Esto es el error más caro de cometer. No todo método sin registrar es "seguro por default" del mismo
modo:

| Tipo de retorno JNI | Valor por default de FalsoJNI si el método no está registrado | ¿Es peligroso? |
|---|---|---|
| `void` | no hace nada | Casi siempre seguro. |
| `Object` (`jobject`/arrays) | `NULL` | **Depende** — seguro SOLO SI el código nativo que llama lo consume con un chequeo de `NULL` antes de usarlo (ej. `GetArrayLength`+`GetXArrayRegion`, que toleran `NULL` con longitud 0). **Peligroso si el código nativo desreferencia el resultado de `GetXArrayElements` sin chequear** (patrón visto real: `puVar = GetFloatArrayElements(...); *puVar` sin `if (puVar)`). |
| `boolean` | `JNI_FALSE` (0) | Normalmente seguro, pero confirmar si el código nativo hace algo raro con "falso" (ej. saltear una inicialización que hace falta para otra cosa). |
| `int`/`long`/`byte`/`short`/`char`/`float`/`double` | `-1` (para tipos enteros) | **Peligroso** si el código nativo lo usa como índice, tamaño, o lo compara con `>= 0` para decidir si algo es válido — `-1` es casi siempre un valor con significado especial que el motor SÍ chequea. |

La única forma confiable de saber cuál aplica es **leer el pseudo-C del wrapper nativo que hace la
llamada JNI** (paso 6) y ver textualmente qué hace con el valor de retorno antes de escribir el stub.

### 9. Implementar, recompilar, redeployar — un bug a la vez

No intentar arreglar 5 cosas a la vez basándose en teoría. Arreglar lo que el log+dump de ESTA corrida
confirma, recompilar, pedir el log nuevo, repetir. La excepción razonable: si al leer el wrapper nativo
del paso 6 aparecen 2-3 funciones hermanas de la MISMA familia con el MISMO patrón de riesgo confirmado
(ej. `GFA_DrawFont`/`GFA_DrawText`/`GFA_MeasureText` todas desreferencian su array sin chequear NULL),
vale la pena arreglar las hermanas también en la misma pasada — evita otra ronda completa de
instalar+probar en consola solo para pisar el mismo patrón en la función de al lado.

## Patrones de bug recurrentes en esta clase de ports (para reconocer más rápido)

- **`pthread_mutex_t`/`pthread_cond_t` de Bionic vs. VitaSDK.** En VitaSDK estos tipos son PUNTEROS
  (`typedef struct pthread_mutex_t_ * pthread_mutex_t;`). Bionic permite mutex/condvar estáticos
  (`PTHREAD_MUTEX_INITIALIZER`) que quedan en ceros sin llamar nunca a `pthread_mutex_init` — Bionic trata
  esos ceros como válidos nativamente; en VitaSDK son un `NULL` real que la implementación de PTE
  desreferencia sin chequear. Señal: crash dentro de `pthread_mutex_lock/unlock` con el primer campo del
  mutex en `0`, llamado desde código C++ genérico (STL/libstdc++), no necesariamente del código del
  juego. Fix: wrapper que detecta el puntero `NULL` y llama `pthread_mutex_init`/`cond_init` on-demand.
- **Allocadores internos del motor con orden de inicialización implícito.** Motores viejos (J2ME/BREW
  derivados) a veces tienen su propio allocator tipo pool/slab (no pasa por `malloc`) que depende de que
  una función de inicialización específica haya corrido antes — y esa dependencia no está documentada en
  ningún lado salvo el desensamblado. Señal: un puntero "sano" (no basura) en `NULL` justo donde debería
  haber una estructura ya alocada. Rastrear hacia atrás qué función lo pobla, y confirmar en qué llamada
  nativa (`NativeInit*`/similar) se dispara.
- **Convención de representación de `jstring` vs. arrays en FalsoJNI.** En variantes simplificadas de
  FalsoJNI, `NewStringUTF` puede devolver el `char*` crudo directamente (no una estructura opaca) — pero
  `NewByteArray`/`NewFloatArray`/etc. SÍ usan una estructura opaca (`JavaDynArray*`, con campos `array`/
  `len`/`type`) gestionada por un allocador propio (`jda_alloc`/`jda_find`). No asumir que un parámetro
  `jobject` es un `char*` sin confirmar cuál mecanismo generó ese valor.
- **Handles `-1` que el motor SÍ chequea.** Ver tabla de la sección 8 — es el bug más común y más fácil de
  introducir por descuido al portar un motor viejo tipo NDK pre-ART.
- **Un mismo método JNI con dos consumidores que esperan representaciones de array distintas.** Un motor
  viejo puede tener una ruta de código que lee el resultado de un método JNI tipo `[B` por **puntero
  crudo** (convención pre-ART: header de "ArrayObject" de Dalvik de 16 bytes + datos, típico de
  `readAssets`/`MC_knlGetResource`) mientras que OTRO subsistema (agregado después, ej. un parser de
  formato de archivo) llama a `GetArrayLength`/`GetByteArrayElements` **estándar** sobre el mismo
  resultado. Si tu stub solo devuelve una de las dos representaciones, el segundo consumidor recibe
  `NULL`/longitud 0 sin crashear ahí mismo ("Could not find the array" — no fatal en el momento), y el
  crash real aparece más adelante, cuando ese código asume que el resultado era válido. Solución: en vez
  de elegir una sola representación, interceptar las funciones de array de la tabla JNI
  (`GetArrayLength`/`GetByteArrayElements`/`GetByteArrayRegion`/`ReleaseByteArrayElements` — mutables en
  memoria pese al tipo `const` público, ya que `jni_init()` las aloca con `malloc()`) para reconocer tus
  propios bloques (un registro simple de punteros) y servirlos desde ahí, cayendo al código real de
  FalsoJNI para cualquier otro array genuino. Nota de C: si el proyecto compila `.c` puro (no `.cpp`),
  `JNIEnv` es literalmente `const struct JNINativeInterface*` (no una struct con campo `.functions` —
  eso es solo la variante C++ de `jni.h`), así que la variable global `jni` **es** la tabla de funciones;
  alcanza con `(struct JNINativeInterface *)(uintptr_t) jni` para escribir los punteros directamente.

## Ejemplo real resuelto con este método

Ver `port_progress.md` de un port de Zenonia 3 a PS Vita, Fase 3.4 y 3.5: un crash de "puntero NULL en un
árbol interno de cache de fuentes" (`CGxFACharCache::findChar`) se resolvió rastreando hacia atrás por 4
niveles de llamadas (constructor → singleton lazy → `CGxFontAndroid::Create` → chequeo `if (-1 <
fontHandle)`) hasta encontrar que dependía del valor de retorno de un método JNI (`GFA_CreateFont`) sin
registrar — y la vtable real de la clase se resolvió vía su símbolo `_ZTV...` cuando el pseudo-C de
Ghidra mostraba solo un placeholder sin dirección válida.
