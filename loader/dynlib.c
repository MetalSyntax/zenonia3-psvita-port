#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/select.h>
#include <pthread.h>

#include <vitaGL.h>
#include "so_util.h"

extern void game_log(const char *fmt, ...);
extern volatile int g_ui_status;

// El logo/titulo/menu (ui_status 0-2, ver ZenoniaUIControllerView.java:
// UI_STATUS_LOGO/TITLE/MAINMENU) se ve blanco y despues negro (confirmado
// por el usuario), sin crash -- CMvTitleState::DrawZeroGrade/DrawTeamLogo
// (out_ghidra.c:105397/105434) hacen un DrawFillRect blanco y LUEGO una
// llamada virtual que dibuja el logo/arte encima con un fade de alpha
// (contador interno < 0x10 -> alpha parcial, si no -> opaco). Hipotesis sin
// confirmar todavia (investigacion estatica, sin acceso a consola real):
// el fade nunca llega a alpha=255/blend opaco, o el blend/textencv activo
// deja el quad invisible. Los logs de glColorPointer/glTexEnvf/glBlendFunc ya
// existian pero estaban topados a los primeros 10-20 llamados (aca ya paso
// el menu) -- se saca el tope MIENTRAS el ui_status este en ese rango para
// poder confirmar con UN log mas en consola real cual de las dos hipotesis
// es la correcta. Quitar este helper (y los `|| zenonia_verbose_ui()` que lo
// usan) una vez que el bug de fondo este resuelto y confirmado.
static int zenonia_verbose_ui(void) {
    return g_ui_status >= 0 && g_ui_status <= 2;
}

// --- Ocultar cruceta + botones nativos (GVUIPlayerController) ---
//
// Primer intento (por textura, ver git log) NO funcionaba: confirmado con un
// log real que Dpad.pzx sube sus graficos a un atlas GL COMPARTIDO via
// glTexSubImage2D (nunca un glTexImage2D nuevo despues de su readAssets), asi
// que "etiquetar la proxima textura subida" nunca etiquetaba nada -- la
// cruceta y los botones seguian visibles siempre pese al toggle.
//
// Solucion real, mas simple y sin tocar nada de GL: GVUIController::Draw()
// (out_ghidra.c:26330, simetrico con PointerPress/Move/Release un poco mas
// abajo en el mismo archivo) es un simple `for (i in 0..*(this+0x19c))
// { dibujar hijo[i] }` -- si ese contador de "objetos activos" es 0, el
// controller no dibuja NI recibe touch de ninguno de sus hijos, sin afectar a
// ningun otro GVUIController (barra de vida/mana, minimapa, etc., cada uno
// con su propia instancia y su propio contador). GVUIPlayerController (la
// cruceta + hasta 5 botones de accion, confirmado por
// GVUIPlayerController::InitialPlayerPadSet(), out_ghidra.c:28466) es la
// UNICA instancia de esa clase en todo el juego -- alcanza con hookear su
// constructor (_ZN20GVUIPlayerControllerC2Ev), dejar que corra normal (arma
// los 5 objetos igual que siempre) y despues poner ESE contador en 0. No hace
// falta re-hookear Draw() para todos los demas controllers ni tocar el estado
// de GL en absoluto -- el touch fisico de la Vita ya cubre lo mismo via
// btn_map en main.c, asi que perder el hit-test de estos botones puntuales no
// quita ninguna funcionalidad real.
//
// Reversible sin tocar este archivo: recompilar con
// `-DHIDE_VIRTUAL_GAMEPAD=OFF` (ver CMakeLists.txt) deja este hook sin
// instalar (zenonia_install_hide_dpad_hook() se vuelve un no-op).
#ifdef ZENONIA_HIDE_DPAD_UI
static so_hook g_player_controller_ctor_hook;

// offset confirmado en GVUIController::GVUIController() (out_ghidra.c:26369,
// `memset(this+8,0,400); *(int*)(this+0x19c)=0;`) y reusado identico por
// Draw()/PointerPress()/PointerMove()/PointerRelease() como limite del loop
// sobre los hasta 100 punteros a hijo que arrancan en this+8.
#define GVUI_CONTROLLER_ACTIVE_COUNT_OFFSET 0x19c

static void GVUIPlayerController_ctor_hook(void *this) {
    // Restaurar el codigo original, correr el constructor real tal cual (arma
    // GVUIDirectionPad + 5 GVUIBatterButton exactamente como en Android), y
    // recien despues apagar el contador -- si lo hicieramos antes, la cruceta
    // ni siquiera terminaria de construirse bien.
    so_unhook(&g_player_controller_ctor_hook);
    ((void (*)(void *)) g_player_controller_ctor_hook.thumb_addr)(this);
    *(int *)((char *) this + GVUI_CONTROLLER_ACTIVE_COUNT_OFFSET) = 0;
    game_log("[HideDpad] GVUIPlayerController construido en %p -- contador de objetos activos puesto a 0 (Draw/touch de la cruceta y los 5 botones de accion deshabilitados)\n", this);
}
#endif

