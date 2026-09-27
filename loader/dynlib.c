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

#if defined(OPTIMIZE_NEON_FIXED) || defined(RGB565_MODE_NEON)
#include <arm_neon.h>
#endif

extern void game_log(const char *fmt, ...);
extern volatile int g_ui_status;

/**
 * @brief The logo/title/menu (ui_status 0-2, see ZenoniaUIControllerView.java: UI_STATUS_LOGO/TITLE/MAINMENU) looks white and then black (confirmed).
 */
static int zenonia_verbose_ui(void) {
    return g_ui_status >= 0 && g_ui_status <= 2;
}

/**
 * @brief offset committed in GVUIController::GVUIController() (out_ghidra.c:26369, `memset(this+8,0,400); *(int*)(this+0x19c)=0;`) and reused.
 */
#ifdef ZENONIA_HIDE_DPAD_UI
static so_hook g_player_controller_ctor_hook;

/**
 * @brief offset committed in GVUIController::GVUIController() (out_ghidra.c:26369, `memset(this+8,0,400); *(int*)(this+0x19c)=0;`) and reused.
 */
#define GVUI_CONTROLLER_ACTIVE_COUNT_OFFSET 0x19c

/**
 * @brief Instance + real (pre-zeroed) active-object count saved by the one-shot
 * ctor hook below, so zenonia_toggle_dpad_visibility() can flip the same field
 * back and forth at runtime (L+R) instead of only ever zeroing it once at boot.
 */
static void *g_player_controller_instance = NULL;
static int g_player_controller_real_active_count = 0;
static volatile int g_dpad_hidden = 1;

static void GVUIPlayerController_ctor_hook(void *this) {
/**
 * @brief Call AFTER so_relocate/so_resolve (you need the module already with dynsym/dynstr resolved, see so_symbol()) and BEFORE the first call to.
 */
    so_unhook(&g_player_controller_ctor_hook);
    ((void (*)(void *)) g_player_controller_ctor_hook.thumb_addr)(this);
    g_player_controller_instance = this;
    g_player_controller_real_active_count = *(int *)((char *) this + GVUI_CONTROLLER_ACTIVE_COUNT_OFFSET);
    *(int *)((char *) this + GVUI_CONTROLLER_ACTIVE_COUNT_OFFSET) = 0;
    game_log("[HideDpad] GVUIPlayerController construido en %p -- contador de objetos activos puesto a 0 (Draw/touch de la cruceta y los 5 botones de accion deshabilitados)\n", this);
}
#endif

/**
 * @brief Toggles the dpad/action-button cluster's active-object count between
 * 0 (hidden) and its real value (visible), so Draw()/PointerPress() see it as
 * empty or full without ever re-running the constructor. No-op if
 * HIDE_VIRTUAL_GAMEPAD was OFF at compile time (no hook installed, cluster was
 * never hidden) or the instance hasn't been constructed yet.
 */
void zenonia_toggle_dpad_visibility(void) {
#ifdef ZENONIA_HIDE_DPAD_UI
    if (!g_player_controller_instance) return;
    g_dpad_hidden = !g_dpad_hidden;
    *(int *)((char *) g_player_controller_instance + GVUI_CONTROLLER_ACTIVE_COUNT_OFFSET) =
        g_dpad_hidden ? 0 : g_player_controller_real_active_count;
    game_log("[HideDpad] L+R -> cruceta/botones %s\n", g_dpad_hidden ? "ocultos" : "visibles");
#endif
}

/**
 * @brief Call AFTER so_relocate/so_resolve (you need the module already with dynsym/dynstr resolved, see so_symbol()) and BEFORE the first call to.
 */
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

/**
 * @brief Wrappers for fixed point OpenGL (GLES1).
 */
void glClearColorx_wrapper(int r, int g, int b, int a) {
    glClearColor(r / 65536.0f, g / 65536.0f, b / 65536.0f, a / 65536.0f);
}

void glTexParameterx_wrapper(GLenum target, GLenum pname, int param) {
    glTexParameteri(target, pname, param);
}

/**
 * @brief 4-strategy A/B testing for the RGB565 software framebuffer that the motor uploads per texture (see CLAUDE.md / CMakeLists.txt).
 */
