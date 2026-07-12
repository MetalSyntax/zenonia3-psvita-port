# Guía de Carga Dinámica y Relocalización (.so) para PS Vita

El cargador nativo de la PS Vita realiza la carga y relocalización de librerías ELF dinámicas compiladas para Android ARM en tiempo de ejecución.

## Estructura de Carga

Cuando un juego utiliza múltiples archivos `.so` (como es el caso de Cocos2d-x con `libcocos2d.so`, `libcocosdenshion.so` y `libgame_logic.so`), deben asignarse diferentes rangos de direcciones base al usar la función `so_file_load` para evitar solapamientos y corrupción de memoria:

```c
// Ejemplo de offsets seguros
#define LOAD_ADDRESS 0x98000000

// Cargar librerías con offset incremental
so_file_load(&denshion_mod, "ux0:data/popclassic/libcocosdenshion.so", LOAD_ADDRESS);
so_file_load(&cocos2d_mod, "ux0:data/popclassic/libcocos2d.so", LOAD_ADDRESS + 0x100000);
so_file_load(&game_mod, "ux0:data/popclassic/libgame_logic.so", LOAD_ADDRESS + 0x800000);
```

## Relocalización y Resolución de Símbolos

Una vez cargada cada librería en la memoria de la consola, se deben relocalizar las referencias y resolver los símbolos enlazándolos con la tabla de llamadas estándar (`default_dynlib`):

```c
so_relocate(&mod);
so_resolve(&mod, default_dynlib, sizeof(default_dynlib), 0);
```

Al final, limpia las cachés de instrucciones y datos del procesador ARM antes de inicializar los módulos:

```c
so_flush_caches(&mod);
so_initialize(&mod);
```