// Llamar DESPUES de so_relocate/so_resolve (necesita el modulo ya con
// dynsym/dynstr resueltos, ver so_symbol()) y ANTES de la primera llamada a
// cualquier Native*() del juego -- GVUIPlayerController se construye durante
// el arranque normal del motor.
void zenonia_install_hide_dpad_hook(so_module *mod) {
#ifdef ZENONIA_HIDE_DPAD_UI
    uintptr_t ctor_addr = so_symbol(mod, "_ZN20GVUIPlayerControllerC2Ev");
    if (!ctor_addr) {
        game_log("[HideDpad] simbolo _ZN20GVUIPlayerControllerC2Ev no encontrado -- cruceta/botones NO se ocultan\n");
        return;
    }
    g_player_controller_ctor_hook = hook_addr(ctor_addr, (uintptr_t) GVUIPlayerController_ctor_hook);
    so_flush_caches(mod);
    game_log("[HideDpad] hook instalado en GVUIPlayerController::ctor (0x%08x)\n", (unsigned int) ctor_addr);
#else
    (void) mod;
#endif
}

// Stub para __errno
int* __errno(void) {
    static int dummy_errno = 0;
    return &dummy_errno;
}

// Wrappers para OpenGL de punto fijo (GLES1)
void glClearColorx_wrapper(int r, int g, int b, int a) {
    glClearColor(r / 65536.0f, g / 65536.0f, b / 65536.0f, a / 65536.0f);
}

void glTexParameterx_wrapper(GLenum target, GLenum pname, int param) {
    glTexParameteri(target, pname, param);
}

// Buffer de conversion reutilizado entre llamadas: el motor sube el
// framebuffer 800x480 completo antes de CADA quad (varias veces por frame),
// y hacer malloc/free de 1.5 MB en cada upload era parte del costo por
// frame. El resultado es valido solo hasta la proxima llamada — los call
// sites lo consumen de inmediato en glTexImage2D/glTexSubImage2D.
void *convert_rgb565_to_rgba8888(const void *pixels, int width, int height) {
    static uint8_t *conv_buf = NULL;
    static int conv_buf_cap = 0;

    if (!pixels) return NULL;
    uint16_t *src = (uint16_t *)pixels;
    int npix = width * height;
    if (npix * 4 > conv_buf_cap) {
        conv_buf = (uint8_t *)realloc(conv_buf, npix * 4);
        conv_buf_cap = npix * 4;
    }
    uint8_t *dst = conv_buf;

    for (int i = 0; i < npix; i++) {
        uint16_t p = src[i];
        dst[i*4 + 0] = ((p >> 11) & 0x1F) * 255 / 31; // R
        dst[i*4 + 1] = ((p >> 5) & 0x3F) * 255 / 63;  // G
        dst[i*4 + 2] = (p & 0x1F) * 255 / 31;         // B
        dst[i*4 + 3] = 255;                           // A
    }

    static int conv_log = 0;
    if (conv_log < 10) {
        uint16_t min_p = 0xFFFF, max_p = 0x0000;
        for (int i = 0; i < npix; i++) {
            if (src[i] < min_p) min_p = src[i];
            if (src[i] > max_p) max_p = src[i];
        }
        game_log("[GL] convert_rgb565_to_rgba8888: min=%04x max=%04x\n", min_p, max_p);
        conv_log++;
    }

    return dst;
}

// El motor sube su framebuffer interno de software (400x240, ver Fase 1 del
// plan) en RGB565 -- vitaGL no lo maneja igual que el motor original espera,
// asi que se convierte a RGBA8888 en CPU antes de subirlo. Mismo bug
// confirmado en hardware real para Zenonia 2 (mismo motor).
void glTexImage2D_wrapper(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels) {
    static int img_log = 0;
    if (img_log < 10) {
        game_log("[GL] glTexImage2D target=%x intFmt=%x w=%d h=%d format=%x type=%x pixels=%p\n",
                 target, internalformat, width, height, format, type, pixels);
        img_log++;
    }
    glGetError();

    if (format == GL_RGB && type == GL_UNSIGNED_SHORT_5_6_5) {
        if (pixels && img_log < 20) {
            uint16_t *p = (uint16_t *)pixels;
            game_log("  -> First 4 pixels: %04x %04x %04x %04x\n", p[0], p[1], p[2], p[3]);
        }
        void *new_pixels = convert_rgb565_to_rgba8888(pixels, width, height);
        glTexImage2D(target, level, GL_RGBA, width, height, border, GL_RGBA, GL_UNSIGNED_BYTE, new_pixels);
    } else {
        glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
    }

    // Forzar filtros min/mag para que la textura no se trate como incompleta por falta de mipmaps
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) game_log("[GL] glTexImage2D ERROR: %x\n", err);
}