#if defined(RGB565_MODE_NEON)
static void convert_rgb565_to_rgba8888_neon(const uint16_t *src, uint8_t *dst, int npix) {
    int i = 0;
    for (; i + 8 <= npix; i += 8) {
        uint16x8_t p = vld1q_u16(src + i);
        uint16x8_t r5 = vshrq_n_u16(p, 11);
        uint16x8_t g6 = vandq_u16(vshrq_n_u16(p, 5), vdupq_n_u16(0x3F));
        uint16x8_t b5 = vandq_u16(p, vdupq_n_u16(0x1F));
/**< @brief Wrappers for fixed point OpenGL (GLES1). */
        uint8x8_t r8 = vmovn_u16(vorrq_u16(vshlq_n_u16(r5, 3), vshrq_n_u16(r5, 2)));
        uint8x8_t g8 = vmovn_u16(vorrq_u16(vshlq_n_u16(g6, 2), vshrq_n_u16(g6, 4)));
        uint8x8_t b8 = vmovn_u16(vorrq_u16(vshlq_n_u16(b5, 3), vshrq_n_u16(b5, 2)));
        uint8x8x4_t out = {{ r8, g8, b8, vdup_n_u8(255) }};
        vst4_u8(dst + i * 4, out); // interleaved R,G,B,A -- mismo layout que dst[i*4+0..3]
    }
    for (; i < npix; i++) {
        uint16_t p = src[i];
        uint8_t r5 = (p >> 11) & 0x1F, g6 = (p >> 5) & 0x3F, b5 = p & 0x1F;
        dst[i*4 + 0] = (r5 << 3) | (r5 >> 2);
        dst[i*4 + 1] = (g6 << 2) | (g6 >> 4);
        dst[i*4 + 2] = (b5 << 3) | (b5 >> 2);
        dst[i*4 + 3] = 255;
    }
}
#endif

/**
 * @brief Conversion buffer reused between calls.
 */
void *convert_rgb565_to_rgba8888(const void *pixels, int width, int height) {
    static uint8_t *conv_buf = NULL;
    static int conv_buf_cap = 0;

    if (!pixels) return NULL;
    uint16_t *src = (uint16_t *)pixels;
    int npix = width * height;
    if (npix * 4 > conv_buf_cap) {
/**< @brief Conversion buffer reused between calls. */
        uint8_t *new_buf = (uint8_t *)realloc(conv_buf, npix * 4);
        if (!new_buf) {
            game_log("[GL] convert_rgb565_to_rgba8888: realloc fallo para %d bytes\n", npix * 4);
            return NULL;
        }
        conv_buf = new_buf;
        conv_buf_cap = npix * 4;
    }
    uint8_t *dst = conv_buf;

#if defined(RGB565_MODE_LUT)
/**< @brief Conversion buffer reused between calls. */
    static uint8_t r5_to_8[32], g6_to_8[64], b5_to_8[32];
    static int lut_ready = 0;
    if (!lut_ready) {
        for (int i = 0; i < 32; i++) r5_to_8[i] = i * 255 / 31;
        for (int i = 0; i < 64; i++) g6_to_8[i] = i * 255 / 63;
        for (int i = 0; i < 32; i++) b5_to_8[i] = i * 255 / 31;
        lut_ready = 1;
    }
    for (int i = 0; i < npix; i++) {
        uint16_t p = src[i];
        dst[i*4 + 0] = r5_to_8[(p >> 11) & 0x1F];
        dst[i*4 + 1] = g6_to_8[(p >> 5) & 0x3F];
        dst[i*4 + 2] = b5_to_8[p & 0x1F];
        dst[i*4 + 3] = 255;
    }
#elif defined(RGB565_MODE_NEON)
    convert_rgb565_to_rgba8888_neon(src, dst, npix);
#else // RGB565_MODE_SCALAR (default/baseline)
    for (int i = 0; i < npix; i++) {
        uint16_t p = src[i];
        dst[i*4 + 0] = ((p >> 11) & 0x1F) * 255 / 31; // R
        dst[i*4 + 1] = ((p >> 5) & 0x3F) * 255 / 63;  // G
        dst[i*4 + 2] = (p & 0x1F) * 255 / 31;         // B
        dst[i*4 + 3] = 255;                           // A
    }
#endif

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

/**
 * @brief The engine uploads its internal software framebuffer (400x240, see Phase 1 of the plan) in RGB565.
 */
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
#if defined(RGB565_MODE_NATIVE)
        glTexImage2D(target, level, GL_RGB, width, height, border, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, pixels);
#else
        void *new_pixels = convert_rgb565_to_rgba8888(pixels, width, height);
        glTexImage2D(target, level, GL_RGBA, width, height, border, GL_RGBA, GL_UNSIGNED_BYTE, new_pixels);
#endif
    } else {
        glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
    }

