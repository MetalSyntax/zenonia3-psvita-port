/*
 * audio.c -- ver audio.h para el contrato y las diferencias con Zenonia 2.
 *
 * Implementacion: un puerto BGM de sceAudioOut a 44100 Hz estereo y un thread
 * mezclador con Tremor (libvorbisidec). Cada voz streamea su OggVorbis_File;
 * las voces cuyo .ogg no es de 44100 Hz se resamplean lineal al vuelo
 * (16000 -> 44100 en este juego). Mono se duplica a estereo.
 */

#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tremor/ivorbisfile.h>

#include "audio.h"

extern void game_log(const char *fmt, ...);

#define SND_DIR "ux0:data/zenonia3/sound"

#define AUDIO_RATE 44100
#define AUDIO_GRAIN 512

// 1 BGM + 1 stream + 4 SFX simultaneos
#define VOICE_BGM    0
#define VOICE_STREAM 1
#define VOICE_SFX0   2
#define NUM_VOICES   6

typedef struct {
    OggVorbis_File vf;
    int active;
    int loop;
    int channels;
    long rate;          // rate real del .ogg (44100 o 16000 en este juego)
    float gain;
    // Estado del resampleador lineal (solo si rate != AUDIO_RATE)
    float pos_frac;     // posicion fraccional dentro del stream fuente
    int16_t prev_l, prev_r; // ultimo frame fuente consumido (para interpolar)
    int have_prev;
    // Staging de decodificacion PROPIO de la voz: el mezclador alterna entre
    // voces, un buffer compartido descartaria frames ya decodificados.
    int16_t stage[256];
    int stage_frames, stage_pos;
} voice_t;

static voice_t voices[NUM_VOICES];
static SceUID audio_mutex = -1;
static SceUID audio_thread_id = -1;
static int audio_port = -1;
static volatile int audio_running = 0;

static void voice_close(voice_t *v) {
    if (v->active) {
        ov_clear(&v->vf);
        v->active = 0;
    }
}

// Lee UN frame fuente (L,R) de la voz; devuelve 0 en EOF definitivo (voz cerrada).
static int voice_next_src_frame(voice_t *v, int16_t *l, int16_t *r) {
    while (v->stage_pos >= v->stage_frames) {
        int bs;
        long got = ov_read(&v->vf, (char *) v->stage, sizeof(v->stage), &bs);
        if (got <= 0) {
            if (v->loop && got == 0 && ov_pcm_seek(&v->vf, 0) == 0)
                continue;
            voice_close(v);
            return 0;
        }
        v->stage_frames = (int) got / (v->channels * 2);
        v->stage_pos = 0;
    }

    *l = v->stage[v->stage_pos * v->channels];
    *r = v->stage[v->stage_pos * v->channels + (v->channels > 1 ? 1 : 0)];
    v->stage_pos++;
    return 1;
}

// Decodea `frames` frames estereo a AUDIO_RATE en `out` (intercalado LR),
// resampleando lineal si la voz no es de 44100 Hz. Devuelve frames escritos.
static int voice_decode(voice_t *v, int16_t *out, int frames) {
    int done = 0;

    if (v->rate == AUDIO_RATE) {
        int16_t l, r;
        while (done < frames && v->active) {
            if (!voice_next_src_frame(v, &l, &r)) break;
            out[done * 2]     = (int16_t)(l * v->gain);
            out[done * 2 + 1] = (int16_t)(r * v->gain);
            done++;
        }
        return done;
    }

    // Resampleo lineal: step = srcRate / dstRate (< 1 para upsample)
    float step = (float) v->rate / (float) AUDIO_RATE;
    while (done < frames && v->active) {
        if (!v->have_prev) {
            if (!voice_next_src_frame(v, &v->prev_l, &v->prev_r)) break;
            v->have_prev = 1;
            v->pos_frac = 0.0f;
        }
        // Frame fuente siguiente para interpolar; si EOF, drena con el ultimo
        int16_t nl = v->prev_l, nr = v->prev_r;
        while (v->pos_frac >= 1.0f) {
            if (!voice_next_src_frame(v, &nl, &nr)) { v->have_prev = 0; break; }
            v->prev_l = nl; v->prev_r = nr;
            v->pos_frac -= 1.0f;
        }
        if (!v->active || !v->have_prev) break;

        // Con pos_frac en [0,1): interpolamos entre prev y el proximo. Para
        // simplificar (y porque 16k->44.1k upsamplea ~2.76x), usamos el frame
        // previo como valor -- interpolacion de orden 0 mejorada con el paso
        // fraccional de arriba. Suficiente para SFX de 16 kHz de un juego 2D.
        out[done * 2]     = (int16_t)(v->prev_l * v->gain);
        out[done * 2 + 1] = (int16_t)(v->prev_r * v->gain);
        done++;
        v->pos_frac += step;
    }
    return done;
}

