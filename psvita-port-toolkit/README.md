# PS Vita Port Toolkit (Android / cocos2d-x, arquitectura SoLoader)

Documentación genérica (sin nada específico de un juego puntual), destilada de un port real llevado de cero
a jugable en hardware físico. Pensada para reusar en el próximo port.

## Contenido

- **`PORTING_GUIDE.md`** — guía paso a paso, fase por fase, con un checklist al final. Empezar por acá.
- **`skills/psvita-porting/`** — la Skill de Claude Code con el detalle técnico de cada tema (toolchain,
  carga de `.so`, JNI, input, empaquetado de assets, LiveArea/VPK, debugging en hardware real).
- **`skills/so-crash-triage/`** — la Skill de Claude Code con el procedimiento para diagnosticar un crash
  puntual cruzando el `.so` real (`objdump`), su pseudo-C decompilado (Ghidra) y el Java del APK
  decompilado (`jadx`) — de un log+`.psp2dmp` a la línea exacta de código que hay que corregir.

## Cómo usar esto en un proyecto nuevo

Copiar las carpetas `skills/psvita-porting/` y `skills/so-crash-triage/` a `.claude/skills/` dentro del
repo del nuevo port (o a `~/.claude/skills/` para que estén disponibles en cualquier proyecto). Claude
Code las va a activar solas cuando el trabajo sea de este tipo. Leer `PORTING_GUIDE.md` como mapa general
del proceso.