/**
 * @brief Force min/mag filters so that the texture is not treated as incomplete due to lack of mipmaps.
 */
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
#if defined(RGB565_MODE_NATIVE)
        glTexSubImage2D(target, level, xoffset, yoffset, width, height, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, pixels);
#else
        void *new_pixels = convert_rgb565_to_rgba8888(pixels, width, height);
        glTexSubImage2D(target, level, xoffset, yoffset, width, height, GL_RGBA, GL_UNSIGNED_BYTE, new_pixels);
#endif
    } else {
        glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
    }

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) game_log("[GL] glTexSubImage2D ERROR: %x\n", err);
}

/**
 * @brief Force min/mag filters so that the texture is not treated as incomplete due to lack of mipmaps.
 */
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

#ifdef OPTIMIZE_NEON_FIXED
/**
 * @brief A/B test against the scalar loop (see CLAUDE.md / CMakeLists.txt: OPTIMIZE_NEON_FIXED). Only valid when the array is tightly-packed.
 */
static void fixed_to_float_neon(const int32_t *src, GLfloat *dst, int total_elems) {
    int i = 0;
    float32x4_t scale = vdupq_n_f32(1.0f / 65536.0f);
    for (; i + 4 <= total_elems; i += 4) {
        int32x4_t v = vld1q_s32(src + i);
        float32x4_t f = vcvtq_f32_s32(v);
        vst1q_f32(dst + i, vmulq_f32(f, scale));
    }
    for (; i < total_elems; i++) {
        dst[i] = src[i] / 65536.0f;
    }
}
#endif

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
/**
 * @brief A/B test against the scalar loop (see CLAUDE.md / CMakeLists.txt: OPTIMIZE_NEON_FIXED). Only valid when the array is tightly-packed.
 */
            GLfloat *new_buf = (GLfloat *)realloc(fixed_vert_buf, needed_floats * sizeof(GLfloat));
            if (!new_buf) {
                game_log("[GL] glDrawArrays: realloc de fixed_vert_buf fallo (%d floats)\n", needed_floats);
                return;
            }
            fixed_vert_buf = new_buf;
            fixed_vert_buf_cap = needed_floats;
        }
        int stride_elems = pending_fixed_stride > 0 ? pending_fixed_stride / sizeof(int32_t) : pending_fixed_size;
#ifdef OPTIMIZE_NEON_FIXED
        if (stride_elems == pending_fixed_size) {
            fixed_to_float_neon(pending_fixed_verts, fixed_vert_buf, needed_verts * pending_fixed_size);
        } else
