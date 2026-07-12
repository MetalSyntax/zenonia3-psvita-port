#ifndef __FONT_H__
#define __FONT_H__

#include <stdint.h>

// Backend de rasterizado de fuente para el puente GFA (NexusFont.java) --
// reemplaza a android.graphics.Paint/Canvas/Bitmap con stb_truetype sobre
// app0:font.ttf (NanumGothic, OFL: cobertura Hangul completa + Latin, el
// juego usa strings coreanos via GFA_SetString/SetStringFromKSC5601).
//
// Convenciones (calcadas de NexusFont.java, que es la fuente de verdad):
//  - "px" es Paint.setTextSize (tamano EM en pixeles).
//  - ascent/descent son POSITIVOS y con ceil (GFA_GetAscent = -ceil(ascent()),
//    GFA_GetDescent = ceil(descent())).
//  - El buffer de pixeles es ARGB de 32 bits; el motor consume SOLO el canal
//    alfa (byte >>24) para el cache de glifos (CopyPixelsToCharCacheBuffer,
//    out_ghidra.c:153389) con stride = ancho del bitmap de GFA_Init.

int gfa_font_init(const char *path); // 1 si cargo bien (idempotente)
int gfa_font_ready(void);

int gfa_font_ascent(float px);
int gfa_font_descent(float px);

// Ancho de avance de un codepoint / de un string de codepoints
float gfa_font_advance(float px, uint32_t cp);
float gfa_font_text_width(float px, const uint32_t *cps, int n);

// Paint.breakText(text, true, maxWidth, null): cantidad de caracteres desde
// el inicio cuyo avance acumulado entra en maxWidth.
int gfa_font_break_text(float px, const uint32_t *cps, int n, float max_width);

// Dibuja una linea de texto en buf (bw*bh pixeles ARGB), pen inicial en
// pen_x, baseline en baseline_y. color es ARGB no premultiplicado (el alfa
// del color escala la cobertura, igual que Paint.setColor). Devuelve el
// ancho avanzado en px.
float gfa_font_draw_line(float px, const uint32_t *cps, int n,
                         uint32_t *buf, int bw, int bh,
                         float pen_x, float baseline_y, uint32_t color);

#endif