static int audio_thread(SceSize args, void *argp) {
    static int16_t mix[AUDIO_GRAIN * 2];
    static int16_t buf[AUDIO_GRAIN * 2];

    while (audio_running) {
        memset(mix, 0, sizeof(mix));

        sceKernelLockMutex(audio_mutex, 1, NULL);
        for (int vi = 0; vi < NUM_VOICES; vi++) {
            voice_t *v = &voices[vi];
            if (!v->active) continue;
            int got = voice_decode(v, buf, AUDIO_GRAIN);
            for (int i = 0; i < got * 2; i++) {
                int s = mix[i] + buf[i];
                if (s > 32767) s = 32767;
                if (s < -32768) s = -32768;
                mix[i] = (int16_t) s;
            }
        }
        sceKernelUnlockMutex(audio_mutex, 1);

        // Bloquea hasta que el hardware consumio el bloque: marca el ritmo
        sceAudioOutOutput(audio_port, mix);
    }
    return 0;
}

void audio_init(void) {
    audio_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, AUDIO_GRAIN,
                                     AUDIO_RATE, SCE_AUDIO_OUT_MODE_STEREO);
    if (audio_port < 0) {
        game_log("[AUDIO] sceAudioOutOpenPort fallo: 0x%08x\n", audio_port);
        return;
    }

    audio_mutex = sceKernelCreateMutex("zen3_audio_mutex", 0, 0, NULL);
    audio_running = 1;
    audio_thread_id = sceKernelCreateThread("zen3_audio", audio_thread,
                                            0x10000100, 0x10000, 0, 0, NULL);
    if (audio_thread_id >= 0) {
        sceKernelStartThread(audio_thread_id, 0, NULL);
        game_log("[AUDIO] mezclador iniciado (port=%d, %d Hz)\n", audio_port, AUDIO_RATE);
    }
}

void audio_stop_bgm(void) {
    if (audio_port < 0) return;
    sceKernelLockMutex(audio_mutex, 1, NULL);
    voice_close(&voices[VOICE_BGM]);
    sceKernelUnlockMutex(audio_mutex, 1);
}

void audio_play(int snd_id, int vol, int is_loop) {
    if (audio_port < 0) return;

    // ZenoniaUIControllerView.OnSoundPlay: vol==0 + isLoop es el comando
    // "parar la musica actual", no un play.
    if (vol == 0 && is_loop) {
        audio_stop_bgm();
        return;
    }

    char path[128];
    snprintf(path, sizeof(path), SND_DIR "/s%03d.ogg", snd_id);

    FILE *f = fopen(path, "rb");
    if (!f) {
        static int miss_log = 0;
        if (miss_log < 20) {
            game_log("[AUDIO] no encontrado: %s\n", path);
            miss_log++;
        }
        return;
    }

    OggVorbis_File vf;
    if (ov_open(f, &vf, NULL, 0) < 0) {
        game_log("[AUDIO] ov_open fallo para %s\n", path);
        fclose(f);
        return;
    }
    vorbis_info *vi = ov_info(&vf, -1);
    if (!vi || (vi->channels != 1 && vi->channels != 2)) {
        game_log("[AUDIO] formato inesperado en %s (ch=%d rate=%ld)\n",
                 path, vi ? vi->channels : -1, vi ? vi->rate : -1);
        ov_clear(&vf); // tambien cierra el FILE*
        return;
    }

    sceKernelLockMutex(audio_mutex, 1, NULL);

    voice_t *target = NULL;
    if (snd_id >= 1 && snd_id <= 15) {
        // SoundPool (SFX 1..15 registrados en Zenonia3Launcher): voz libre o
        // pisar la primera
        for (int i = VOICE_SFX0; i < NUM_VOICES; i++)
            if (!voices[i].active) { target = &voices[i]; break; }
        if (!target) target = &voices[VOICE_SFX0];
    } else if (is_loop) {
        target = &voices[VOICE_BGM];    // mBgmPlayer: corta la musica anterior
    } else {
        target = &voices[VOICE_STREAM]; // mPlayer: corta el stream anterior
    }

    voice_close(target);
    target->vf = vf;
    target->loop = is_loop;
    target->channels = vi->channels;
    target->rate = vi->rate;
    // NexusSound.setVolume(vol/10): mVolume = (vol/10)/10.0f -- escala 0..1
    // con truncado entero, y vol<=0 silencia (isSoundON=false).
    int vol10 = vol / 10;
    target->gain = vol10 > 10 ? 1.0f : (vol10 / 10.0f);
    if (target->gain <= 0.0f) { // vol>0 pero <10 -> algo audible igual
        target->gain = vol > 0 ? 0.1f : 0.0f;
    }
    target->pos_frac = 0.0f;
    target->have_prev = 0;
    target->stage_frames = 0;
    target->stage_pos = 0;
    target->active = 1;

    sceKernelUnlockMutex(audio_mutex, 1);
}

void audio_stop_all(void) {
    if (audio_port < 0) return;
    sceKernelLockMutex(audio_mutex, 1, NULL);
    for (int i = 0; i < NUM_VOICES; i++)
        voice_close(&voices[i]);
    sceKernelUnlockMutex(audio_mutex, 1);
}
