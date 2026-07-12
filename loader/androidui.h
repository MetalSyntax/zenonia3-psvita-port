#ifndef ANDROIDUI_H
#define ANDROIDUI_H

// Dibuja los overlays Android (ImageView/ImageButton) que el juego original
// superpone al GLSurfaceView para logo/titulo/menu -- ver la nota completa
// en androidui.c. Este loader no tiene capa Java/Android, asi que sin esto
// esas 3 pantallas se ven en blanco/negro (solo lo que el .so dibuja nativo
// de fondo, sin el arte real encima).
// screen_w/screen_h: necesarios para wrapear el texto de ABOUT/HELP al ancho
// real del panel en pixeles de pantalla (ver htmlview_load() en androidui.c).
void androidui_load(int screen_w, int screen_h);
void androidui_draw(int ui_status, int screen_w, int screen_h);

// Que boton del menu (si alguno) cae bajo (sx,sy) -- coordenadas de PANTALLA
// (mismo espacio 0..screen_w/0..screen_h que androidui_draw(), NO el espacio
// de juego 400x240 que usa el resto del touch en main.c). Solo tiene sentido
// llamarlo con ui_status==2 (MAINMENU). Ver la nota en main.c: en el APK
// original estos son ImageButton de Android que consumen el toque ANTES de
// que llegue al GLSurfaceView -- por eso hace falta interceptarlos aca en
// vez de dejar que main.c reenvie el toque crudo al motor.
typedef enum {
    ANDROIDUI_MENU_HIT_NONE = 0,
    ANDROIDUI_MENU_HIT_COMMUNITY,
    ANDROIDUI_MENU_HIT_OPTIONS,
    ANDROIDUI_MENU_HIT_NEWGAME,
    ANDROIDUI_MENU_HIT_CONTINUE,
    ANDROIDUI_MENU_HIT_HELP,
    ANDROIDUI_MENU_HIT_ABOUT,
} androidui_menu_hit;

androidui_menu_hit androidui_menu_hit_test(float sx, float sy, int screen_w, int screen_h);

// ui_status==5000 (UI_STATUS_REPLY_PAGE): popup de "valorar la app" que
// Natives.showReplyMoveComponent() muestra al tocar "Continuar" en el menu
// (ver Natives.java showReplyMoveComponent()/hideReplyMoveComponent() -- 2
// ImageButton reales, reply_write_id/reply_later_id, no un WebView).
typedef enum {
    ANDROIDUI_REPLY_HIT_NONE = 0,
    ANDROIDUI_REPLY_HIT_WRITE,
    ANDROIDUI_REPLY_HIT_LATER,
} androidui_reply_hit;

androidui_reply_hit androidui_reply_hit_test(float sx, float sy, int screen_w, int screen_h);

// ui_status==5 (UI_STATUS_ABOUT) y ui_status==4 (UI_STATUS_HELP):
// Natives.showAboutComponent()/showHelpComponent() muestran un fondo
// (aboutBg/helpBg) + el MISMO boton "back" compartido (uiback, arriba a la
// derecha, misma posicion en las 2 pantallas) encima de un WebView con
// about.html/help_eng.html -- el texto de esos WebView ahora se rasteriza
// con htmlview.c (ver androidui.c), pero el boton back se resuelve aca para
// poder salir tocando la pantalla igual que con Circulo/HAL_KEY_BACK.
typedef enum {
    ANDROIDUI_BACKBTN_HIT_NONE = 0,
    ANDROIDUI_BACKBTN_HIT_BACK,
} androidui_backbtn_hit;

androidui_backbtn_hit androidui_backbtn_hit_test(float sx, float sy, int screen_w, int screen_h);

// Scroll del texto de ABOUT/HELP (D-pad arriba/abajo, ver main.c) -- no-op si
// ui_status no es 4 o 5. delta_px > 0 baja.
void androidui_scroll_info_text(int ui_status, float delta_px, int screen_w, int screen_h);

#endif