#endif
        {
            for (int i = 0; i < needed_verts; i++) {
                const int32_t *src = pending_fixed_verts + i * stride_elems;
                GLfloat *dst = fixed_vert_buf + i * pending_fixed_size;
                for (int c = 0; c < pending_fixed_size; c++) {
                    dst[c] = src[c] / 65536.0f;
                }
            }
        }
        glVertexPointer(pending_fixed_size, GL_FLOAT, 0, fixed_vert_buf);
    }

    if (pending_fixed_colors) {
        int needed_verts = first + count;
        int needed_floats = needed_verts * pending_fixed_color_size;
        if (needed_floats > fixed_color_buf_cap) {
            GLfloat *new_buf = (GLfloat *)realloc(fixed_color_buf, needed_floats * sizeof(GLfloat));
            if (!new_buf) {
                game_log("[GL] glDrawArrays: realloc de fixed_color_buf fallo (%d floats)\n", needed_floats);
                return;
            }
            fixed_color_buf = new_buf;
            fixed_color_buf_cap = needed_floats;
        }
        int stride_elems = pending_fixed_color_stride > 0 ? pending_fixed_color_stride / sizeof(int32_t) : pending_fixed_color_size;
#ifdef OPTIMIZE_NEON_FIXED
        if (stride_elems == pending_fixed_color_size) {
            fixed_to_float_neon(pending_fixed_colors, fixed_color_buf, needed_verts * pending_fixed_color_size);
        } else
#endif
        {
            for (int i = 0; i < needed_verts; i++) {
                const int32_t *src = pending_fixed_colors + i * stride_elems;
                GLfloat *dst = fixed_color_buf + i * pending_fixed_color_size;
                for (int c = 0; c < pending_fixed_color_size; c++) {
                    dst[c] = src[c] / 65536.0f;
                }
            }
        }
        glColorPointer(pending_fixed_color_size, GL_FLOAT, 0, fixed_color_buf);
    }

    if (pending_fixed_texcoords) {
        int needed_verts = first + count;
        int needed_floats = needed_verts * pending_fixed_texcoord_size;
        if (needed_floats > fixed_texcoord_buf_cap) {
            GLfloat *new_buf = (GLfloat *)realloc(fixed_texcoord_buf, needed_floats * sizeof(GLfloat));
            if (!new_buf) {
                game_log("[GL] glDrawArrays: realloc de fixed_texcoord_buf fallo (%d floats)\n", needed_floats);
                return;
            }
            fixed_texcoord_buf = new_buf;
            fixed_texcoord_buf_cap = needed_floats;
        }
        int stride_elems = pending_fixed_texcoord_stride > 0 ? pending_fixed_texcoord_stride / sizeof(int32_t) : pending_fixed_texcoord_size;
#ifdef OPTIMIZE_NEON_FIXED
        if (stride_elems == pending_fixed_texcoord_size) {
            fixed_to_float_neon(pending_fixed_texcoords, fixed_texcoord_buf, needed_verts * pending_fixed_texcoord_size);
        } else
#endif
        {
            for (int i = 0; i < needed_verts; i++) {
                const int32_t *src = pending_fixed_texcoords + i * stride_elems;
                GLfloat *dst = fixed_texcoord_buf + i * pending_fixed_texcoord_size;
                for (int c = 0; c < pending_fixed_texcoord_size; c++) {
                    dst[c] = src[c] / 65536.0f;
                }
            }
        }
        glTexCoordPointer(pending_fixed_texcoord_size, GL_FLOAT, 0, fixed_texcoord_buf);
    }

    glDrawArrays(mode, first, count);
}

/**
 * @brief No log limit so far.
 */
void glTexEnvf_wrapper(GLenum target, GLenum pname, GLfloat param) {
    static int log = 0;
    if (log < 10) {
        game_log("[GL] glTexEnvf target=%x pname=%x param=%f ui_status=%d\n", target, pname, param, g_ui_status);
        log++;
    }
    glTexEnvf(target, pname, param);
}

/**
 * @brief Without wrapper until now (went directly to vitaGL).
 */
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
/**
 * @brief struct stat with the bionic layout (Android ARM 32-bit, NDK android-9) -- It is NOT the one in newlib/vitasdk.
 */
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

/**
 * @brief Deliberate pass-through.
 */
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
/**
 * @brief Convert from fixed point (Q16.16) to float.
 */
    glOrthof_wrapper(left / 65536.0f, right / 65536.0f, bottom / 65536.0f, top / 65536.0f, zNear / 65536.0f, zFar / 65536.0f);
}


