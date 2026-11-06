/**
 * @file  vitamenu.c
 * @brief In-game "PS Vita options" menu, opened with Start + Select (main.c).
 *
 * Same scheme as the Carnivores port: Cross replaces the buttons of the
 * highlighted action, Square adds one more, Triangle unbinds it; a button can
 * only belong to one action. Circle (or Start) saves and closes. While it is
 * open main.c stops calling NativeRender(), so the game is paused underneath.
 *
 * Text is rasterized with the GFA font backend (font.c) into one RGBA texture,
 * redrawn only when something on the menu changes.
 */

#include "vitamenu.h"
#include "config.h"
#include "font.h"

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vitaGL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { ROW_ACTION, ROW_FPS, ROW_VBUTTONS, ROW_DEFAULTS, ROW_CLOSE } row_kind;

#define ROW_COUNT (ACT_COUNT + 4)

#define PANEL_X 80
#define PANEL_Y 24
#define PANEL_W 800
#define PANEL_H 496
#define FONT_PX 22.0f
#define ROW_H   30
#define LIST_Y  64

#define REPEAT_DELAY 400000
#define REPEAT_RATE  110000

/* gfa_font_draw_line() writes 0xAARRGGBB words, read back by GL_RGBA as bytes B,G,R,A:
 * pass blue in the "red" byte so the colour on screen is the one named here. */
#define TEXT_RGB(r, g, b) (0xFF000000u | ((uint32_t) (b) << 16) | ((uint32_t) (g) << 8) | (uint32_t) (r))
#define COL_WHITE  TEXT_RGB(255, 255, 255)
#define COL_GREY   TEXT_RGB(180, 180, 180)
#define COL_ACCENT TEXT_RGB(255, 200, 0)

/* Same palette as the Carnivores port's menu (vita_menu.c there). */
#define RGBA_DIM    0.0f, 0.0f, 0.0f, 0.69f
#define RGBA_PANEL  0.118f, 0.102f, 0.078f, 0.94f
#define RGBA_BORDER 0.44f, 0.376f, 0.29f, 1.0f
#define RGBA_SEL    1.0f, 0.784f, 0.0f, 0.31f

#define TITLE_LINE_Y 50                    /* separator under the title */
#define FOOTER_LINE_Y (PANEL_H - 34)       /* separator above the key hints */
#define OPTIONS_Y (LIST_Y + ACT_COUNT * ROW_H) /* separator between bindings and options */

extern GLint g_engine_fb_tex;
extern int g_engine_fb_tex_w, g_engine_fb_tex_h;

static int open, sel, capture; /* capture: 0 off, 1 replace, 2 add */
static uint32_t prev_dirs;
static SceUInt64 repeat_at;

static GLuint text_tex;
static uint32_t *text_buf;
static int dirty = 1;

static row_kind kind_of(int r) {
    return r < ACT_COUNT ? ROW_ACTION : (row_kind) (ROW_FPS + (r - ACT_COUNT));
}

int vitamenu_active(void) { return open; }

void vitamenu_open(void) {
    open = 1;
    capture = 0;
    dirty = 1;
    prev_dirs = SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT;
}

void vitamenu_close(void) {
    config_save();
    open = 0;
    capture = 0;
}

