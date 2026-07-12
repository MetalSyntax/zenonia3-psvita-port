#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <vitaGL.h>

#include "htmlview.h"
#include "htmltext.h"
#include "font.h"

extern void game_log(const char *fmt, ...);

#define HTML_RAW_CAP   (64 * 1024)
#define HTML_TEXT_CAP  (16 * 1024)
#define MAX_DISPLAY_LINES 512
#define MAX_TEX_H 4096
#define PANEL_PAD_X 10.0f

struct htmlview {
    GLuint tex;
    int tex_w, tex_h;
    float content_h; // alto real usado (<= tex_h)
    float scroll_y;
};

// Copia bytes ASCII (ya garantizado por htmltext_extract) a codepoints -- el
// font.h de este puente trabaja con arrays de uint32_t, no con UTF-8.
static void ascii_to_cps(const char *s, int n, uint32_t *cps) {
    for (int i = 0; i < n; i++) cps[i] = (uint32_t)(unsigned char) s[i];
}

// Colores reales de la hoja de estilos del HTML original (about.html/
// help_eng.html comparten el mismo <style>): .style3 (encabezado de seccion,
// bold + regla arriba/abajo) = #00b0f0; .style2b (etiqueta, bold) = #FD9E35;
// .style2 (cuerpo) = #ffffff (se uso blanco puro en vez de #999999 real --
// mas legible sobre el fondo oscuro real de la Vita que sobre el gris claro
// que asumia el CSS original).
#define COLOR_BODY   0xFFFFFFFFu
#define COLOR_HEADER 0xFF00B0F0u
#define COLOR_LABEL  0xFFFD9E35u

typedef enum { LINE_STYLE_BODY = 0, LINE_STYLE_HEADER, LINE_STYLE_LABEL } line_style;

static uint32_t style_color(line_style st) {
    switch (st) {
        case LINE_STYLE_HEADER: return COLOR_HEADER;
        case LINE_STYLE_LABEL:  return COLOR_LABEL;
        default:                return COLOR_BODY;
    }
}

// Rellena una franja horizontal solida y opaca (para la regla arriba/abajo de
// un encabezado, ver htmltext.h: HTMLTEXT_STYLE_HEADER) directo en el buffer
// ARGB premultiplicado (alpha=255 -> RGB = color tal cual, ver font.c).
static void draw_hrule(uint32_t *buf, int bw, int bh, int y, float x0, float x1, uint32_t color) {
    if (y < 0 || y >= bh) return;
    int ix0 = (int) x0; if (ix0 < 0) ix0 = 0;
    int ix1 = (int) x1; if (ix1 > bw) ix1 = bw;
    uint32_t px = 0xFF000000u | (color & 0x00FFFFFFu);
    for (int x = ix0; x < ix1; x++) buf[y * bw + x] = px;
}

// Wrapea UNA linea logica (ya sin '\n') en 1+ lineas de display usando
// gfa_font_break_text (Paint.breakText real, ver font.h). Devuelve la
// cantidad de lineas agregadas a out[] (hasta out_cap).
static int wrap_line(const char *line, int line_len, float font_px, float max_w,
                      const char **out_ptrs, int *out_lens, int out_cap) {
    if (line_len == 0) {
        if (out_cap > 0) { out_ptrs[0] = line; out_lens[0] = 0; return 1; }
        return 0;
    }

    uint32_t cps[1024];
    int n = line_len > 1024 ? 1024 : line_len;
    ascii_to_cps(line, n, cps);

    int produced = 0;
    int offset = 0;
    while (offset < n && produced < out_cap) {
        int remaining = n - offset;
        int fit = gfa_font_break_text(font_px, cps + offset, remaining, max_w);
        if (fit <= 0) fit = 1; // una palabra mas ancha que el panel: forzar progreso
        // No partir una palabra a la mitad si hay un espacio mas atras (busca
        // el ultimo espacio dentro de lo que entra, salvo que sea una sola
        // palabra larga sin espacios).
        if (fit < remaining) {
            int back = fit;
            while (back > 0 && line[offset + back - 1] != ' ') back--;
            if (back > 0) fit = back;
        }
        out_ptrs[produced] = line + offset;
        out_lens[produced] = fit;
        produced++;
        offset += fit;
        while (offset < n && line[offset] == ' ') offset++; // no arrancar la siguiente con espacio
    }
    return produced;
}

