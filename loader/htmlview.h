#ifndef HTMLVIEW_H
#define HTMLVIEW_H

#include <stdint.h>

// Reemplazo de los WebView que Android mostraba encima de las pantallas
// ABOUT/HELP (about.html/help_eng.html originales del APK, ver
// zenonia3/assets/html/ y port_progress.md) -- este loader no tiene un
// renderizador HTML real, asi que cada documento se parsea UNA vez
// (htmltext_extract), se wrapea a un ancho de panel fijo y se rasteriza
// completo (NanumGothic, el mismo font.ttf que ya usa el puente GFA) a una
// UNICA textura GL alta -- el "scroll" es solo mover la ventana de texture
// coords que se muestra en el panel de pantalla fijo.
typedef struct htmlview htmlview;

// Carga app0:html/<name>.html (empaquetado en el VPK, ver CMakeLists.txt).
// panel_w_px es el ancho REAL en pixeles de pantalla del panel donde se va a
// dibujar (para wrapear las lineas) -- calcularlo con el sw/sh de
// androidui_draw(), no con el espacio logico 400x240. Devuelve NULL si el
// archivo no existe o el font (gfa_font_init) no esta disponible.
htmlview *htmlview_load(const char *name, float panel_w_px, float font_px);

// Textura de UNA sola linea (p.ej. el titulo "HELP" que Android mostraba en
// un TextView aparte, ver Natives.showHelpAboutTitleTextComponent() --
// ABOUT ya lo trae dibujado en el propio fondo, ui_about_bg.png, HELP no).
// Reusa el mismo tipo/htmlview_draw() -- content_h == alto de una linea, sin
// necesidad de scroll.
htmlview *htmlview_make_label(const char *text, float font_px, uint32_t color);

// Dibuja el panel (x,y,w,h en espacio de pantalla, mismo que
// androidui_draw_quad) mostrando la ventana actual de scroll. Asume que el
// caller ya dejo listo el estado GL (proyeccion ortografica, blend, texture
// 2d, client states) -- mismo contrato que androidui_draw_quad().
void htmlview_draw(htmlview *v, float x, float y, float w, float h);

// delta_px > 0 baja (muestra texto mas abajo). viewport_h es el mismo `h` de
// htmlview_draw() -- se usa para no dejar scrollear mas alla del final real
// del contenido.
void htmlview_scroll(htmlview *v, float delta_px, float viewport_h);

// Tamano nativo (tamano real de la textura, en px) -- para dibujar una
// etiqueta (htmlview_make_label()) a su tamano real sin distorsion, o para
// centrarla a mano en vez de usar un rect fijo.
void htmlview_native_size(htmlview *v, float *w, float *h);

#endif