void glTexSubImage2D_wrapper(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels) {
    static int subimg_log = 0;
    if (subimg_log < 10) {
        game_log("[GL] glTexSubImage2D target=%x w=%d h=%d format=%x type=%x\n", target, width, height, format, type);
        if (type == GL_UNSIGNED_SHORT_5_6_5 && pixels) {
            uint16_t *p = (uint16_t*)pixels;
            game_log("  -> First 4 pixels: %04x %04x %04x %04x\n", p[0], p[1], p[2], p[3]);
        }
        subimg_log++;
    }
    glGetError();

    if (format == GL_RGB && type == GL_UNSIGNED_SHORT_5_6_5) {
        void *new_pixels = convert_rgb565_to_rgba8888(pixels, width, height);
        glTexSubImage2D(target, level, xoffset, yoffset, width, height, GL_RGBA, GL_UNSIGNED_BYTE, new_pixels);
    } else {
        glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
    }

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) game_log("[GL] glTexSubImage2D ERROR: %x\n", err);
}

// vitaGL no consume arrays de vertices GL_FIXED correctamente (misma clase de
// bug que glClearColorx_wrapper/glTexParameterx_wrapper arriba). Este motor
// (derivado de J2ME/BREW) manda datos de vertices en punto fijo Q16.16; sin
// esta conversion, vitaGL lee los enteros crudos como si fueran floats y la
// geometria queda fuera del frustum (pantalla en blanco aunque el motor siga
// renderizando frames). glVertexPointer_wrapper diferisce la conversion hasta
// glDrawArrays_wrapper, que es donde se sabe cuantos vertices hay.
static const int32_t *pending_fixed_verts = NULL;
static GLint pending_fixed_size = 0;
static GLsizei pending_fixed_stride = 0;
static GLfloat *fixed_vert_buf = NULL;
static int fixed_vert_buf_cap = 0;

static const int32_t *pending_fixed_colors = NULL;
static GLint pending_fixed_color_size = 0;
static GLsizei pending_fixed_color_stride = 0;
static GLfloat *fixed_color_buf = NULL;
static int fixed_color_buf_cap = 0;

static const int32_t *pending_fixed_texcoords = NULL;
static GLint pending_fixed_texcoord_size = 0;
static GLsizei pending_fixed_texcoord_stride = 0;
static GLfloat *fixed_texcoord_buf = NULL;
static int fixed_texcoord_buf_cap = 0;

void glDrawArrays_wrapper(GLenum mode, GLint first, GLsizei count) {
    static int draw_count = 0;
    if (draw_count < 10) {
        game_log("[GL] glDrawArrays mode=%x first=%d count=%d\n", mode, first, count);
        draw_count++;
    }

    if (pending_fixed_verts) {
        int needed_verts = first + count;
        int needed_floats = needed_verts * pending_fixed_size;
        if (needed_floats > fixed_vert_buf_cap) {
            fixed_vert_buf = (GLfloat *)realloc(fixed_vert_buf, needed_floats * sizeof(GLfloat));
            fixed_vert_buf_cap = needed_floats;
        }
        int stride_elems = pending_fixed_stride > 0 ? pending_fixed_stride / sizeof(int32_t) : pending_fixed_size;
        for (int i = 0; i < needed_verts; i++) {
            const int32_t *src = pending_fixed_verts + i * stride_elems;
            GLfloat *dst = fixed_vert_buf + i * pending_fixed_size;
            for (int c = 0; c < pending_fixed_size; c++) {
                dst[c] = src[c] / 65536.0f;
            }
        }
        glVertexPointer(pending_fixed_size, GL_FLOAT, 0, fixed_vert_buf);
    }

    if (pending_fixed_colors) {
        int needed_verts = first + count;
        int needed_floats = needed_verts * pending_fixed_color_size;
        if (needed_floats > fixed_color_buf_cap) {
            fixed_color_buf = (GLfloat *)realloc(fixed_color_buf, needed_floats * sizeof(GLfloat));
            fixed_color_buf_cap = needed_floats;
        }
        int stride_elems = pending_fixed_color_stride > 0 ? pending_fixed_color_stride / sizeof(int32_t) : pending_fixed_color_size;
        for (int i = 0; i < needed_verts; i++) {
            const int32_t *src = pending_fixed_colors + i * stride_elems;
            GLfloat *dst = fixed_color_buf + i * pending_fixed_color_size;
            for (int c = 0; c < pending_fixed_color_size; c++) {
                dst[c] = src[c] / 65536.0f;
            }
        }
        glColorPointer(pending_fixed_color_size, GL_FLOAT, 0, fixed_color_buf);
    }

    if (pending_fixed_texcoords) {
        int needed_verts = first + count;
        int needed_floats = needed_verts * pending_fixed_texcoord_size;
        if (needed_floats > fixed_texcoord_buf_cap) {
            fixed_texcoord_buf = (GLfloat *)realloc(fixed_texcoord_buf, needed_floats * sizeof(GLfloat));
            fixed_texcoord_buf_cap = needed_floats;
        }
        int stride_elems = pending_fixed_texcoord_stride > 0 ? pending_fixed_texcoord_stride / sizeof(int32_t) : pending_fixed_texcoord_size;
        for (int i = 0; i < needed_verts; i++) {
            const int32_t *src = pending_fixed_texcoords + i * stride_elems;
            GLfloat *dst = fixed_texcoord_buf + i * pending_fixed_texcoord_size;
            for (int c = 0; c < pending_fixed_texcoord_size; c++) {
                dst[c] = src[c] / 65536.0f;
            }
        }
        glTexCoordPointer(pending_fixed_texcoord_size, GL_FLOAT, 0, fixed_texcoord_buf);
    }

    glDrawArrays(mode, first, count);
}