static htmlview *upload_texture(uint32_t *buf, int bw, int bh, float content_h) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bw, bh, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // CLAMP_TO_EDGE: con GL_REPEAT (default), el filtro bilineal en los bordes
    // del scroll (arriba/abajo del todo) mezclaria con el otro extremo de la
    // textura (wrap-around) -- un artefacto de 1 texel visible al scrollear
    // al limite.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    htmlview *v = malloc(sizeof(htmlview));
    if (!v) return NULL;
    v->tex = tex;
    v->tex_w = bw;
    v->tex_h = bh;
    v->content_h = content_h;
    v->scroll_y = 0.0f;
    return v;
}

htmlview *htmlview_load(const char *name, float panel_w_px, float font_px) {
    gfa_font_init("app0:font.ttf"); // idempotente -- ver font.h
    if (!gfa_font_ready()) {
        game_log("[HtmlView] font no disponible, no se puede renderizar %s\n", name);
        return NULL;
    }

    char path[128];
    snprintf(path, sizeof(path), "app0:html/%s.html", name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        game_log("[HtmlView] %s no encontrado\n", path);
        return NULL;
    }
    char *raw = malloc(HTML_RAW_CAP);
    size_t raw_len = raw ? fread(raw, 1, HTML_RAW_CAP - 1, f) : 0;
    fclose(f);
    if (!raw) return NULL;
    raw[raw_len] = '\0';

    char *text = malloc(HTML_TEXT_CAP);
    if (!text) { free(raw); return NULL; }
    htmltext_extract(raw, text, HTML_TEXT_CAP);
    free(raw);

    float max_w = panel_w_px - 2.0f * PANEL_PAD_X;

    // Lineas de display: array de (puntero, largo, estilo) apuntando DENTRO
    // de text[] -- no se copian, se leen directo al rasterizar.
    const char **line_ptrs = malloc(sizeof(char *) * MAX_DISPLAY_LINES);
    int *line_lens = malloc(sizeof(int) * MAX_DISPLAY_LINES);
    line_style *line_styles = malloc(sizeof(line_style) * MAX_DISPLAY_LINES);
    if (!line_ptrs || !line_lens || !line_styles) {
        free(text); free((void *) line_ptrs); free(line_lens); free(line_styles);
        return NULL;
    }
    int num_lines = 0;

    char *p = text;
    while (*p && num_lines < MAX_DISPLAY_LINES) {
        line_style st = LINE_STYLE_BODY;
        if (*p == HTMLTEXT_STYLE_HEADER) { st = LINE_STYLE_HEADER; p++; }
        else if (*p == HTMLTEXT_STYLE_LABEL) { st = LINE_STYLE_LABEL; p++; }

        char *nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int) strlen(p);
        int added = wrap_line(p, len, font_px, max_w, line_ptrs + num_lines, line_lens + num_lines,
                               MAX_DISPLAY_LINES - num_lines);
        for (int i = 0; i < added; i++) line_styles[num_lines + i] = st;
        num_lines += added;
        if (!nl) break;
        p = nl + 1;
    }
    if (*p) {
        game_log("[HtmlView] %s: contenido recortado a %d lineas (no entraba completo)\n", name, num_lines);
    }

    int ascent = gfa_font_ascent(font_px);
    int descent = gfa_font_descent(font_px);
    int line_h = ascent + descent + 4;
    if (line_h < 1) line_h = (int) (font_px * 1.3f) + 4;

    int bw = (int) panel_w_px;
    int wanted_h = num_lines * line_h;
    int bh = wanted_h > MAX_TEX_H ? MAX_TEX_H : wanted_h;
    if (bh < 1) bh = 1;
    if (wanted_h > MAX_TEX_H) {
        game_log("[HtmlView] %s: alto de contenido %d recortado a %d (limite de textura)\n", name, wanted_h, MAX_TEX_H);
    }

    uint32_t *buf = calloc((size_t) bw * (size_t) bh, sizeof(uint32_t));
    if (!buf) {
        free(text); free((void *) line_ptrs); free(line_lens);
        return NULL;
    }

    uint32_t cps[1024];
    int y = 0;
    for (int i = 0; i < num_lines && y + line_h <= bh; i++, y += line_h) {
        if (line_styles[i] == LINE_STYLE_HEADER) {
            // Regla horizontal real del CSS (.style3: border-top/bottom
            // solid #00b0f0) -- una arriba y otra abajo del renglon.
            draw_hrule(buf, bw, bh, y + 1, 0.0f, (float) bw, COLOR_HEADER);
            draw_hrule(buf, bw, bh, y + line_h - 2, 0.0f, (float) bw, COLOR_HEADER);
        }
        if (line_lens[i] <= 0) continue;
        int n = line_lens[i] > 1024 ? 1024 : line_lens[i];
        ascii_to_cps(line_ptrs[i], n, cps);
        gfa_font_draw_line(font_px, cps, n, buf, bw, bh,
                            PANEL_PAD_X, (float) (y + ascent), style_color(line_styles[i]));
    }

    free(text);
    free((void *) line_ptrs);
    free(line_lens);
    free(line_styles);

    htmlview *v = upload_texture(buf, bw, bh, (float) (num_lines * line_h));
    free(buf);
    if (v) {
        game_log("[HtmlView] %s cargado: %d lineas, textura %dx%d (contenido %.0fpx)\n",
                 name, num_lines, bw, bh, v->content_h);
    }
    return v;
}

