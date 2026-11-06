/**
 * @brief Which menu button (if any) falls under (sx,sy).
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <vitaGL.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "../lib/stb/stb_image.h"

#include "androidui.h"
#include "htmlview.h"

extern void game_log(const char *fmt, ...);

typedef struct {
    const char *name; // <nombre>.png, relativo a drawable/
    GLuint tex;
    int w, h;
} androidui_tex;

static androidui_tex g_tex_logo        = { "ui_logo_gamevil.png" };
static androidui_tex g_tex_title_bg    = { "ui_title_bg_nate.png" };
static androidui_tex g_tex_title_logo5 = { "ui_title_logo5.png" };
static androidui_tex g_tex_menu_back0  = { "ui_menu_back0.png" };
static androidui_tex g_tex_menu_back1  = { "ui_menu_back1.png" };
static androidui_tex g_tex_btn_newgame   = { "ui_menu_newgame.png" };
static androidui_tex g_tex_btn_continue  = { "ui_menu_continue.png" };
static androidui_tex g_tex_btn_options   = { "ui_menu_options.png" };
static androidui_tex g_tex_btn_help      = { "ui_menu_help.png" };
static androidui_tex g_tex_btn_about     = { "ui_menu_about.png" };
static androidui_tex g_tex_btn_community = { "ui_menu_community.png" };
static androidui_tex g_tex_about_bg      = { "ui_about_bg.png" };
static androidui_tex g_tex_help_bg       = { "ui_help_bg.png" };
static androidui_tex g_tex_backbtn       = { "ui_menu_back.png" };
static androidui_tex g_tex_reply_bg      = { "reply_page_back_e.png" };
static androidui_tex g_tex_btn_write     = { "button_write_01_global.png" };
static androidui_tex g_tex_btn_later     = { "button_later_01_global.png" };

/**
 * @brief ui_status==5000 (UI_STATUS_REPLY_PAGE): "rate the app" popup that Natives.
 */
#define INFO_IMG_W 480.0f
#define INFO_IMG_H 320.0f
#define INFO_BOX_LEFT_PX 40.0f
#define INFO_BOX_TOP_PX  52.0f
#define INFO_BOX_W_PX   400.0f
#define INFO_BOX_H_PX   235.0f
#define INFO_FONT_PX 15.0f

/**
 * @brief ui_status==5000 (UI_STATUS_REPLY_PAGE): "rate the app" popup that Natives.
 */
#define INFO_TITLE_CENTER_X_PX 240.0f
#define INFO_TITLE_CENTER_Y_PX 28.0f

static htmlview *g_about_text = NULL;
static htmlview *g_help_text = NULL;
static htmlview *g_help_title = NULL;