// Sin limite de log hasta ahora -- el motor llama esto por cada
// sprite/textura dibujada, TODOS los frames (a diferencia de sus vecinos en
// este archivo, que ya capan a las primeras ~10-20 llamadas). Cada
// game_log() hace fprintf+fflush sincronico (I/O bloqueante a disco, mismo
// costo que ENABLE_VERBOSE_JNI_LOG documenta en CLAUDE.md) -- sin este cap
// era un fflush por sprite dibujado, suficiente para tirar el framerate de
// 60 a ~13 fps sostenidos en consola real.
void glTexEnvf_wrapper(GLenum target, GLenum pname, GLfloat param) {
    static int log = 0;
    if (log < 10) {
        game_log("[GL] glTexEnvf target=%x pname=%x param=%f ui_status=%d\n", target, pname, param, g_ui_status);
        log++;
    }
    glTexEnvf(target, pname, param);
}

// Sin wrapper hasta ahora (pasaba directo a vitaGL) -- se agrega SOLO para
// loguear durante logo/titulo/menu, ver nota de zenonia_verbose_ui() arriba.
void glBlendFunc_wrapper(GLenum sfactor, GLenum dfactor) {
    if (zenonia_verbose_ui()) {
        game_log("[GL] glBlendFunc sfactor=%x dfactor=%x ui_status=%d\n", sfactor, dfactor, g_ui_status);
    }
    glBlendFunc(sfactor, dfactor);
}

void glEnable_wrapper(GLenum cap) {
    static int enable_log = 0;
    if (enable_log < 20) {
        game_log("[GL] glEnable cap=%x\n", cap);
        enable_log++;
    }
    glEnable(cap);
}

void glDisable_wrapper(GLenum cap) {
    static int disable_log = 0;
    if (disable_log < 20) {
        game_log("[GL] glDisable cap=%x\n", cap);
        disable_log++;
    }
    glDisable(cap);
}

void glTexCoordPointer_wrapper(GLint size, GLenum type, GLsizei stride, const void *pointer) {
    static int log = 0;
    if (log < 10) {
        game_log("[GL] glTexCoordPointer size=%d type=%x stride=%d pointer=%p\n", size, type, stride, pointer);
        log++;
    }
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    static int log_uvs = 0;
    if (pointer && log_uvs < 20) {
        if (type == GL_FIXED) {
            int32_t *uv = (int32_t *)pointer;
            game_log("  -> UVs Fixed (first 6): (%.2f, %.2f) (%.2f, %.2f) (%.2f, %.2f)\n",
                     uv[0]/65536.0f, uv[1]/65536.0f, uv[2]/65536.0f, uv[3]/65536.0f, uv[4]/65536.0f, uv[5]/65536.0f);
        } else {
            float *uv = (float *)pointer;
            game_log("  -> UVs Float (first 6): (%.2f, %.2f) (%.2f, %.2f) (%.2f, %.2f)\n",
                     uv[0], uv[1], uv[2], uv[3], uv[4], uv[5]);
        }
        log_uvs++;
    }

    if (type == GL_FIXED) {
        pending_fixed_texcoords = (const int32_t *)pointer;
        pending_fixed_texcoord_size = size;
        pending_fixed_texcoord_stride = stride;
        return;
    }

    pending_fixed_texcoords = NULL;
    glTexCoordPointer(size, type, stride, pointer);
}

void glColorPointer_wrapper(GLint size, GLenum type, GLsizei stride, const void *pointer) {
    static int log = 0;
    if (log < 10) {
        game_log("[GL] glColorPointer size=%d type=%x stride=%d pointer=%p\n", size, type, stride, pointer);
        log++;
    }
    glEnableClientState(GL_COLOR_ARRAY);
    static int log_colors = 0;
    // Sin tope mientras dure logo/titulo/menu (ver zenonia_verbose_ui()) --
    // el alfa de este color es la pista clave para confirmar/descartar la
    // hipotesis del fade pegado en DrawZeroGrade/DrawTeamLogo.
    if (pointer && (log_colors < 20 || zenonia_verbose_ui())) {
        if (type == GL_FIXED) {
            int32_t *c = (int32_t *)pointer;
            game_log("  -> Colors Fixed (first 4): (%d, %d, %d, %d) ui_status=%d\n", c[0], c[1], c[2], c[3], g_ui_status);
        } else if (type == GL_UNSIGNED_BYTE) {
            uint8_t *c = (uint8_t *)pointer;
            game_log("  -> Colors UByte (first 4): (%d, %d, %d, %d) ui_status=%d\n", c[0], c[1], c[2], c[3], g_ui_status);
        } else {
            float *c = (float *)pointer;
            game_log("  -> Colors Float (first 4): (%.2f, %.2f, %.2f, %.2f) ui_status=%d\n", c[0], c[1], c[2], c[3], g_ui_status);
        }
        log_colors++;
    }

    if (type == GL_FIXED) {
        pending_fixed_colors = (const int32_t *)pointer;
        pending_fixed_color_size = size;
        pending_fixed_color_stride = stride;
        return;
    }

    pending_fixed_colors = NULL;
    glColorPointer(size, type, stride, pointer);
}