void vitamenu_update(uint32_t held, uint32_t pressed) {
    if (!open) return;

    if (capture) {
        if (pressed & SCE_CTRL_START) {
            capture = 0;
            dirty = 1;
            return;
        }
        uint32_t b = pressed & config_bindable_buttons();
        if (b) {
            b &= -b; /* one button per press */
            config_bind(sel, b, capture == 2);
            capture = 0;
            dirty = 1;
        }
        return;
    }

    if (pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_START)) {
        vitamenu_close();
        return;
    }

    uint32_t dirs = held & (SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT);
    uint32_t fresh = dirs & ~prev_dirs;
    prev_dirs = dirs;
    SceUInt64 now = sceKernelGetProcessTimeWide();
    uint32_t step = 0;
    if (fresh) {
        step = fresh;
        repeat_at = now + REPEAT_DELAY;
    } else if (dirs && now >= repeat_at) {
        step = dirs;
        repeat_at = now + REPEAT_RATE;
    }

    if (step & SCE_CTRL_UP)   { sel = (sel + ROW_COUNT - 1) % ROW_COUNT; dirty = 1; }
    if (step & SCE_CTRL_DOWN) { sel = (sel + 1) % ROW_COUNT; dirty = 1; }

    int toggle = (fresh & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT)) || (pressed & SCE_CTRL_CROSS);
    switch (kind_of(sel)) {
    case ROW_ACTION:
        if (pressed & SCE_CTRL_CROSS)         capture = 1;
        else if (pressed & SCE_CTRL_SQUARE)   capture = 2;
        else if (pressed & SCE_CTRL_TRIANGLE) cfg_binds[sel] = 0;
        break;
    case ROW_FPS:
        if (toggle) cfg_fps = cfg_fps == 30 ? 22 : 30;
        break;
    case ROW_VBUTTONS:
        if (toggle) cfg_vbuttons_opacity = cfg_vbuttons_opacity ? 0 : 100;
        break;
    case ROW_DEFAULTS:
        if (pressed & SCE_CTRL_CROSS) config_defaults();
        break;
    case ROW_CLOSE:
        if (pressed & SCE_CTRL_CROSS) vitamenu_close();
        break;
    }
    if (pressed) dirty = 1;
}

/* --- drawing -------------------------------------------------------------- */

static void text(const char *s, float x, float baseline, uint32_t color) {
    uint32_t cps[128];
    int n = 0;
    for (; s[n] && n < 128; n++) cps[n] = (unsigned char) s[n];
    gfa_font_draw_line(FONT_PX, cps, n, text_buf, PANEL_W, PANEL_H, x, baseline, color);
}

static void text_centered(const char *s, float px, float baseline, uint32_t color) {
    uint32_t cps[128];
    int n = 0;
    for (; s[n] && n < 128; n++) cps[n] = (unsigned char) s[n];
    float w = gfa_font_text_width(px, cps, n);
    gfa_font_draw_line(px, cps, n, text_buf, PANEL_W, PANEL_H, (PANEL_W - w) / 2.0f, baseline, color);
}

static const char *footer(void) {
    if (capture) return "Press the new button     START: cancel";
    switch (kind_of(sel)) {
    case ROW_ACTION:   return "Up/Down: move   Cross: set   Square: add   Triangle: clear   Circle: save";
    case ROW_FPS:
    case ROW_VBUTTONS: return "Up/Down: move   Cross / Left / Right: change   Circle: save and close";
    default:           return "Up/Down: move   Cross: select   Circle: save and close";
    }
}