static int androidui_load_one(androidui_tex *t) {
/**< @brief ui_status==5000 (UI_STATUS_REPLY_PAGE): "rate the app" popup that Natives. */
    char primary_path[256];
    char fallback_path[256];
    snprintf(primary_path, sizeof(primary_path), "ux0:data/zenonia3/drawable/%s", t->name);
    const char *path = primary_path;
    FILE *f = fopen(path, "rb");
    if (f) {
        fclose(f);
    } else {
        snprintf(fallback_path, sizeof(fallback_path), "app0:drawable/%s", t->name);
        f = fopen(fallback_path, "rb");
        if (!f) {
            game_log("[AndroidUI] %s (ni en ux0 ni en app0) no encontrado\n", t->name);
            return 0;
        }
        fclose(f);
        path = fallback_path;
    }

    int channels;
    unsigned char *pixels = stbi_load(path, &t->w, &t->h, &channels, 4);
    if (!pixels) {
        game_log("[AndroidUI] %s: fallo al decodificar PNG (%s)\n", path, stbi_failure_reason());
        return 0;
    }

    glGenTextures(1, &t->tex);
    glBindTexture(GL_TEXTURE_2D, t->tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t->w, t->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    stbi_image_free(pixels);

    game_log("[AndroidUI] cargado %s (%dx%d) -> tex=%u\n", path, t->w, t->h, (unsigned int) t->tex);
    return 1;
}

void androidui_load(int screen_w, int screen_h) {
    androidui_load_one(&g_tex_logo);
    androidui_load_one(&g_tex_title_bg);
    androidui_load_one(&g_tex_title_logo5);
    androidui_load_one(&g_tex_menu_back0);
    androidui_load_one(&g_tex_menu_back1);
    androidui_load_one(&g_tex_btn_newgame);
    androidui_load_one(&g_tex_btn_continue);
    androidui_load_one(&g_tex_btn_options);
    androidui_load_one(&g_tex_btn_help);
    androidui_load_one(&g_tex_btn_about);
    androidui_load_one(&g_tex_btn_community);
    androidui_load_one(&g_tex_about_bg);
    androidui_load_one(&g_tex_help_bg);
    androidui_load_one(&g_tex_backbtn);
    androidui_load_one(&g_tex_reply_bg);
    androidui_load_one(&g_tex_btn_write);
    androidui_load_one(&g_tex_btn_later);

    float panel_w_px = INFO_BOX_W_PX * (float) screen_w / INFO_IMG_W;
    g_about_text = htmlview_load("about", panel_w_px, INFO_FONT_PX);
    g_help_text = htmlview_load("help_eng", panel_w_px, INFO_FONT_PX);
/**
 * @brief "HELP" by hand on the empty ui_help_bg banner.
 */
    g_help_title = htmlview_make_label("HELP", 18.0f, 0xFFFFFFFFu);
}

/**
 * @brief Draw a quad with top-left corner at (x,y) and size (w,h) at.
 */
static void androidui_draw_quad(androidui_tex *t, float x, float y, float w, float h) {
    if (!t->tex) return;

    glBindTexture(GL_TEXTURE_2D, t->tex);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

    const float verts[] = { x, y,  x + w, y,  x, y + h,  x + w, y + h };
    const float uvs[]   = { 0, 0,  1, 0,       0, 1,       1, 1 };
    glVertexPointer(2, GL_FLOAT, 0, verts);
    glTexCoordPointer(2, GL_FLOAT, 0, uvs);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

/**
 * @brief Based on com/gamevil/nexus2/Natives.
 */
typedef struct { androidui_tex *tex; float top240; float left400; } menu_btn_pos;
static const menu_btn_pos MENU_BUTTONS[] = {
    { &g_tex_btn_community, 100, 25 },
    { &g_tex_btn_options,   145, 60 },
    { &g_tex_btn_newgame,   160, 130 },
    { &g_tex_btn_continue,  160, 210 },
    { &g_tex_btn_help,      145, 280 },
    { &g_tex_btn_about,     100, 315 },
};
#define MENU_BUTTON_COUNT (sizeof(MENU_BUTTONS) / sizeof(MENU_BUTTONS[0]))

/**< @brief Same MENU_BUTTONS array as above, but indexed by the public enum for. */
static const androidui_menu_hit MENU_BUTTON_HIT[] = {
    ANDROIDUI_MENU_HIT_COMMUNITY,
    ANDROIDUI_MENU_HIT_OPTIONS,
    ANDROIDUI_MENU_HIT_NEWGAME,
    ANDROIDUI_MENU_HIT_CONTINUE,
    ANDROIDUI_MENU_HIT_HELP,
    ANDROIDUI_MENU_HIT_ABOUT,
};

static int g_menu_cursor = -1; /* index into MENU_BUTTONS, -1 = hidden */

void androidui_menu_cursor_move(int delta) {
    if (g_menu_cursor < 0) {
        g_menu_cursor = 2; /* first press only shows it, on NEW GAME */
        return;
    }
    g_menu_cursor += delta;
    if (g_menu_cursor < 0) g_menu_cursor = 0;
    if (g_menu_cursor >= (int) MENU_BUTTON_COUNT) g_menu_cursor = MENU_BUTTON_COUNT - 1;
}

void androidui_menu_cursor_hide(void) { g_menu_cursor = -1; }

androidui_menu_hit androidui_menu_cursor_hit(void) {
    return g_menu_cursor < 0 ? ANDROIDUI_MENU_HIT_NONE : MENU_BUTTON_HIT[g_menu_cursor];
}

/* Carnivores-style highlight: white L-shaped corners around the button, no fill. Drawn with a 1x1
 * white texture through androidui_draw_quad() -- the exact GL state the menu art already uses --
 * because an untextured draw makes vitaGL build a new fixed-function shader at runtime, which
 * coincided with the 2-6 fps stretch in log_002 while the old filled cursor was on screen. */
static androidui_tex g_tex_white = { "(white)" };

static void draw_menu_cursor(float x, float y, float w, float h) {
    if (!g_tex_white.tex) {
        static const uint32_t white = 0xFFFFFFFFu;
        glGenTextures(1, &g_tex_white.tex);
        glBindTexture(GL_TEXTURE_2D, g_tex_white.tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        g_tex_white.w = g_tex_white.h = 1;
    }
    const float t = 3.0f, l = 14.0f;           /* thickness, arm length */
    const float x0 = x - t, y0 = y - t, x1 = x + w, y1 = y + h;
    androidui_draw_quad(&g_tex_white, x0, y0, l, t);           /* top-left */
    androidui_draw_quad(&g_tex_white, x0, y0, t, l);
    androidui_draw_quad(&g_tex_white, x1 + t - l, y0, l, t);   /* top-right */
    androidui_draw_quad(&g_tex_white, x1, y0, t, l);
    androidui_draw_quad(&g_tex_white, x0, y1, l, t);           /* bottom-left */
    androidui_draw_quad(&g_tex_white, x0, y1 + t - l, t, l);
    androidui_draw_quad(&g_tex_white, x1 + t - l, y1, l, t);   /* bottom-right */
    androidui_draw_quad(&g_tex_white, x1, y1 + t - l, t, l);
}

androidui_menu_hit androidui_menu_hit_test(float sx, float sy, int screen_w, int screen_h) {
    float sw = (float) screen_w, sh = (float) screen_h;
    for (unsigned int i = 0; i < MENU_BUTTON_COUNT; i++) {
        const menu_btn_pos *b = &MENU_BUTTONS[i];
        float bx = b->left400 * sw / 400.0f;
        float by = b->top240 * sh / 240.0f;
        float bw = (float) b->tex->w;
        float bh = (float) b->tex->h;
        if (sx >= bx && sx < bx + bw && sy >= by && sy < by + bh) {
            return MENU_BUTTON_HIT[i];
        }
    }
    return ANDROIDUI_MENU_HIT_NONE;
}

/**< @brief Based on Natives. */
androidui_reply_hit androidui_reply_hit_test(float sx, float sy, int screen_w, int screen_h) {
    float sw = (float) screen_w, sh = (float) screen_h;
    float top = 170.0f * sh / 240.0f;

    float write_x = 20.0f * sw / 400.0f;
    if (sx >= write_x && sx < write_x + (float) g_tex_btn_write.w &&
        sy >= top && sy < top + (float) g_tex_btn_write.h) {
        return ANDROIDUI_REPLY_HIT_WRITE;
    }

    float later_x = 180.0f * sw / 400.0f;
    if (sx >= later_x && sx < later_x + (float) g_tex_btn_later.w &&
        sy >= top && sy < top + (float) g_tex_btn_later.h) {
        return ANDROIDUI_REPLY_HIT_LATER;
    }

    return ANDROIDUI_REPLY_HIT_NONE;
}

/**< @brief Based on main. */
androidui_backbtn_hit androidui_backbtn_hit_test(float sx, float sy, int screen_w, int screen_h) {
    float sw = (float) screen_w;
    float back_x = sw - (float) g_tex_backbtn.w;
    if (sx >= back_x && sx < sw && sy >= 0 && sy < (float) g_tex_backbtn.h) {
        return ANDROIDUI_BACKBTN_HIT_BACK;
    }
    return ANDROIDUI_BACKBTN_HIT_NONE;
}

/**
 * @brief Actual rect on text panel screen (same for ABOUT/HELP, see.).
 */
static void info_panel_rect(int screen_w, int screen_h, float *x, float *y, float *w, float *h) {
    *x = INFO_BOX_LEFT_PX * (float) screen_w / INFO_IMG_W;
    *y = INFO_BOX_TOP_PX * (float) screen_h / INFO_IMG_H;
    *w = INFO_BOX_W_PX * (float) screen_w / INFO_IMG_W;
    *h = INFO_BOX_H_PX * (float) screen_h / INFO_IMG_H;
}

void androidui_scroll_info_text(int ui_status, float delta_px, int screen_w, int screen_h) {
    htmlview *v = ui_status == 5 ? g_about_text : ui_status == 4 ? g_help_text : NULL;
    if (!v) return;
    float px, py, pw, ph;
    info_panel_rect(screen_w, screen_h, &px, &py, &pw, &ph);
    htmlview_scroll(v, delta_px, ph);
}

void androidui_draw(int ui_status, int screen_w, int screen_h) {
/**
 * @brief It is only called for states without the port's own art (see main.).
 */
    if (!(ui_status == -1 || ui_status == 1 || ui_status == 2 || ui_status == 4 || ui_status == 5 || ui_status == 5000)) return;

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
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    float sw = (float) screen_w, sh = (float) screen_h;

    if (ui_status == -1) {
/**< @brief LOGO: before the first real OnUIStatusChange arrives. */
        androidui_draw_quad(&g_tex_logo, 0, 0, sw, sh);
    } else if (ui_status == 1) {
/**
 * @brief TITLE: full background + small logo centered below (bottom|center_horizontal, native size).
 */
        androidui_draw_quad(&g_tex_title_bg, 0, 0, sw, sh);
        float lw = sh > 0 ? (float) g_tex_title_logo5.w : 0; // tamano nativo, sin escalar
        float lh = (float) g_tex_title_logo5.h;
        androidui_draw_quad(&g_tex_title_logo5, (sw - lw) / 2.0f, sh - lh, lw, lh);
    } else if (ui_status == 2) {
/**< @brief MAINMENU: full background + top strip + 6 buttons. */
        androidui_draw_quad(&g_tex_menu_back0, 0, 0, sw, sh);
/**< @brief ui_menu_back1: marginTop="142px" hardcodeado en el XML (no.). */
        float back1_y = 142.0f * sh / 320.0f;
        androidui_draw_quad(&g_tex_menu_back1, 0, back1_y, sw, (float) g_tex_menu_back1.h);
        for (unsigned int i = 0; i < MENU_BUTTON_COUNT; i++) {
            const menu_btn_pos *b = &MENU_BUTTONS[i];
            float bx = b->left400 * sw / 400.0f;
            float by = b->top240 * sh / 240.0f;
            androidui_draw_quad(b->tex, bx, by, (float) b->tex->w, (float) b->tex->h);
            if ((int) i == g_menu_cursor)
                draw_menu_cursor(bx, by, (float) b->tex->w, (float) b->tex->h);
        }
    } else if (ui_status == 5 || ui_status == 4) {
/**< @brief Error 500 (Server Error). */
        androidui_draw_quad(ui_status == 5 ? &g_tex_about_bg : &g_tex_help_bg, 0, 0, sw, sh);
        androidui_draw_quad(&g_tex_backbtn, sw - (float) g_tex_backbtn.w, 0,
                             (float) g_tex_backbtn.w, (float) g_tex_backbtn.h);
        if (ui_status == 4 && g_help_title) {
/**< @brief ui_help_bg. */
            float lw, lh;
            htmlview_native_size(g_help_title, &lw, &lh);
            float cx = INFO_TITLE_CENTER_X_PX * sw / INFO_IMG_W;
            float cy = INFO_TITLE_CENTER_Y_PX * sh / INFO_IMG_H;
            htmlview_draw(g_help_title, cx - lw / 2.0f, cy - lh / 2.0f, lw, lh);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        float px, py, pw, ph;
        info_panel_rect(screen_w, screen_h, &px, &py, &pw, &ph);
        htmlview_draw(ui_status == 5 ? g_about_text : g_help_text, px, py, pw, ph);
/**< @brief htmlview_draw() changes the blend func for the premultiplied alpha. */
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else if (ui_status == 5000) {
/**< @brief REPLY_PAGE: "Rate the app" popup displayed when tapped. */
        androidui_draw_quad(&g_tex_reply_bg, 0, 0, sw, sh);
        float top = 170.0f * sh / 240.0f;
        androidui_draw_quad(&g_tex_btn_write, 20.0f * sw / 400.0f, top,
                             (float) g_tex_btn_write.w, (float) g_tex_btn_write.h);
        androidui_draw_quad(&g_tex_btn_later, 180.0f * sw / 400.0f, top,
                             (float) g_tex_btn_later.w, (float) g_tex_btn_later.h);
    }

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}
