# Input táctil y de botones: dos bugs que parecen "no anda nada" pero son puntuales

## El touch está apagado por default — hay que activarlo explícitamente

`sceTouchPeek()`/`sceTouchRead()` **siempre devuelven `reportNum=0`** (cero toques, siempre) hasta que se
llama a `sceTouchSetSamplingState()`. Esto es fácil de pasar por alto porque el resto del código de touch
(lectura, escalado de coordenadas, forward a `nativeTouchesBegin/Move/End`) puede estar perfecto y el touch
sigue sin responder — el bug no está en la lógica, está en que nunca se prendió el sensor:

```c
// Una sola vez, antes del loop principal -- sin esto sceTouchPeek jamás reporta nada
sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
```

Si el juego original de Android controla el movimiento (caminar, joystick virtual) por touch y usa botones
físicos solo para acciones puntuales (salto, agachar), un síntoma confuso de este bug es "los botones andan
pero no puedo moverme" — no parece un bug de touch a primera vista.

## Nunca mandarle al motor el ID crudo de `SceTouchReport` — corrompe el heap

`SceTouchReport::id` (`psp2/touch.h`) es un contador de **8 bits que sigue creciendo durante toda la
sesión** — no es un rango chico (0-4) como los `pointer id` de `MotionEvent` en Android real. Si se le pasa
ese id crudo directo a `nativeTouchesBegin`/`Move`/`End`, y el motor lo usa internamente para indexar un
array de tamaño fijo (pensado para IDs chicos de Android), un id grande **escribe fuera de los límites y
corrompe el heap** — no siempre crashea al toque, a veces recién se nota más tarde en un `free()` no
relacionado, lo que hace parecer que el bug está en otro lado. Confirmado con `vita-parse-core` sobre
volcados reales: un crash cayó directo dentro de `nativeTouchesEnd`, otro fue corrupción de heap descubierta
recién dentro de `free()`, con la pila mostrando `nativeTouchesEnd → CCObject::release() → ~CCSet()` como
camino real.

Un primer síntoma relacionado pero **distinto** (y más fácil de confundir con este bug): si además el índice
del loop (`i`) no coincide con el id real usado en `Begin`/`Move`, el motor nunca recibe el `End` del touch
que efectivamente trackeaba — el personaje queda moviéndose solo sin responder a nada. Pero **arreglar solo
eso no alcanza** si seguís mandando el id crudo — hace falta resolver los dos problemas juntos.

**Fix correcto**: nunca mandar el id crudo del hardware al motor. Mantener un slot chico y estable (0-4) por
dedo — el id crudo solo se usa para reconocer qué dedo es cuál entre frames (buscarlo en los slots ya
asignados, o darle uno libre si es nuevo):

```c
int lastX[5] = {-1, -1, -1, -1, -1};
int lastY[5] = {-1, -1, -1, -1, -1};
int slotHwId[5] = {-1, -1, -1, -1, -1};  // id crudo del hardware ocupando cada slot, -1 = libre

int seen[5] = {0, 0, 0, 0, 0};
for (int r = 0; r < touch.reportNum && r < 5; r++) {
    int hwId = touch.report[r].id;
    int x = ..., y = ...;

    int slot = -1;
    for (int s = 0; s < 5; s++) if (slotHwId[s] == hwId) { slot = s; break; }
    if (slot == -1) {
        for (int s = 0; s < 5; s++) if (slotHwId[s] == -1) { slot = s; break; }
        if (slot == -1) continue;  // mas de 5 touches simultaneos -- se descarta
        slotHwId[slot] = hwId;
        lastX[slot] = -1; lastY[slot] = -1;  // fuerza un Begin
    }
    seen[slot] = 1;

    if (lastX[slot] == -1) nativeTouchesBegin(jniEnv, NULL, slot, x, y);       // <- slot, no hwId
    else if (lastX[slot] != x || lastY[slot] != y) nativeTouchesMove(jniEnv, NULL, slot, x, y);
    lastX[slot] = x; lastY[slot] = y;
}
for (int s = 0; s < 5; s++) {
    if (slotHwId[s] != -1 && !seen[s]) {
        nativeTouchesEnd(jniEnv, NULL, s, lastX[s], lastY[s]);                 // <- slot, no hwId
        lastX[s] = -1; lastY[s] = -1; slotHwId[s] = -1;
    }
}
```

## No inventar un slot "extra" para touches sintéticos (mapear un botón físico a un touch virtual)

Es común querer simular un touch en la posición de un joystick/botón virtual en pantalla cuando se presiona
un botón físico (D-Pad, gatillos) — por ejemplo, si el juego original solo entiende movimiento por drag
táctil y no tiene ningún manejo de `KeyEvent` para eso (confirmable buscando strings de `"joystick"` o
similar en el `.so` con `strings`/`nm`, sin ese código no hay keycode que lo vaya a mover). La tentación es
darle a ese touch sintético un ID "que seguro no choca" con los reales, tipo `5` (uno más que el rango
0-4) — **ese es el mismo bug de la sección anterior**: cocos2d-x en Android soporta `CC_MAX_TOUCHES == 5`
(slots `0`-`4`), así que `5` ya es uno de más, fuera del array interno del motor, y corrompe el heap igual
que un ID crudo grande.

El touch sintético tiene que competir por los **mismos 5 slots** que los dedos reales, a través del mismo
mecanismo de asignación — dándole un id virtual estable (por ejemplo `-2`, que nunca es un id real de
`SceTouchReport` de 0-255 ni el `-1` de "libre") para que se le asigne un slot 0-4 como a cualquier otro
"dedo":

```c
// se agrega como una entrada mas a la lista de "reportes" de este frame,
// antes de correr el mismo loop de asignacion de slots de arriba
if (padWantsMovimiento) {
    reportHwId[reportCount] = -2;  // id virtual, nunca uno real
    reportX[reportCount] = ...;
    reportY[reportCount] = ...;
    reportCount++;
}
```

Si además el resultado es que el personaje "gira pero no camina" (o alguna acción parcial pero no la
completa), el desplazamiento simulado desde el centro del joystick virtual probablemente cruza su zona
muerta de "girar" pero no la de "moverse" — agrandar la distancia del drag simulado, no cambiar la lógica.

## Mapeo de botones físicos a keycodes de Android

Si el juego original usa `KeyEvent` (D-Pad, botones de acción) además de touch, mapear los botones reales de
la Vita a los keycodes estándar de Android que el motor espera (`KEYCODE_DPAD_LEFT=21`,
`KEYCODE_DPAD_RIGHT=22`, `KEYCODE_BACK=4`, etc.) con flancos de subida/bajada (no mandar `KeyDown` repetido
mientras el botón sigue apretado):

```c
if ((current_pad & SCE_CTRL_LEFT) && !(oldpad & SCE_CTRL_LEFT)) nativeKeyDown(jniEnv, NULL, 21);
if (!(current_pad & SCE_CTRL_LEFT) && (oldpad & SCE_CTRL_LEFT)) nativeKeyUp(jniEnv, NULL, 21);
```
