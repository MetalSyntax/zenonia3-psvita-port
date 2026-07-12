# Validación Estática, Linting y Pruebas Estructurales

Es fundamental garantizar que todas las interfaces simuladas de JNI y variables de entorno del cargador se comporten de manera predecible.

## 1. Validación de Firmas JNI (Linting Conceptual)
* Verifica que las firmas de tipos pasadas a `GetMethodID` o `GetFieldID` coincidan con la especificación Java de Android. Una firma mal formada (por ejemplo, omitir un `;` en una clase de objeto) causará fallas silenciosas en la inicialización:
  * **Correcto**: `(Ljava/lang/String;)V`
  * **Incorrecto**: `(Ljava/lang/String)V`

## 2. Pruebas Unitarias Estructurales
Dado que no existe un entorno de pruebas nativo en el dispositivo de manera sencilla, puedes construir un arnés de pruebas (Mock Test Harness) para verificar de forma estática en tu ordenador:
* Valida la lectura de la tabla de relocalización del `.so` antes de empaquetar el VPK.
* Asegura que los stubs de funciones JNI retornen valores dummy consistentes (como `0`, `NULL` o instancias válidas del falso entorno JNI) para evitar que el motor de Cocos2d-x aborte por excepciones no controladas.
