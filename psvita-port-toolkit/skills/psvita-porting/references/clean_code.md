# Clean Code y Estándares de Código para Ports de PS Vita

Para asegurar la robustez del cargador y evitar fallos de memoria difíciles de rastrear, sigue estas pautas de desarrollo en C:

## 1. Gestión Segura de Memoria
* **Verificación de punteros**: Comprueba siempre el retorno de llamadas a `malloc`, `calloc` y `realloc` antes de utilizarlos.
* **Definición de tamaños de Heap**: Controla la cantidad asignada a `_newlib_heap_size_user` para no agotar la RAM de la consola (256MB máximo para aplicaciones de usuario en modelos tradicionales).
* **Liberación ordenada**: Asegúrate de liberar (`free`) todos los recursos dinámicos al cerrar subprocesos o finalizar el contexto de ejecución.

## 2. Convención y Nombres Claros
* Prefiere nombres descriptivos para variables de control de estados como `is_gpu_initialized` o `is_audio_enabled`.
* Separa los métodos del ciclo de vida de la emulación de Android mediante un prefijo consistente como `android_` o `jni_`.

## 3. Manejo Correcto de Hilos (Threads)
* Ajusta adecuadamente el tamaño de pila (`stacksize`) de los hilos de POSIX (`pthread_attr_setstacksize`). Un tamaño por defecto muy grande puede agotar la memoria disponible rápidamente.
* Enlaza la ejecución del hilo principal del juego usando llamadas nativas de control como `sceKernelExitDeleteThread` al finalizar la inicialización.
