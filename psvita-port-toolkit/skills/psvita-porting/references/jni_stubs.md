# Stubs de JNI: cuándo un no-op alcanza y cuándo hace falta más

Un soloader típico resuelve `GetMethodID`/`GetStaticMethodID` contra una tabla plana de nombres (sin
distinguir clase ni "static" de "instance" — ambos lookups suelen usar la misma tabla). Cuando el motor pide
un método que no está en la tabla, normalmente **no es fatal por sí solo**: el wrapper de JNI del propio
`.so` (`JniHelper` o similar) suele loguear un warning ("Failed to find [static] method id of X") y seguir,
tratándolo como si la llamada no hiciera nada.

## El peligro: no todo lo que "no está implementado" es seguro de ignorar

Si el motor **espera un callback de vuelta** después de esa llamada (el patrón típico de Android: llamar a
Java para que haga algo asíncrono — reproducir un video, un sonido, mostrar un diálogo — y Java le avisa a
native cuando termina), un stub vacío deja al motor esperando ese callback **para siempre**. Esto no crashea
— **cuelga el juego sin responder a nada**, que es mucho más difícil de diagnosticar que un crash porque no
deja ningún rastro en el log más allá de dónde se detuvo.

Síntoma típico en el log: la última línea es justo la que dispara la acción (`"Playing Video"`,
`"Loading..."`, etc.), después nada más — ni error, ni continuación. Si además la consola queda totalmente
sin responder a input (no solo esa pantalla en particular), es una señal fuerte de este patrón, no de un
crash.

## Patrón correcto: buscar y disparar el callback de "completado" real del motor

Antes de asumir que un no-op alcanza, buscá con `nm -D` (sobre el `.so` real del juego) si existe un método
exportado que suene a "on\*Completed"/"on\*Finished" para esa misma feature — casi siempre existe, porque es
la mitad de vuelta del mismo mecanismo que el motor ya usa en Android real:

```bash
arm-vita-eabi-nm -D --defined-only libcocos2d.so | c++filt | grep -i video
# -> Java_org_cocos2dx_lib_Cocos2dxVideo_onVideoCompleted
```

Y el stub, en vez de no hacer nada, resuelve y llama a ese callback inmediatamente (simulando que la acción
terminó al instante):

```c
void Stub_playVideo(jmethodID id, va_list args) {
    void (* onCompleted)(JNIEnv *env, jobject thiz) =
        (void *) so_symbol(&engine_mod, "Java_org_cocos2dx_lib_Cocos2dxVideo_onVideoCompleted");
    if (onCompleted) onCompleted(&jni, NULL);
}
```

Esto aplica al mismo patrón para cualquier feature "reproducir algo y avisar cuando termine" que no tenga
sentido implementar de verdad en el port (video, animaciones nativas de Android, diálogos del sistema) — la
señal de que hace falta este patrón (no solo un no-op) es que el motor se cuelga esperando, no que crashea.

## Verificar el registro, no solo el nombre del método

Un método puede estar implementado en el código C pero no aparecer en la tabla que mapea nombre → ID
(`nameToMethodId` o equivalente) — y viceversa, estar en la tabla de nombres pero no en la tabla de
implementaciones por tipo de retorno (`MethodsVoid`, `MethodsObject`, etc., según el diseño del soloader).
Si un stub "no hace nada" a pesar de estar bien escrito, confirmar ambas tablas antes de seguir buscando en
otro lado.
