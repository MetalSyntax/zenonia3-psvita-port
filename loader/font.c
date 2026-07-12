/*
 * font.c -- ver font.h. Rasterizador stb_truetype para el puente GFA.
 *
 * Notas de fidelidad con android.graphics.Paint:
 *  - setTextSize(px) es el tamano EM en pixeles -> stbtt_ScaleForMappingEmToPixels
 *    (NO ScaleForPixelHeight, que usa ascent-descent y da glifos mas chicos).
 *  - Sin kerning (Paint tampoco lo aplica por default en drawText simple).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "../lib/stb/stb_truetype.h"

#include "font.h"

extern void game_log(const char *fmt, ...);

static unsigned char *font_blob = NULL;
static stbtt_fontinfo font_info;
static int font_ok = 0;

int gfa_font_init(const char *path) {
    if (font_ok) return 1;

    FILE *f = fopen(path, "rb");
    if (!f) {
        game_log("[FONT] no se pudo abrir %s\n", path);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    font_blob = (unsigned char *) malloc(size);
    if (!font_blob) { fclose(f); return 0; }
    fread(font_blob, 1, size, f);
    fclose(f);

    if (!stbtt_InitFont(&font_info, font_blob,
                        stbtt_GetFontOffsetForIndex(font_blob, 0))) {
        game_log("[FONT] stbtt_InitFont fallo para %s\n", path);
        free(font_blob);
        font_blob = NULL;
        return 0;
    }
    font_ok = 1;
    game_log("[FONT] %s cargada (%ld bytes)\n", path, size);
    return 1;
}

int gfa_font_ready(void) {
    return font_ok;
}

static float px_scale(float px) {
    return stbtt_ScaleForMappingEmToPixels(&font_info, px);
}

int gfa_font_ascent(float px) {
    if (!font_ok) return (int) px;
    int a, d, lg;
    stbtt_GetFontVMetrics(&font_info, &a, &d, &lg);
    return (int) ceilf(a * px_scale(px));
}

int gfa_font_descent(float px) {
    if (!font_ok) return 0;
    int a, d, lg;
    stbtt_GetFontVMetrics(&font_info, &a, &d, &lg);
    return (int) ceilf(-d * px_scale(px)); // d es negativo en stb
}

float gfa_font_advance(float px, uint32_t cp) {
    if (!font_ok) return px * 0.6f;
    int adv, lsb;
    int g = stbtt_FindGlyphIndex(&font_info, (int) cp);
    if (g == 0 && cp != ' ') {
        // Glifo ausente: caja del ancho de un espacio ideografico aproximado
        stbtt_GetCodepointHMetrics(&font_info, 0x3000, &adv, &lsb);
        if (adv == 0) stbtt_GetCodepointHMetrics(&font_info, 'M', &adv, &lsb);
    } else {
        stbtt_GetGlyphHMetrics(&font_info, g, &adv, &lsb);
    }
    return adv * px_scale(px);
}

float gfa_font_text_width(float px, const uint32_t *cps, int n) {
    float w = 0.0f;
    for (int i = 0; i < n; i++)
        w += gfa_font_advance(px, cps[i]);
    return w;
}

int gfa_font_break_text(float px, const uint32_t *cps, int n, float max_width) {
    float w = 0.0f;
    int i;
    for (i = 0; i < n; i++) {
        w += gfa_font_advance(px, cps[i]);
        if (w > max_width) break;
    }
    return i;
}

float gfa_font_draw_line(float px, const uint32_t *cps, int n,
                         uint32_t *buf, int bw, int bh,
                         float pen_x, float baseline_y, uint32_t color) {
    if (!font_ok || !buf) return 0.0f;

    float scale = px_scale(px);
    float start_x = pen_x;

    uint32_t col_a = (color >> 24) & 0xff;
    uint32_t col_r = (color >> 16) & 0xff;
    uint32_t col_g = (color >> 8) & 0xff;
    uint32_t col_b = color & 0xff;
    if (col_a == 0) col_a = 255; // el motor a veces manda 0x00RRGGBB

    for (int i = 0; i < n; i++) {
        int g = stbtt_FindGlyphIndex(&font_info, (int) cps[i]);
        int adv, lsb;
        if (g != 0) {
            stbtt_GetGlyphHMetrics(&font_info, g, &adv, &lsb);
            int gw, gh, xoff, yoff;
            unsigned char *gray = stbtt_GetGlyphBitmap(&font_info, scale, scale,
                                                       g, &gw, &gh, &xoff, &yoff);
            if (gray) {
                int gx0 = (int) (pen_x + 0.5f) + xoff;
                int gy0 = (int) (baseline_y + 0.5f) + yoff;
                for (int y = 0; y < gh; y++) {
                    int dy = gy0 + y;
                    if (dy < 0 || dy >= bh) continue;
                    for (int x = 0; x < gw; x++) {
                        int dx = gx0 + x;
                        if (dx < 0 || dx >= bw) continue;
                        uint32_t cov = gray[y * gw + x];
                        if (!cov) continue;
                        uint32_t a = cov * col_a / 255;
                        uint32_t old = buf[dy * bw + dx];
                        uint32_t old_a = old >> 24;
                        if (a >= old_a) {
                            // Premultiplicado, igual que un Bitmap ARGB_8888
                            // dibujado con Canvas: el motor usa solo A para el
                            // char cache; RGB queda para el camino de blit
                            // directo (CGxFontAndroid::DrawFont).
                            buf[dy * bw + dx] = (a << 24) |
                                                ((col_r * a / 255) << 16) |
                                                ((col_g * a / 255) << 8) |
                                                (col_b * a / 255);
                        }
                    }
                }
                stbtt_FreeBitmap(gray, NULL);
            }
            pen_x += adv * scale;
        } else {
            pen_x += gfa_font_advance(px, cps[i]);
        }
    }
    return pen_x - start_x;
}
