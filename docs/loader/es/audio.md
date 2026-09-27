# `loader/audio.h` — Design Architecture & Notes

Explanatory and architectural design notes extracted from source code and replaced with concise technical Doxygen blocks. This document preserves the reasoning ('why') separated from technical API documentation.

## `audio_init` (line ~4)

**Source File:** `loader/audio.h`

> Reproductor de audio del port, adaptado del de Zenonia 2 (mismo motor) con
> dos diferencias reales de Zenonia 3:
>
> 1. Los .ogg del APK NO son todos 22050 Hz: hay 44100 Hz mono (60) y
> 16000 Hz mono (17). El mezclador corre a 44100 y resamplea (lineal)
> las voces que lo necesiten.
> 2. El despacho replica ZenoniaUIControllerView.OnSoundPlay (jadx):
> - vol == 0 && isLoop  -> stopBGMSound() (comando de "parar musica")
> - sndID 1..15         -> SFX (SoundPool, se superponen)
> - resto               -> BGM (isLoop) o stream one-shot (!isLoop)
> y NexusSound.setVolume(vol/10): mVolume = (vol/10)/10.0f.
>
> El motor pide R.raw.s000 + sndID: los resource ID de Android son consecutivos
> en orden alfabetico de los .ogg EXISTENTES en res/raw, pero los nombres
> originales tienen huecos (falta s010, s019, ...) y sufijos irregulares
> (s116xx.ogg), asi que sndID NO es el numero del nombre de archivo. Antes
> hacia falta renombrar los archivos a mano antes de instalar; ahora
> audio_init() escanea ux0:data/zenonia3/sound/ una vez, ordena los *.ogg
> alfabeticamente y reconstruye esa misma asignacion de IDs directamente
> sobre los nombres originales del APK (verificado: indice ordinal 10 ==
> s011.ogg). Esto permite extraer res/raw/ tal cual a
> ux0:data/zenonia3/sound/ sin ningun paso de renombrado -- una carpeta con
> audio de una version de APK distinta a la que corre el motor (orden
> ordinal desalineado) es la causa clasica de "sin musica"/SFX equivocado
> (ver port_progress.md Fase 5).

---