/**
 * @brief Stubs de C++/GCC para el shim table del juego (default_dynlib[]),
 * NO para el propio loader: el juego nunca lanza excepciones reales, asi
 * que estos no-ops le bastan. Nombrados con prefijo game_ (en vez de los
 * nombres reales __cxa_*) porque libstdc++.a real SI se enlaza en el
 * loader (vitaGL trae un preprocesador de shaders en C++) y esos simbolos
 * reales viven en el mismo objeto que __gxx_personality_v0 -- si esta
 * funcion tuviera el nombre real, el linker tira "multiple definition" en
 * cuanto libstdc++.a se extrae por __gxx_personality_v0 (eh_arm.o trae los
 * tres juntos). La tabla de abajo sigue exponiendo el nombre real por
 * string al .so cargado, que es lo unico que le importa a so_resolve().
 */
void game_cxa_begin_cleanup() {}
void game_cxa_call_unexpected() {}
int __cxa_guard_acquire(int* g) { return !*(char*)(g); }
void __cxa_guard_release(int* g) { *(char*)g = 1; }
void game_cxa_type_match() {}
void __gnu_Unwind_Find_exidx() {}
void __stack_chk_fail() {}

/**
 * @brief Registering C++ static destructors (normally called dlclose()/exit() from a real dynamic library).
 */
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

/**
 * @brief pthread_mutex_t/pthread_cond_t in VitaSDK are actually POINERS (typedef struct pthread_mutex_t_ * pthread_mutex_t;) to a structure internal.
 */
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
/**
 * @brief strncpy no garantiza terminador si in_path >= out_size (256, ver).
 */
        snprintf(out_path, out_size, "%s", in_path);
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

/**
 * @brief strncpy does not guarantee terminator if in_path >= out_size (256, see fopen_hook/stat_hook/access_hook).
 */
FILE* fopen_hook(const char* path, const char* mode) {
    char new_path[256];
    translate_path(path, new_path, sizeof(new_path));
    static int log = 0;
    if (log < 10) {
        game_log("[FakeJNI] fopen_hook: %s -> %s\n", path, new_path);
        log++;
    }
    return fopen(new_path, mode);
}

/**
 * @brief struct stat with the bionic layout (Android ARM 32-bit, NDK android-9) -- It is NOT the one in newlib/vitasdk.
 */
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
    static int log = 0;
    if (log < 10) {
        game_log("[FakeJNI] stat_hook: %s -> %s = %d (size=%ld)\n", path, new_path, res, res == 0 ? (long) st.st_size : -1L);
        log++;
    }
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
    static int log = 0;
    if (log < 10) {
        game_log("[FakeJNI] access_hook: %s -> %s\n", path, new_path);
        log++;
    }
    return access(new_path, amode);
}

/**
 * @brief Error 500 (Server Error).
 */
so_default_dynlib default_dynlib[] = {
    // C++ ABI / GCC
    { "__cxa_atexit", (uintptr_t)&__cxa_atexit },
    { "__cxa_finalize", (uintptr_t)&__cxa_finalize },
    { "__aeabi_atexit", (uintptr_t)&__aeabi_atexit },
    { "__cxa_begin_cleanup", (uintptr_t)&game_cxa_begin_cleanup },
    { "__cxa_call_unexpected", (uintptr_t)&game_cxa_call_unexpected },
    { "__cxa_guard_acquire", (uintptr_t)&__cxa_guard_acquire },
    { "__cxa_guard_release", (uintptr_t)&__cxa_guard_release },
    { "__cxa_type_match", (uintptr_t)&game_cxa_type_match },
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

/**
 * @brief Import resolution table.
 */
    { "pthread_mutex_init", (uintptr_t)&pthread_mutex_init },
    { "pthread_mutex_lock", (uintptr_t)&pthread_mutex_lock_wrapper },
    { "pthread_mutex_unlock", (uintptr_t)&pthread_mutex_unlock_wrapper },
    { "pthread_mutex_destroy", (uintptr_t)&pthread_mutex_destroy_wrapper },
    { "pthread_cond_wait", (uintptr_t)&pthread_cond_wait_wrapper },
    { "pthread_cond_broadcast", (uintptr_t)&pthread_cond_broadcast_wrapper },
    { "pthread_key_create", (uintptr_t)&pthread_key_create },
    { "pthread_getspecific", (uintptr_t)&pthread_getspecific },
    { "pthread_setspecific", (uintptr_t)&pthread_setspecific },

/**
 * @brief Import resolution table.
 */
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