void glVertexPointer_wrapper(GLint size, GLenum type, GLsizei stride, const void *pointer) {
    static int log = 0;
    if (log < 10) {
        game_log("[GL] glVertexPointer size=%d type=%x stride=%d pointer=%p\n", size, type, stride, pointer);
        log++;
    }
    glEnableClientState(GL_VERTEX_ARRAY);
    static int log_verts = 0;
    if (pointer && log_verts < 20) {
        if (type == GL_FIXED) {
            int32_t *v = (int32_t *)pointer;
            game_log("  -> Verts Fixed (first 6): (%d, %d) (%d, %d) (%d, %d)\n",
                     v[0], v[1], v[2], v[3], v[4], v[5]);
        } else {
            float *v = (float *)pointer;
            game_log("  -> Verts Float (first 6): (%.2f, %.2f) (%.2f, %.2f) (%.2f, %.2f)\n",
                     v[0], v[1], v[2], v[3], v[4], v[5]);
        }
        log_verts++;
    }

    if (type == GL_FIXED) {
        pending_fixed_verts = (const int32_t *)pointer;
        pending_fixed_size = size;
        pending_fixed_stride = stride;
        return;
    }

    pending_fixed_verts = NULL;
    glVertexPointer(size, type, stride, pointer);
}

void glEnableClientState_wrapper(GLenum array) {
    static int enable_cs_log = 0;
    if (enable_cs_log < 10) {
        game_log("[GL] glEnableClientState array=%x\n", array);
        enable_cs_log++;
    }
    glEnableClientState(array);
}

void glDisableClientState_wrapper(GLenum array) {
    static int disable_cs_log = 0;
    if (disable_cs_log < 10) {
        game_log("[GL] glDisableClientState array=%x\n", array);
        disable_cs_log++;
    }
    glDisableClientState(array);
}

void glMatrixMode_wrapper(GLenum mode) {
    static int log = 0;
    if (log < 10) {
        game_log("[GL] glMatrixMode mode=%x\n", mode);
        log++;
    }
    glMatrixMode(mode);
}

void glLoadIdentity_wrapper() {
    static int log = 0;
    if (log < 10) {
        game_log("[GL] glLoadIdentity\n");
        log++;
    }
    glLoadIdentity();
}



void glViewport_wrapper(GLint x, GLint y, GLsizei width, GLsizei height) {
    static int log = 0;
    if (log < 10) {
        game_log("[GL] glViewport x=%d y=%d w=%d h=%d (Forcing 960x544)\n", x, y, width, height);
        log++;
    }
    glViewport(0, 0, 960, 544);
}

// Pass-through deliberado: el motor manda una proyeccion CENTRADA
// (-400..400, -240..240) y sus vertices de quad de pantalla completa usan ese
// mismo sistema (GL_FIXED -400..400/-240..240, confirmado en los logs).
// Forzar aqui un sistema con origen en la esquina (0..800, 480..0) desplaza
// ese quad a un solo cuadrante de la pantalla e invierte Y — regresion real
// vista en consola (cuadro chico blanco sobre negro, log_1783657108).
void glOrthof_wrapper(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar) {
    static int log = 0;
    if (log < 10) {
        game_log("[GL] glOrthof left=%.2f right=%.2f bottom=%.2f top=%.2f\n", left, right, bottom, top);
        log++;
    }
    glOrthof(left, right, bottom, top, zNear, zFar);
}

void glOrthox_wrapper(GLint left, GLint right, GLint bottom, GLint top, GLint zNear, GLint zFar) {
    static int log = 0;
    if (log < 10) {
        game_log("[GL] glOrthox left=%d right=%d bottom=%d top=%d\n", left, right, bottom, top);
        log++;
    }
    // Convert from fixed point (Q16.16) to float
    glOrthof_wrapper(left / 65536.0f, right / 65536.0f, bottom / 65536.0f, top / 65536.0f, zNear / 65536.0f, zFar / 65536.0f);
}


// Stubs de C++/GCC
void __cxa_begin_cleanup() {}
void __cxa_call_unexpected() {}
int __cxa_guard_acquire(int* g) { return !*(char*)(g); }
void __cxa_guard_release(int* g) { *(char*)g = 1; }
void __cxa_type_match() {}
void __gnu_Unwind_Find_exidx() {}
void __stack_chk_fail() {}

// Registro de destructores estaticos de C++ (llamados normalmente al
// dlclose()/exit() de una libreria dinamica real). Este loader nunca
// descarga el .so ni llama exit() de forma ordenada -- el proceso termina
// con sceKernelExitProcess() -- asi que no hace falta ejecutar los
// destructores registrados: alcanza con no-ops que devuelvan éxito. Faltaban
// en la tabla de imports pese a estar confirmados en la Fase 1 del plan
// (objdump -T | grep UND), causando "Unknown symbol" y crash real en
// consola (ver port_progress.md).
int __cxa_atexit(void (*func)(void *), void *arg, void *dso_handle) {
    (void) func; (void) arg; (void) dso_handle;
    return 0;
}
void __cxa_finalize(void *dso_handle) {
    (void) dso_handle;
}
int __aeabi_atexit(void *arg, void (*func)(void *), void *dso_handle) {
    return __cxa_atexit(func, arg, dso_handle);
}
int __stack_chk_guard = 0;