htmlview *htmlview_make_label(const char *text, float font_px, uint32_t color) {
    gfa_font_init("app0:font.ttf");
    if (!gfa_font_ready() || !text || !*text) return NULL;

    int len = (int) strlen(text);
    uint32_t cps[1024];
    int n = len > 1024 ? 1024 : len;
    ascii_to_cps(text, n, cps);

    int ascent = gfa_font_ascent(font_px);
    int descent = gfa_font_descent(font_px);
    int bh = ascent + descent + 2;
    if (bh < 1) bh = (int) (font_px * 1.3f);
    int bw = (int) gfa_font_text_width(font_px, cps, n) + 4;
    if (bw < 1) bw = 1;

    uint32_t *buf = calloc((size_t) bw * (size_t) bh, sizeof(uint32_t));
    if (!buf) return NULL;
    gfa_font_draw_line(font_px, cps, n, buf, bw, bh, 2.0f, (float) ascent, color);

    htmlview *v = upload_texture(buf, bw, bh, (float) bh);
    free(buf);
    return v;
}

void htmlview_draw(htmlview *v, float x, float y, float w, float h) {
    if (!v || !v->tex) return;

    float max_scroll = v->content_h - h;
    if (max_scroll < 0) max_scroll = 0;
    if (v->scroll_y > max_scroll) v->scroll_y = max_scroll;
    if (v->scroll_y < 0) v->scroll_y = 0;

    float v0 = v->scroll_y / (float) v->tex_h;
    float v1 = (v->scroll_y + h) / (float) v->tex_h;
    if (v1 > 1.0f) v1 = 1.0f;

    glBindTexture(GL_TEXTURE_2D, v->tex);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

    // gfa_font_draw_line() escribe alpha PREMULTIPLICADO en RGB (ver font.c) --
    // blendear con GL_ONE (no GL_SRC_ALPHA) para no aplicar el alpha 2 veces
    // sobre los bordes antialiaseados. Restaurado por el caller (androidui.c)
    // despues de esta llamada.
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    const float verts[] = { x, y,  x + w, y,  x, y + h,  x + w, y + h };
    const float uvs[]   = { 0, v0,  1, v0,     0, v1,      1, v1 };
    glVertexPointer(2, GL_FLOAT, 0, verts);
    glTexCoordPointer(2, GL_FLOAT, 0, uvs);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void htmlview_scroll(htmlview *v, float delta_px, float viewport_h) {
    if (!v) return;
    float max_scroll = v->content_h - viewport_h;
    if (max_scroll < 0) max_scroll = 0;
    v->scroll_y += delta_px;
    if (v->scroll_y < 0) v->scroll_y = 0;
    if (v->scroll_y > max_scroll) v->scroll_y = max_scroll;
}

void htmlview_native_size(htmlview *v, float *w, float *h) {
    *w = v ? (float) v->tex_w : 0.0f;
    *h = v ? (float) v->tex_h : 0.0f;
}