static void render_text(void) {
    memset(text_buf, 0, PANEL_W * PANEL_H * sizeof(uint32_t));
    float base = (ROW_H + gfa_font_ascent(FONT_PX) - gfa_font_descent(FONT_PX)) / 2.0f;

    text_centered("PS Vita options", 26.0f, 36.0f, COL_ACCENT);
    for (int r = 0; r < ROW_COUNT; r++) {
        float y = LIST_Y + r * ROW_H + base;
        uint32_t col = r == sel ? COL_WHITE : COL_GREY;
        char value[96] = "";
        const char *label = "";
        switch (kind_of(r)) {
        case ROW_ACTION:
            label = action_label(r);
            if (capture && r == sel)
                snprintf(value, sizeof(value), "%s", capture == 2 ? "add: press a button..." : "press a button...");
            else
                config_buttons_text(cfg_binds[r], value, sizeof(value));
            break;
        case ROW_FPS:
            label = "Frame rate";
            snprintf(value, sizeof(value), cfg_fps == 22 ? "< 22 fps (original) >" : "< 30 fps >");
            break;
        case ROW_VBUTTONS:
            label = "On-screen buttons";
            snprintf(value, sizeof(value), cfg_vbuttons_opacity ? "< Visible (100%%) >" : "< Hidden (0%%) >");
            break;
        case ROW_DEFAULTS: label = "Restore defaults"; break;
        case ROW_CLOSE:    label = "Save and close";   break;
        }
        if (r == sel) text(">", 14.0f, y, COL_ACCENT);
        text(label, 32.0f, y, col);
        text(value, PANEL_W * 0.5f, y, r == sel ? COL_ACCENT : COL_WHITE);
    }
    text_centered(footer(), 17.0f, PANEL_H - 12.0f, COL_GREY); /* smaller: the longest hint must fit 800px */

    if (!text_tex) glGenTextures(1, &text_tex);
    glBindTexture(GL_TEXTURE_2D, text_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, PANEL_W, PANEL_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, text_buf);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

static void quad_uv(float x, float y, float w, float h, float u1, float v1) {
    const float verts[] = { x, y,  x + w, y,  x, y + h,  x + w, y + h };
    const float uvs[]   = { 0, 0,  u1, 0,     0, v1,     u1, v1 };
    glVertexPointer(2, GL_FLOAT, 0, verts);
    glTexCoordPointer(2, GL_FLOAT, 0, uvs);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void quad(float x, float y, float w, float h) { quad_uv(x, y, w, h, 1.0f, 1.0f); }

static void rect(float x, float y, float w, float h, float r, float g, float b, float a) {
    glColor4f(r, g, b, a);
    quad(x, y, w, h);
}

void vitamenu_draw(int screen_w, int screen_h) {
    if (!open) return;
    gfa_font_init("app0:font.ttf");
    if (!text_buf) text_buf = malloc(PANEL_W * PANEL_H * sizeof(uint32_t));
    if (!text_buf || !gfa_font_ready()) return;
    if (dirty) {
        render_text();
        dirty = 0;
    }

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrthof(0, (float) screen_w, (float) screen_h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);

    float sw = (float) screen_w, sh = (float) screen_h;

    /* Last game frame (400x240 corner of the engine's framebuffer texture), dimmed below. */
    if (g_engine_fb_tex && g_engine_fb_tex_w > 0 && g_engine_fb_tex_h > 0) {
        glEnable(GL_TEXTURE_2D);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glBindTexture(GL_TEXTURE_2D, (GLuint) g_engine_fb_tex);
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        quad_uv(0, 0, sw, sh, 400.0f / g_engine_fb_tex_w, 240.0f / g_engine_fb_tex_h);
    }

    glDisable(GL_TEXTURE_2D);
    rect(0, 0, sw, sh, RGBA_DIM);
    rect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, RGBA_PANEL);
    /* 2px frame (Carnivores' 1 GUI unit at 480x320, scaled to 960x544) */
    rect(PANEL_X - 2, PANEL_Y - 2, PANEL_W + 4, 2, RGBA_BORDER);
    rect(PANEL_X - 2, PANEL_Y + PANEL_H, PANEL_W + 4, 2, RGBA_BORDER);
    rect(PANEL_X - 2, PANEL_Y, 2, PANEL_H, RGBA_BORDER);
    rect(PANEL_X + PANEL_W, PANEL_Y, 2, PANEL_H, RGBA_BORDER);
    rect(PANEL_X + 20, PANEL_Y + TITLE_LINE_Y, PANEL_W - 40, 2, RGBA_BORDER);
    rect(PANEL_X + 20, PANEL_Y + OPTIONS_Y, PANEL_W - 40, 1, RGBA_BORDER);
    rect(PANEL_X + 20, PANEL_Y + FOOTER_LINE_Y, PANEL_W - 40, 2, RGBA_BORDER);
    rect(PANEL_X + 8, PANEL_Y + LIST_Y + sel * ROW_H, PANEL_W - 16, ROW_H, RGBA_SEL);

    glEnable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBindTexture(GL_TEXTURE_2D, text_tex);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); /* premultiplied, see font.c */
    quad(PANEL_X, PANEL_Y, PANEL_W, PANEL_H);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}