// pthread_mutex_t/pthread_cond_t en VitaSDK son en realidad PUNTEROS
// (typedef struct pthread_mutex_t_ * pthread_mutex_t;) a una estructura
// interna que aloca pthread_mutex_init(). El .so esta compilado contra
// Bionic (Android), donde un mutex/condvar estatico declarado con
// PTHREAD_MUTEX_INITIALIZER/PTHREAD_COND_INITIALIZER queda en CEROS sin
// llamar nunca a pthread_mutex_init en tiempo de ejecucion -- Bionic esta
// disenado para tratar esos ceros como "mutex valido, sin lockear" de
// forma nativa. En VitaSDK esos mismos ceros son un puntero NULL real, y
// pthread_mutex_lock/unlock de PTE (pthreads-embedded) lo desreferencian
// sin chequear, crasheando (confirmado en consola real: Data abort dentro
// de pthread_mutex_unlock, llamado desde el node-allocator interno de
// libstdc++ sobre un mutex estatico nunca inicializado -- ver
// port_progress.md). Se detecta el puntero NULL y se inicializa on-demand
// antes de usarlo, en vez de pasar el valor crudo a la implementacion real.
int pthread_mutex_lock_wrapper(pthread_mutex_t *mutex) {
    if (mutex && (*mutex == NULL || (intptr_t)*mutex == 0x4000)) {
        *mutex = NULL;
        pthread_mutex_init(mutex, NULL);
    }
    return pthread_mutex_lock(mutex);
}
int pthread_mutex_unlock_wrapper(pthread_mutex_t *mutex) {
    if (mutex && (*mutex == NULL || (intptr_t)*mutex == 0x4000)) {
        *mutex = NULL;
        pthread_mutex_init(mutex, NULL);
    }
    return pthread_mutex_unlock(mutex);
}
int pthread_mutex_destroy_wrapper(pthread_mutex_t *mutex) {
    if (!mutex || *mutex == NULL || (intptr_t)*mutex == 0x4000) return 0; // nunca se inicializo, nada que destruir
    return pthread_mutex_destroy(mutex);
}
int pthread_cond_wait_wrapper(pthread_cond_t *cond, pthread_mutex_t *mutex) {
    if (cond && (*cond == NULL || (intptr_t)*cond == 0x4000)) {
        *cond = NULL;
        pthread_cond_init(cond, NULL);
    }
    if (mutex && (*mutex == NULL || (intptr_t)*mutex == 0x4000)) {
        *mutex = NULL;
        pthread_mutex_init(mutex, NULL);
    }
    return pthread_cond_wait(cond, mutex);
}
int pthread_cond_broadcast_wrapper(pthread_cond_t *cond) {
    if (cond && (*cond == NULL || (intptr_t)*cond == 0x4000)) {
        *cond = NULL;
        pthread_cond_init(cond, NULL);
    }
    return pthread_cond_broadcast(cond);
}

void* malloc_wrapper(size_t size) {
    void* ptr = malloc(size);
    if (!ptr) {
        game_log("[FakeJNI] MALLOC FAILED FOR SIZE %u\n", (unsigned int)size);
    }
    return ptr;
}
void free_wrapper(void* ptr) {
    free(ptr);
}
void* calloc_wrapper(size_t n, size_t size) {
    return calloc(n, size);
}
void* realloc_wrapper(void* ptr, size_t size) {
    return realloc(ptr, size);
}

void translate_path(const char* in_path, char* out_path, size_t out_size) {
    if (strncmp(in_path, "ux0:", 4) == 0) {
        strncpy(out_path, in_path, out_size);
        return;
    }
    const char* relative = in_path;
    if (strncmp(in_path, "app0:/", 6) == 0) {
        relative += 6;
    }
    while (*relative == '/') {
        relative++;
    }
    snprintf(out_path, out_size, "ux0:data/zenonia3/assets/%s", relative);
}

FILE* fopen_hook(const char* path, const char* mode) {
    char new_path[256];
    translate_path(path, new_path, sizeof(new_path));
    game_log("[FakeJNI] fopen_hook: %s -> %s\n", path, new_path);
    return fopen(new_path, mode);
}

