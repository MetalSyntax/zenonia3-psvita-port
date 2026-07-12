/*
 * androidui.c
 *
 * El logo de Gamevil, el fondo de titulo y el fondo + botones del menu
 * principal (New Game/Continue/Options/Help/About/Community) del APK
 * original NO los dibuja el .so nativo -- son ImageView/ImageButton de
 * Android superpuestos al GLSurfaceView (ver res/layout/main.xml y
 * com/gamevil/nexus2/Natives.java: showTitleComponent()/showMenuComponent()/
 * showMenuItemComponent()). Este loader no tiene ninguna capa Java/Android
 * (es un soloader puro), asi que esas 3 pantallas se veian en blanco (logo)
 * y despues negro (titulo/menu) -- lo unico que se dibujaba de fondo era el
 * DrawFillRect blanco propio del .so (CMvTitleState::DrawZeroGrade/
 * DrawTeamLogo, out_ghidra.c:105397/105434), sin el arte real encima.
 *
 * Solucion: replicar esos overlays a mano con texturas GL, usando los PNG
 * reales del APK (variantes "globales"/en ingles -- no las _kr/_jp/_ch) ya
 * convertidos a RGBA8888 crudo por tools/build_android_ui_assets.py y
 * empaquetados en el VPK (ver CMakeLists.txt, mismo mecanismo que font.ttf).
 *
 * Posiciones: NO son inventadas -- salen de leer el codigo Java real
 * (com/gamevil/nexus2/Natives.java, showMenuItemComponent()) que calcula los
 * margenes de los 6 botones del menu como fraccion de
 * `displayWidth`/`displayHeight` sobre una base de 400x240 -- exactamente la
 * misma resolucion logica (GAME_W/GAME_H) que ya usa este loader para todo
 * lo demas. La unica rama que cambia esos valores (`displayWidth >= 1024`)
 * no aplica en Vita (960 < 1024), asi que los botones quedan a su tamano
 * NATIVO de PNG (82x61), igual que en cualquier telefono real de la epoca
 * con esa resolucion -- no es una aproximacion, es literalmente lo que hacia
 * el codigo original en ese branch.
 *
 * Excepcion marcada abajo: el margen superior de "ui_menu_back1" (franja
 * decorativa arriba del menu) esta hardcodeado en el XML como "142px" (no
 * calculado en Java como los demas), asi que su base de escala real no esta
 * 100% confirmada -- se aproxima usando la altura nativa del propio PNG
 * (320, no 240) como referencia. No bloqueante: "ui_menu_back0" ya cubre
 * toda la pantalla de fondo, esta franja es puramente decorativa.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <vitaGL.h>

#include "androidui.h"
#include "htmlview.h"

extern void game_log(const char *fmt, ...);

typedef struct {
    const char *rgba_path; // app0:androidui/<nombre>.rgba
    GLuint tex;
    int w, h;
} androidui_tex;

static androidui_tex g_tex_logo        = { "app0:androidui/ui_logo_gamevil.rgba" };
static androidui_tex g_tex_title_bg    = { "app0:androidui/ui_title_bg_nate.rgba" };
static androidui_tex g_tex_title_logo5 = { "app0:androidui/ui_title_logo5.rgba" };
static androidui_tex g_tex_menu_back0  = { "app0:androidui/ui_menu_back0.rgba" };
static androidui_tex g_tex_menu_back1  = { "app0:androidui/ui_menu_back1.rgba" };
static androidui_tex g_tex_btn_newgame   = { "app0:androidui/ui_menu_newgame.rgba" };
static androidui_tex g_tex_btn_continue  = { "app0:androidui/ui_menu_continue.rgba" };
static androidui_tex g_tex_btn_options   = { "app0:androidui/ui_menu_options.rgba" };
static androidui_tex g_tex_btn_help      = { "app0:androidui/ui_menu_help.rgba" };
static androidui_tex g_tex_btn_about     = { "app0:androidui/ui_menu_about.rgba" };
static androidui_tex g_tex_btn_community = { "app0:androidui/ui_menu_community.rgba" };
static androidui_tex g_tex_about_bg      = { "app0:androidui/ui_about_bg.rgba" };
static androidui_tex g_tex_help_bg       = { "app0:androidui/ui_help_bg.rgba" };
static androidui_tex g_tex_backbtn       = { "app0:androidui/ui_menu_back.rgba" };
static androidui_tex g_tex_reply_bg      = { "app0:androidui/reply_page_back_e.rgba" };
static androidui_tex g_tex_btn_write     = { "app0:androidui/button_write_01_global.rgba" };
static androidui_tex g_tex_btn_later     = { "app0:androidui/button_later_01_global.rgba" };

// Panel de texto de ABOUT/HELP: el rect de Android para
// aboutWebView/helpWebView (leftMargin=1/400, topMargin=5/240, width=300/400,
// height=146/240) resulto NO alinear con el cuadro gris real de
// ui_about_bg.png/ui_help_bg.png (confirmado visualmente por el usuario --
// texto pegado arriba a la izquierda, superpuesto al marco/titulo). Estos 2
// PNG son fondo fitXY a pantalla completa, asi que su propio espacio de
// pixeles (480x320, el tamano nativo del PNG) mapea 1:1 a fracciones de
// pantalla -- medido con Pillow sobre el PNG real (buscando el borde del
// cuadro gris oscuro): interior util en x=[27,452] y=[43,300] (misma
// geometria en las 2 imagenes, mismo template). Se le resta un margen extra
// (~13-14px) para no tocar el marco decorativo ni superponerse con el
// titulo "ABOUT" horneado en el banner superior.
#define INFO_IMG_W 480.0f
#define INFO_IMG_H 320.0f
#define INFO_BOX_LEFT_PX 40.0f
#define INFO_BOX_TOP_PX  52.0f
#define INFO_BOX_W_PX   400.0f
#define INFO_BOX_H_PX   235.0f
#define INFO_FONT_PX 15.0f

// Centro horizontal + linea de base del titulo "ABOUT" horneado en
// ui_about_bg.png (medido buscando los pixeles de texto mas brillantes del
// banner: bbox x=[123,358] y=[18,39] sobre 480x320 -> centro (240, 28)).
// ui_help_bg.png tiene el MISMO banner pero vacio (el titulo era un TextView
// de Android aparte, Natives.showHelpAboutTitleTextComponent()) -- se dibuja
// "HELP" ahi mismo a mano, unicamente para esta pantalla.
#define INFO_TITLE_CENTER_X_PX 240.0f
#define INFO_TITLE_CENTER_Y_PX 28.0f

static htmlview *g_about_text = NULL;
static htmlview *g_help_text = NULL;
static htmlview *g_help_title = NULL;

static int androidui_load_one(androidui_tex *t) {
    FILE *f = fopen(t->rgba_path, "rb");
    if (!f) {
        char fallback_path[256];
        snprintf(fallback_path, sizeof(fallback_path), "ux0:data/zenonia3/%s", t->rgba_path + 5);
        f = fopen(fallback_path, "rb");
        if (!f) {
            game_log("[AndroidUI] %s (ni en ux0) no encontrado\n", t->rgba_path);
            return 0;
        }
    }
    uint32_t header[2];
    if (fread(header, sizeof(uint32_t), 2, f) != 2) {
        game_log("[AndroidUI] %s: header invalido\n", t->rgba_path);
        fclose(f);
        return 0;
    }
    t->w = (int) header[0];
    t->h = (int) header[1];
    size_t n = (size_t) t->w * (size_t) t->h * 4;
    void *pixels = malloc(n);
    if (!pixels) { fclose(f); return 0; }
    size_t got = fread(pixels, 1, n, f);
    fclose(f);
    if (got != n) {
        game_log("[AndroidUI] %s: %zu/%zu bytes leidos, se descarta\n", t->rgba_path, got, n);
        free(pixels);
        return 0;
    }

    glGenTextures(1, &t->tex);
    glBindTexture(GL_TEXTURE_2D, t->tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t->w, t->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    free(pixels);

    game_log("[AndroidUI] cargado %s (%dx%d) -> tex=%u\n", t->rgba_path, t->w, t->h, (unsigned int) t->tex);
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
    // "HELP" a mano en el banner vacio de ui_help_bg.png -- ABOUT ya lo trae
    // horneado en ui_about_bg.png, ver Natives.showHelpAboutTitleTextComponent().
    g_help_title = htmlview_make_label("HELP", 18.0f, 0xFFFFFFFFu);
}

// Dibuja un quad con esquina superior-izquierda en (x,y) y tamano (w,h) en
// espacio de pantalla (proyeccion ortografica 0..screen_w / 0..screen_h ya
// activa -- ver androidui_draw()). GL_BLEND habilitado: la mayoria de estos
// PNG son RGBA reales (botones/franjas con transparencia).
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

// Basado en com/gamevil/nexus2/Natives.java: showMenuItemComponent(). Todas
// las fracciones son sobre 400 (ancho) / 240 (alto) -- misma base logica
// GAME_W/GAME_H de main.c. La rama `displayWidth >= 1024` de ese metodo no
// aplica en Vita (960 < 1024), asi que el tamano de cada boton queda en su
// resolucion nativa de PNG (82x61).
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

// Mismo array MENU_BUTTONS de arriba, pero indexado por el enum publico para
// poder recalcular el rect real (en espacio de pantalla) sin duplicar las
// fracciones 400/240.
static const androidui_menu_hit MENU_BUTTON_HIT[] = {
    ANDROIDUI_MENU_HIT_COMMUNITY,
    ANDROIDUI_MENU_HIT_OPTIONS,
    ANDROIDUI_MENU_HIT_NEWGAME,
    ANDROIDUI_MENU_HIT_CONTINUE,
    ANDROIDUI_MENU_HIT_HELP,
    ANDROIDUI_MENU_HIT_ABOUT,
};

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

// Basado en Natives.java: showReplyMoveComponent() -- img_btn_write_Layout/
// img_btn_later_Layout, misma base logica 400x240. REPLY_OFFSET_TOPMARGIN_0
// (170) es comun a ambos botones; REPLY_OFFSET_LEFTMARGIN_0 (20, "escribir
// resena") y REPLY_OFFSET_LEFTMARGIN_1 (180, "mas tarde") los separan.
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

// Basado en main.xml: "uiback" (drawable ui_menu_back) tiene
// layout_gravity="top|right", wrap_content, sin margin -- queda pegado a la
// esquina superior derecha a su tamano nativo de PNG. Compartido por ABOUT
// (ui_status==5) y HELP (ui_status==4): misma vista, misma posicion.
androidui_backbtn_hit androidui_backbtn_hit_test(float sx, float sy, int screen_w, int screen_h) {
    float sw = (float) screen_w;
    float back_x = sw - (float) g_tex_backbtn.w;
    if (sx >= back_x && sx < sw && sy >= 0 && sy < (float) g_tex_backbtn.h) {
        return ANDROIDUI_BACKBTN_HIT_BACK;
    }
    return ANDROIDUI_BACKBTN_HIT_NONE;
}

// Rect real en pantalla del panel de texto (mismo para ABOUT/HELP, ver la
// nota junto a los #define INFO_BOX_*/INFO_IMG_*).
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
    // Solo se llama para los estados sin arte propio del port (ver main.c).
    // Nada que dibujar en cualquier otro estado.
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
        // LOGO: antes de que llegue el primer OnUIStatusChange real.
        androidui_draw_quad(&g_tex_logo, 0, 0, sw, sh);
    } else if (ui_status == 1) {
        // TITLE: fondo completo + logo chico centrado abajo (bottom|center_horizontal, tamano nativo).
        androidui_draw_quad(&g_tex_title_bg, 0, 0, sw, sh);
        float lw = sh > 0 ? (float) g_tex_title_logo5.w : 0; // tamano nativo, sin escalar
        float lh = (float) g_tex_title_logo5.h;
        androidui_draw_quad(&g_tex_title_logo5, (sw - lw) / 2.0f, sh - lh, lw, lh);
    } else if (ui_status == 2) {
        // MAINMENU: fondo completo + franja superior + 6 botones.
        androidui_draw_quad(&g_tex_menu_back0, 0, 0, sw, sh);
        // ui_menu_back1: marginTop="142px" hardcodeado en el XML (no
        // calculado en Java como los demas) -- aproximado sobre la altura
        // nativa del propio PNG (320, no 240). Ancho estirado a pantalla
        // completa (match_parent), alto nativo (wrap_content).
        float back1_y = 142.0f * sh / 320.0f;
        androidui_draw_quad(&g_tex_menu_back1, 0, back1_y, sw, (float) g_tex_menu_back1.h);
        for (unsigned int i = 0; i < MENU_BUTTON_COUNT; i++) {
            const menu_btn_pos *b = &MENU_BUTTONS[i];
            float bx = b->left400 * sw / 400.0f;
            float by = b->top240 * sh / 240.0f;
            androidui_draw_quad(b->tex, bx, by, (float) b->tex->w, (float) b->tex->h);
        }
    } else if (ui_status == 5 || ui_status == 4) {
        // ABOUT (5) / HELP (4): fondo + boton "back" arriba a la derecha +
        // el texto que Android mostraba en un WebView encima de ese mismo
        // fondo, ahora parseado/rasterizado por htmlview.c (about.html /
        // help_eng.html originales) -- ver port_progress.md. Circulo
        // (HAL_KEY_BACK), tocar el boton, o D-pad arriba/abajo para scrollear
        // (ver androidui_scroll_info_text(), llamado desde main.c).
        androidui_draw_quad(ui_status == 5 ? &g_tex_about_bg : &g_tex_help_bg, 0, 0, sw, sh);
        androidui_draw_quad(&g_tex_backbtn, sw - (float) g_tex_backbtn.w, 0,
                             (float) g_tex_backbtn.w, (float) g_tex_backbtn.h);
        if (ui_status == 4 && g_help_title) {
            // ui_help_bg.png trae el banner superior VACIO (a diferencia de
            // ui_about_bg.png, que ya tiene "ABOUT" horneado) -- dibujar
            // "HELP" a mano, centrado en la misma posicion que ocupaba el
            // TextView de Android.
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
        // htmlview_draw() cambia el blend func para el alpha premultiplicado
        // del texto (ver htmlview.c) -- restaurar el de este archivo por si
        // se agrega algo despues en este mismo branch.
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else if (ui_status == 5000) {
        // REPLY_PAGE: popup de "valorar la app" que se muestra al tocar
        // Continuar en el menu (Natives.showReplyMoveComponent()) -- fondo +
        // boton "escribir resena" + boton "mas tarde".
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