// struct stat con el layout de bionic (Android ARM 32-bit, NDK android-9) --
// NO es el de newlib/vitasdk. El motor lee directamente st_mode en el offset
// 16 y st_size en el 48 (confirmado en Zenonia 2 desensamblando
// MC_fsFileAttribute -- mismo motor y mismo consumidor aqui, ver
// out_ghidra.c:150281: los offsets que Ghidra decompila para el stat local
// coinciden con bionic). Pasarle el struct stat de newlib deja esos offsets
// con basura de stack: en Zenonia 2, al cargar una partida guardada, el
// "tamano" leido era un puntero del heap (MALLOC FAILED FOR SIZE 0x81340CE0)
// y el motor crasheaba -- fix portado ANTES de que el sintoma aparezca aqui
// (Zenonia2 port_progress.md §12.4).
typedef struct {
    uint64_t st_dev;         // 0
    uint8_t  __pad0[4];      // 8
    uint32_t __st_ino;       // 12
    uint32_t st_mode;        // 16  <- leido por el motor
    uint32_t st_nlink;       // 20
    uint32_t st_uid;         // 24
    uint32_t st_gid;         // 28
    uint64_t st_rdev;        // 32
    uint8_t  __pad3[4];      // 40 (+4 de alineacion implicita)
    int64_t  st_size;        // 48  <- leido por el motor
    uint32_t st_blksize;     // 56 (+4 de alineacion implicita)
    uint64_t st_blocks;      // 64
    uint32_t st_atime;       // 72
    uint32_t st_atime_nsec;  // 76
    uint32_t st_mtime;       // 80
    uint32_t st_mtime_nsec;  // 84
    uint32_t st_ctime;       // 88
    uint32_t st_ctime_nsec;  // 92
    uint64_t st_ino;         // 96
} bionic_stat_t;             // 104 bytes (el motor reserva espacio de sobra)

_Static_assert(__builtin_offsetof(bionic_stat_t, st_mode) == 16, "bionic st_mode");
_Static_assert(__builtin_offsetof(bionic_stat_t, st_size) == 48, "bionic st_size");

int stat_hook(const char* path, void* statbuf) {
    char new_path[256];
    translate_path(path, new_path, sizeof(new_path));

    struct stat st;
    int res = stat(new_path, &st);
    game_log("[FakeJNI] stat_hook: %s -> %s = %d (size=%ld)\n", path, new_path, res, res == 0 ? (long) st.st_size : -1L);
    if (res == 0 && statbuf) {
        bionic_stat_t *bst = (bionic_stat_t *) statbuf;
        memset(bst, 0, sizeof(*bst));
        bst->st_mode = st.st_mode; // los bits S_IFDIR/permisos son POSIX, coinciden
        bst->st_nlink = st.st_nlink;
        bst->st_uid = st.st_uid;
        bst->st_gid = st.st_gid;
        bst->st_size = st.st_size;
        bst->st_blksize = st.st_blksize;
        bst->st_blocks = st.st_blocks;
        bst->st_atime = st.st_atime;
        bst->st_mtime = st.st_mtime;
        bst->st_ctime = st.st_ctime;
        bst->__st_ino = st.st_ino;
        bst->st_ino = st.st_ino;
    }
    return res;
}

int access_hook(const char* path, int amode) {
    char new_path[256];
    translate_path(path, new_path, sizeof(new_path));
    game_log("[FakeJNI] access_hook: %s -> %s\n", path, new_path);
    return access(new_path, amode);
}

// Tabla de resolucion de imports. Confirmada 1:1 contra
// `objdump -T libgameDSO.so | grep UND` (92 simbolos, ver Fase 1 del plan):
// este motor no importa __android_log_print en este build (a diferencia de
// Zenonia 2), asi que no se registra -- si aparece en un build futuro
// alcanza con agregar la entrada, la funcion ya existe en main.c.
so_default_dynlib default_dynlib[] = {
    // C++ ABI / GCC
    { "__cxa_atexit", (uintptr_t)&__cxa_atexit },
    { "__cxa_finalize", (uintptr_t)&__cxa_finalize },
    { "__aeabi_atexit", (uintptr_t)&__aeabi_atexit },
    { "__cxa_begin_cleanup", (uintptr_t)&__cxa_begin_cleanup },
    { "__cxa_call_unexpected", (uintptr_t)&__cxa_call_unexpected },
    { "__cxa_guard_acquire", (uintptr_t)&__cxa_guard_acquire },
    { "__cxa_guard_release", (uintptr_t)&__cxa_guard_release },
    { "__cxa_type_match", (uintptr_t)&__cxa_type_match },
    { "__gnu_Unwind_Find_exidx", (uintptr_t)&__gnu_Unwind_Find_exidx },
    { "__stack_chk_fail", (uintptr_t)&__stack_chk_fail },
    { "__stack_chk_guard", (uintptr_t)&__stack_chk_guard },

    // Libc - Memoria
    { "malloc", (uintptr_t)&malloc_wrapper },
    { "free", (uintptr_t)&free_wrapper },
    { "calloc", (uintptr_t)&calloc_wrapper },
    { "realloc", (uintptr_t)&realloc_wrapper },
    { "memcpy", (uintptr_t)&memcpy },
    { "memset", (uintptr_t)&memset },
    { "memmove", (uintptr_t)&memmove },
    { "memcmp", (uintptr_t)&memcmp },

    // Libc - String
    { "strcpy", (uintptr_t)&strcpy },
    { "strncpy", (uintptr_t)&strncpy },
    { "strcmp", (uintptr_t)&strcmp },
    { "strncmp", (uintptr_t)&strncmp },
    { "strcat", (uintptr_t)&strcat },
    { "strncat", (uintptr_t)&strncat },
    { "strlen", (uintptr_t)&strlen },
    { "strstr", (uintptr_t)&strstr },
    { "strchr", (uintptr_t)&strchr },

    // Libc - I/O
    { "fopen", (uintptr_t)&fopen_hook },
    { "fread", (uintptr_t)&fread },
    { "fwrite", (uintptr_t)&fwrite },
    { "fclose", (uintptr_t)&fclose },
    { "fseek", (uintptr_t)&fseek },
    { "ftell", (uintptr_t)&ftell },
    { "printf", (uintptr_t)&printf },
    { "vprintf", (uintptr_t)&vprintf },
    { "vsprintf", (uintptr_t)&vsprintf },
    { "sprintf", (uintptr_t)&sprintf },
    { "putchar", (uintptr_t)&putchar },
    { "puts", (uintptr_t)&puts },

    // Libc - Filesystem
    { "access", (uintptr_t)&access_hook },
    { "stat", (uintptr_t)&stat_hook },
    { "unlink", (uintptr_t)&unlink },
    { "rename", (uintptr_t)&rename },
    { "close", (uintptr_t)&close },
    { "fcntl", (uintptr_t)&fcntl },
    { "select", (uintptr_t)&select },

    // Libc - Tiempo y matematica
    { "time", (uintptr_t)&time },
    { "gettimeofday", (uintptr_t)&gettimeofday },
    { "localtime", (uintptr_t)&localtime },
    { "ceil", (uintptr_t)&ceil },
    { "atoi", (uintptr_t)&atoi },
    { "abort", (uintptr_t)&abort },
    { "exit", (uintptr_t)&exit },
    { "raise", (uintptr_t)&raise },

    // pthread (usado internamente por FalsoJNI y por el motor)
    { "pthread_mutex_init", (uintptr_t)&pthread_mutex_init },
    { "pthread_mutex_lock", (uintptr_t)&pthread_mutex_lock_wrapper },
    { "pthread_mutex_unlock", (uintptr_t)&pthread_mutex_unlock_wrapper },
    { "pthread_mutex_destroy", (uintptr_t)&pthread_mutex_destroy_wrapper },
    { "pthread_cond_wait", (uintptr_t)&pthread_cond_wait_wrapper },
    { "pthread_cond_broadcast", (uintptr_t)&pthread_cond_broadcast_wrapper },
    { "pthread_key_create", (uintptr_t)&pthread_key_create },
    { "pthread_getspecific", (uintptr_t)&pthread_getspecific },
    { "pthread_setspecific", (uintptr_t)&pthread_setspecific },

    // OpenGL ES 1.1 (mapeado a vitaGL)
    { "glActiveTexture", (uintptr_t)&glActiveTexture },
    { "glBindTexture", (uintptr_t)&glBindTexture },
    { "glClear", (uintptr_t)&glClear },
    { "glClearColorx", (uintptr_t)&glClearColorx_wrapper },
    { "glColorPointer", (uintptr_t)&glColorPointer_wrapper },
    { "glColor4f", (uintptr_t)&glColor4f },
    { "glColor4x", (uintptr_t)&glClearColorx_wrapper }, /* using same logic as glClearColorx for now */
    { "glDisable", (uintptr_t)&glDisable_wrapper },
    { "glDisableClientState", (uintptr_t)&glDisableClientState_wrapper },
    { "glDrawArrays", (uintptr_t)&glDrawArrays_wrapper },
    { "glEnable", (uintptr_t)&glEnable_wrapper },
    { "glEnableClientState", (uintptr_t)&glEnableClientState_wrapper },
    { "glBlendFunc", (uintptr_t)&glBlendFunc_wrapper },
    { "glAlphaFunc", (uintptr_t)&glAlphaFunc },
    { "glGenTextures", (uintptr_t)&glGenTextures },
    { "glHint", (uintptr_t)&glHint },
    { "glLoadIdentity", (uintptr_t)&glLoadIdentity_wrapper },
    { "glMatrixMode", (uintptr_t)&glMatrixMode_wrapper },
    { "glNormalPointer", (uintptr_t)&glNormalPointer },
    { "glOrthof", (uintptr_t)&glOrthof_wrapper },
    { "glOrthox", (uintptr_t)&glOrthox_wrapper },
    { "glTexCoordPointer", (uintptr_t)&glTexCoordPointer_wrapper },
    { "glTexEnvf", (uintptr_t)&glTexEnvf_wrapper },
    { "glTexImage2D", (uintptr_t)&glTexImage2D_wrapper },
    { "glTexSubImage2D", (uintptr_t)&glTexSubImage2D_wrapper },
    { "glTexParameterx", (uintptr_t)&glTexParameterx_wrapper },
    { "glVertexPointer", (uintptr_t)&glVertexPointer_wrapper },
    { "glViewport", (uintptr_t)&glViewport_wrapper },

    // Red / Sockets
    { "socket", (uintptr_t)&socket },
    { "connect", (uintptr_t)&connect },
    { "send", (uintptr_t)&send },
    { "recv", (uintptr_t)&recv },
    { "shutdown", (uintptr_t)&shutdown },
    { "inet_addr", (uintptr_t)&inet_addr },

    // Libc core
    { "__errno", (uintptr_t)&__errno },
};

int default_dynlib_size = sizeof(default_dynlib);
