/**
 * @brief java.c "Java-side" native method handlers that libgameDSO.so (Zenonia 3, same Gamevil Nexus2/Clet engine Zenonia 2) calls back via FalsoJNI.
 */

#include <falso_jni/FalsoJNI_Impl.h>
#include <falso_jni/FalsoJNI.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "audio.h"
#include "font.h"

extern void game_log(const char *fmt, ...);

/**
 * @brief readAssets/isAssetExist are always called with the same relative path (the engine calls isAssetExist(path) before deciding if it's worth).
 */
static int zenonia_resolve_asset_path(const char *name, char *out, size_t out_size) {
    snprintf(out, out_size, "ux0:data/zenonia3/%s", name);
    if (access(out, F_OK) == 0) return 1;

    snprintf(out, out_size, "ux0:data/zenonia3/assets/%s", name);
    if (access(out, F_OK) == 0) return 1;

    return 0;
}

/**
 * @brief Bridge so that readAssets serves the TWO different consumers that has the engine --- "Dalvik's ArrayObject" trick (16 byte header + raw).
 */
#define ZENONIA_DALVIK_REGISTRY_MAX 1024
static void *g_dalvik_registry[ZENONIA_DALVIK_REGISTRY_MAX];
static int g_dalvik_registry_count = 0;

static void zenonia_register_dalvik_array(void *ptr) {
    if (g_dalvik_registry_count < ZENONIA_DALVIK_REGISTRY_MAX) {
        g_dalvik_registry[g_dalvik_registry_count++] = ptr;
    } else {
        game_log("[Java] ADVERTENCIA: registro de arrays Dalvik lleno (%d), no se puede rastrear mas\n",
            ZENONIA_DALVIK_REGISTRY_MAX);
    }
}

static int zenonia_is_dalvik_array(void *ptr) {
    for (int i = 0; i < g_dalvik_registry_count; i++) {
        if (g_dalvik_registry[i] == ptr) return 1;
    }
    return 0;
}

static jsize (*zenonia_orig_GetArrayLength)(JNIEnv *, jarray) = NULL;
static jbyte *(*zenonia_orig_GetByteArrayElements)(JNIEnv *, jbyteArray, jboolean *) = NULL;
static void (*zenonia_orig_GetByteArrayRegion)(JNIEnv *, jbyteArray, jsize, jsize, jbyte *) = NULL;
static void (*zenonia_orig_ReleaseByteArrayElements)(JNIEnv *, jbyteArray, jbyte *, jint) = NULL;

static jsize Zenonia_GetArrayLength_wrapper(JNIEnv *env, jarray array) {
    if (zenonia_is_dalvik_array(array)) {
        return (jsize) *(uint32_t *)((char *) array + 8);
    }
    return zenonia_orig_GetArrayLength(env, array);
}

static jbyte *Zenonia_GetByteArrayElements_wrapper(JNIEnv *env, jbyteArray array, jboolean *isCopy) {
    if (zenonia_is_dalvik_array(array)) {
        if (isCopy) *isCopy = JNI_FALSE;
        return (jbyte *)((char *) array + 16);
    }
    return zenonia_orig_GetByteArrayElements(env, array, isCopy);
}

static void Zenonia_GetByteArrayRegion_wrapper(JNIEnv *env, jbyteArray array, jsize start, jsize len, jbyte *buf) {
    if (zenonia_is_dalvik_array(array)) {
        memcpy(buf, (char *) array + 16 + start, len);
        return;
    }
    zenonia_orig_GetByteArrayRegion(env, array, start, len, buf);
}

static void Zenonia_ReleaseByteArrayElements_wrapper(JNIEnv *env, jbyteArray array, jbyte *elems, jint mode) {
    if (zenonia_is_dalvik_array(array)) {
        return; // memoria nuestra (malloc de Zenonia_readAssets), no hay copia JNI que liberar
    }
    zenonia_orig_ReleaseByteArrayElements(env, array, elems, mode);
}

/**
 * @brief Call AFTER jni_init() (main.c).
 */
void zenonia_install_array_hooks(void) {
    struct JNINativeInterface *funcs = (struct JNINativeInterface *)(uintptr_t) jni;
    zenonia_orig_GetArrayLength = funcs->GetArrayLength;
    zenonia_orig_GetByteArrayElements = funcs->GetByteArrayElements;
    zenonia_orig_GetByteArrayRegion = funcs->GetByteArrayRegion;
    zenonia_orig_ReleaseByteArrayElements = funcs->ReleaseByteArrayElements;
    funcs->GetArrayLength = Zenonia_GetArrayLength_wrapper;
    funcs->GetByteArrayElements = Zenonia_GetByteArrayElements_wrapper;
    funcs->GetByteArrayRegion = Zenonia_GetByteArrayRegion_wrapper;
    funcs->ReleaseByteArrayElements = Zenonia_ReleaseByteArrayElements_wrapper;
}

/**
 * @brief Call AFTER jni_init() (main.c).
 */

NameToMethodID nameToMethodId[] = {
    { 1, "readAssets", METHOD_TYPE_OBJECT },
/**
 * @brief Call AFTER jni_init() (main.c).
 */
    { 3, "readAssete", METHOD_TYPE_OBJECT },
    { 2, "isAssetExist", METHOD_TYPE_INT },
    { 4, "getGLOptionLinear", METHOD_TYPE_INT },
    { 5, "SetSpeed", METHOD_TYPE_VOID },
    { 6, "getPhoneModel", METHOD_TYPE_OBJECT },
    { 7, "getAbsolueFilePath", METHOD_TYPE_OBJECT },
    { 8, "OnUIStatusChange", METHOD_TYPE_VOID },
    { 9, "OnSoundPlay", METHOD_TYPE_VOID },
/**
 * @brief Real engine type (inherited from Zenonia 2, confirmed in Zenonia 3 with `strings libgameDSO.so`: both strings "readAssets" and "readAssete").
 */
    { 10, "OnStopSound", METHOD_TYPE_VOID },
    { 11, "hideLoadingDialog", METHOD_TYPE_VOID },
    { 12, "OnShowSaveButton", METHOD_TYPE_VOID },
/**
 * @brief Real engine type (inherited from Zenonia 2, confirmed in Zenonia 3 with `strings libgameDSO.so`: both strings "readAssets" and "readAssete").
 */
    { 13, "OnVibrate", METHOD_TYPE_VOID },
    { 14, "OnEvent", METHOD_TYPE_VOID },
/**
 * @brief Real engine type (inherited from Zenonia 2, confirmed in Zenonia 3 with `strings libgameDSO.so`: both strings "readAssets" and "readAssete").
 */
    { 15, "GFA_IsInitialized", METHOD_TYPE_BOOLEAN },
    { 16, "GFA_Init", METHOD_TYPE_BOOLEAN },
    { 17, "GFA_SetTextSize", METHOD_TYPE_VOID },
    { 18, "GFA_CreateFont", METHOD_TYPE_INT },
    { 19, "GFA_SetColor", METHOD_TYPE_VOID },
    { 20, "GFA_GetWordwrapPositionEx", METHOD_TYPE_INT },
    { 21, "GFA_SetFont", METHOD_TYPE_INT },
    { 22, "GFA_CharWidth", METHOD_TYPE_INT },
    { 23, "GFA_CharHeight", METHOD_TYPE_INT },
    { 24, "GFA_GetAscent", METHOD_TYPE_INT },
    { 25, "GFA_GetDescent", METHOD_TYPE_INT },
    { 26, "GFA_GetCurrentFont", METHOD_TYPE_INT },
    { 27, "GFA_GetColor", METHOD_TYPE_INT },
    { 28, "GFA_GetStringLength", METHOD_TYPE_INT },
    { 29, "GFA_SetStringFromKSC5601", METHOD_TYPE_VOID },
    { 30, "GFA_SetStringFromUnicode", METHOD_TYPE_VOID },
    { 31, "GFA_SetString", METHOD_TYPE_VOID },
    { 32, "GFA_SetTextAlign", METHOD_TYPE_VOID },
    { 33, "GFA_SetAntiAlias", METHOD_TYPE_VOID },
    { 34, "GFA_SetLocale", METHOD_TYPE_VOID },
    { 35, "GFA_ReleaseFont", METHOD_TYPE_VOID },
    { 36, "GFA_Release", METHOD_TYPE_VOID },
/**
 * @brief Real engine type (inherited from Zenonia 2, confirmed in Zenonia 3 with `strings libgameDSO.so`: both strings "readAssets" and "readAssete").
 */
    { 37, "GFA_DrawFont", METHOD_TYPE_OBJECT },
    { 38, "GFA_DrawText", METHOD_TYPE_OBJECT },
    { 39, "GFA_MeasureText", METHOD_TYPE_OBJECT },
    { 40, "GFA_GetPixels32", METHOD_TYPE_OBJECT },
    { 41, "GFA_GetPixels16", METHOD_TYPE_OBJECT },
/**
 * @brief Real engine type (inherited from Zenonia 2, confirmed in Zenonia 3 with `strings libgameDSO.so`: both strings "readAssets" and "readAssete").
 */
    { 42, "getPhoneNumber", METHOD_TYPE_OBJECT },
    { 43, "getSimSerialNumber", METHOD_TYPE_OBJECT },
    { 44, "getMacAddress", METHOD_TYPE_OBJECT },
    { 45, "getDeviceID", METHOD_TYPE_OBJECT },
/**
 * @brief Safe no-ops (void): prevent "not found" spam in the log.
 */
    { 46, "getLocaleID", METHOD_TYPE_INT },
};

/**< @brief fstat instead of fseek(SEEK_END)+ftell. */
volatile int g_ui_status = -1;

/**< @brief fstat instead of fseek(SEEK_END)+ftell. */
jobject Zenonia_readAssets(jmethodID id, va_list args) {
    jstring filename = va_arg(args, jstring);
    const char *name = (const char *) filename;
    game_log("[Java] readAssets: %s\n", name ? name : "(null)");

    if (!name) return NULL;

    char path[256];
    if (!zenonia_resolve_asset_path(name, path, sizeof(path))) {
        game_log("[Java] readAssets: not found (tried bare and assets/-prefixed): %s\n", name);
        return NULL;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        game_log("[Java] readAssets: failed to open %s\n", path);
        return NULL;
    }

/**
 * @brief Logged because a not found methodID causes methodIntCall() to FalseJNI returns -1 (see FalseJNI_ImplBridge.c).
 */
    struct stat st;
    long size = -1;
    if (fstat(fileno(f), &st) == 0) {
        size = st.st_size;
    }

    unsigned char peek[8] = {0};
    long cur = ftell(f);
    fseek(f, 0, SEEK_SET);
    fread(peek, 1, size < 8 ? (size_t) size : 8, f);
    fseek(f, cur, SEEK_SET);
    game_log("[Java] readAssets: %s size=%ld first8=%02x%02x%02x%02x%02x%02x%02x%02x\n",
        path, size, peek[0], peek[1], peek[2], peek[3], peek[4], peek[5], peek[6], peek[7]);

    if (size < 0 || size > 64 * 1024 * 1024) { // ningun asset individual del juego deberia acercarse a 64MB
        game_log("[Java] readAssets: bogus/oversized size %ld for %s, aborting\n", size, path);
        fclose(f);
        return NULL;
    }

    void *array_obj = malloc(16 + size);
    if (!array_obj) {
        fclose(f);
        return NULL;
    }

    memset(array_obj, 0, 16); // header de ArrayObject de Dalvik en cero

/**
 * @brief Logged because a not found methodID causes methodIntCall() to FalseJNI returns -1 (see FalseJNI_ImplBridge.c).
 */
    *(uint32_t *)((char *)array_obj + 8) = (uint32_t)size;

    fread((char *) array_obj + 16, 1, size, f);
    fclose(f);

    zenonia_register_dalvik_array(array_obj);

    game_log("[Java] readAssets: Success. Size: %ld bytes\n", size);
    return array_obj;
}

/**< @brief fstat instead of fseek(SEEK_END)+ftell. */
jint Zenonia_isAssetExist(jmethodID id, va_list args) {
    jstring filename = va_arg(args, jstring);
    const char *name = (const char *) filename;
    if (!name) return 0;

    char path[256];
    if (zenonia_resolve_asset_path(name, path, sizeof(path))) {
        struct stat st;
        if (stat(path, &st) == 0 && !S_ISDIR(st.st_mode)) {
            game_log("[Java] isAssetExist: %s -> %ld (%s)\n", name, (long)st.st_size, path);
            return (jint)st.st_size;
        }
    }

    game_log("[Java] isAssetExist: %s -> 0 (not found)\n", name);
    return 0;
}

jint Zenonia_getGLOptionLinear(jmethodID id, va_list args) {
    return 1; // 1 = filtrado lineal
}

void Zenonia_SetSpeed(jmethodID id, va_list args) {
    int speed = va_arg(args, int);
    game_log("[Java] SetSpeed: %d\n", speed);
}

jobject Zenonia_getPhoneModel(jmethodID id, va_list args) {
    return NULL;
}

jobject Zenonia_getAbsolueFilePath(jmethodID id, va_list args) {
/**
 * @brief Logged because a not found methodID causes methodIntCall() to FalseJNI returns -1 (see FalseJNI_ImplBridge.c).
 */
    return (jobject) "ux0:data/zenonia3/";
}

/**
 * @brief Logged because a not found methodID causes methodIntCall() to FalseJNI returns -1 (see FalseJNI_ImplBridge.c).
 */
static jobject zenonia_new_byte_array_str(const char *s) {
    int len = (int) strlen(s);
    JavaDynArray *jda = jda_alloc(len, FIELD_TYPE_BYTE);
    if (!jda) return NULL;
    memcpy(jda->array, s, len);
    return (jobject) jda;
}

jobject Zenonia_getPhoneNumber(jmethodID id, va_list args) {
    return zenonia_new_byte_array_str("01012345678");
}

jobject Zenonia_getSimSerialNumber(jmethodID id, va_list args) {
    return zenonia_new_byte_array_str("8982000012345678901");
}

jobject Zenonia_getMacAddress(jmethodID id, va_list args) {
    return zenonia_new_byte_array_str("00:00:00:00:00:00");
}

jobject Zenonia_getDeviceID(jmethodID id, va_list args) {
    return zenonia_new_byte_array_str("000000000000000");
}

void Zenonia_OnUIStatusChange(jmethodID id, va_list args) {
    int status = va_arg(args, int);
    game_log("[Java] OnUIStatusChange: %d\n", status);
    g_ui_status = status;
}

void Zenonia_VoidNoop(jmethodID id, va_list args) {
}

/**
 * @brief Actual signature: OnSoundPlay(int sndID, int vol, boolean isLoop).
 */
void Zenonia_OnSoundPlay(jmethodID id, va_list args) {
    int snd_id = va_arg(args, int);
    int vol = va_arg(args, int);
    int is_loop = va_arg(args, int); // jboolean
    static int snd_log = 0;
    if (snd_log < 40) {
        game_log("[Java] OnSoundPlay: id=%d vol=%d isLoop=%d\n", snd_id, vol, is_loop);
        snd_log++;
    }
    audio_play(snd_id, vol, is_loop);
}

void Zenonia_OnStopSound(jmethodID id, va_list args) {
    (void) id; (void) args;
    audio_stop_all();
}

void Zenonia_OnVibrate(jmethodID id, va_list args) {
    int time_ms = va_arg(args, int);
    game_log("[Java] OnVibrate: %d ms (no-op, sin soporte de vibracion en Vita fisico)\n", time_ms);
}

void Zenonia_OnEvent(jmethodID id, va_list args) {
    int event = va_arg(args, int);
    game_log("[Java] OnEvent: %d\n", event);
}

/**
 * @brief GFA bridge (fonts) -- NexusFont.
 */
#include "ksc5601_table.h"

#define GFA_MAX_FONTS 5
#define GFA_MAX_STR 1024
static int g_gfa_initialized = 0;
static float g_gfa_text_size = 12.0f;
static int g_gfa_color = 0xff000000;
static int g_gfa_cur_font = -1;
static char g_gfa_font_family[GFA_MAX_FONTS][128];
static int g_gfa_font_used[GFA_MAX_FONTS] = {0};
static int g_gfa_width = 0, g_gfa_height = 0, g_gfa_bpp = 32;
static int g_gfa_string_len = 0;
/**< @brief GFA bridge (fonts) -- NexusFont. */
static uint32_t g_gfa_str[GFA_MAX_STR];
static int g_gfa_str_n = 0;
/**
 * @brief String decoders (NexusFont receives UTF-8 (jstring), UTF-16LE or EUC-KR/KSC5601 depending on the function).
 */
static JavaDynArray *g_gfa_pixels32_jda = NULL;
static JavaDynArray *g_gfa_pixels16_jda = NULL;

/**
 * @brief String decoders (NexusFont receives UTF-8 (jstring), UTF-16LE or EUC-KR/KSC5601 depending on the function).
 */

static int gfa_decode_utf8(const char *s, uint32_t *out, int max) {
    int n = 0;
    const unsigned char *p = (const unsigned char *) s;
    while (*p && n < max) {
        uint32_t cp;
        if (*p < 0x80) { cp = *p++; }
        else if ((*p & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
            cp = ((*p & 0x1F) << 6) | (p[1] & 0x3F); p += 2;
        } else if ((*p & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
            cp = ((*p & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3;
        } else if ((*p & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
            cp = ((*p & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F); p += 4;
        } else { cp = '?'; p++; }
        out[n++] = cp;
    }
    return n;
}

static int gfa_decode_utf16le(const unsigned char *b, int nbytes, uint32_t *out, int max) {
    int n = 0;
    for (int i = 0; i + 1 < nbytes && n < max; i += 2) {
        uint32_t u = b[i] | (b[i + 1] << 8);
        if (u >= 0xD800 && u <= 0xDBFF && i + 3 < nbytes) {
            uint32_t lo = b[i + 2] | (b[i + 3] << 8);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            }
        }
        if (u == 0) break; // el motor manda buffers con padding de ceros
        out[n++] = u;
    }
    return n;
}

static int gfa_decode_euckr(const unsigned char *b, int nbytes, uint32_t *out, int max) {
    int n = 0, i = 0;
    while (i < nbytes && n < max) {
        unsigned char c = b[i];
        if (c == 0) break;
        if (c < 0x80) { out[n++] = c; i++; continue; }
        if (i + 1 < nbytes && c >= 0x81 && c <= 0xFE) {
            unsigned char t = b[i + 1];
            if (t >= 0x41 && t <= 0xFE) {
                uint32_t u = ksc5601_to_ucs2[(c - 0x81) * 190 + (t - 0x41)];
                out[n++] = u ? u : '?';
                i += 2;
                continue;
            }
        }
        out[n++] = '?';
        i++;
    }
    return n;
}

/**
 * @brief Metrics with fallback if the source did not load (same approximations as the stubs from Phase 3.4, so that the game does not lose the).
 */
static float gfa_char_advance(uint32_t cp) {
    if (gfa_font_ready()) return gfa_font_advance(g_gfa_text_size, cp);
    return g_gfa_text_size * 0.6f;
}

static float gfa_text_width_n(const uint32_t *cps, int n) {
    if (gfa_font_ready()) return gfa_font_text_width(g_gfa_text_size, cps, n);
    return g_gfa_text_size * 0.6f * n;
}

/**
 * @brief Paint.breakText(text, true, maxWidth).
 */
static int gfa_break_text(const uint32_t *cps, int n, float max_width) {
    if (gfa_font_ready()) return gfa_font_break_text(g_gfa_text_size, cps, n, max_width);
    float w = 0; int i;
    for (i = 0; i < n; i++) { w += g_gfa_text_size * 0.6f; if (w > max_width) break; }
    return i;
}

/**
 * @brief Word BreakIterator, approximate.
 */
static int gfa_word_break_length(const uint32_t *cps, int n, int fit_chars) {
    int break_len = 0;
    int i = 0;
    while (i < n) {
        int word_end = i;
        uint32_t c = cps[word_end];
        if (c >= 0x1100 && c <= 0xFFDC) {
            word_end++; // CJK/Hangul: cada caracter corta
        } else {
            while (word_end < n) {
                uint32_t w = cps[word_end];
                if (w >= 0x1100 && w <= 0xFFDC) break;
                word_end++;
                if (cps[word_end - 1] == ' ') break;
            }
        }
        if (word_end - i + break_len > fit_chars) break;
        break_len += word_end - i;
        i = word_end;
    }
    return break_len;
}

/**
 * @brief GFA_GetPixels32/16 YES they are safe by construction even if they return a empty array.
 */
static JavaDynArray *gfa_pixels32(void) {
    int count = g_gfa_width * g_gfa_height;
    if (count <= 0) count = 1;
    if (g_gfa_pixels32_jda && g_gfa_pixels32_jda->len != count) {
        jda_free(g_gfa_pixels32_jda);
        g_gfa_pixels32_jda = NULL;
    }
    if (!g_gfa_pixels32_jda) {
        g_gfa_pixels32_jda = jda_alloc(count, FIELD_TYPE_INT);
        if (g_gfa_pixels32_jda) memset(g_gfa_pixels32_jda->array, 0, count * sizeof(int32_t));
    }
    return g_gfa_pixels32_jda;
}

jboolean Zenonia_GFA_IsInitialized(jmethodID id, va_list args) {
    return g_gfa_initialized ? JNI_TRUE : JNI_FALSE;
}

/**
 * @brief Paint.breakText(text, true, maxWidth).
 */
jboolean Zenonia_GFA_Init(jmethodID id, va_list args) {
    g_gfa_width = va_arg(args, int);
    g_gfa_height = va_arg(args, int);
    g_gfa_bpp = va_arg(args, int);
    (void) va_arg(args, int); // colorkey (solo importa para el camino de 16bpp)
    (void) va_arg(args, int); // antialias (jboolean, promovido a int)
    (void) va_arg(args, int); // locale
    g_gfa_initialized = 1;
    gfa_font_init("app0:font.ttf");
    game_log("[Java] GFA_Init: ok w=%d h=%d bpp=%d font_ready=%d\n",
        g_gfa_width, g_gfa_height, g_gfa_bpp, gfa_font_ready());
    return JNI_TRUE;
}

/**
 * @brief JNI promotes float to double when passing it through a variadic va_list (rule of C language, independent of the target's floating point ABI).
 */
void Zenonia_GFA_SetTextSize(jmethodID id, va_list args) {
    g_gfa_text_size = (float) va_arg(args, double);
}

/**
 * @brief Ensure persistent pixel buffers with the current pixel size GFA_Init (if the engine re-initializes with another size, they are released and).
 */
jint Zenonia_GFA_CreateFont(jmethodID id, va_list args) {
    jstring fam = va_arg(args, jstring);
    int style = va_arg(args, int);
    const char *family = (const char *) fam;
    game_log("[Java] GFA_CreateFont: family=%s style=%d\n", family ? family : "(null)", style);

    for (int i = 0; i < GFA_MAX_FONTS; i++) {
        if (g_gfa_font_used[i] && family && strcmp(g_gfa_font_family[i], family) == 0) {
            return i;
        }
    }
    for (int i = 0; i < GFA_MAX_FONTS; i++) {
        if (!g_gfa_font_used[i]) {
            g_gfa_font_used[i] = 1;
            if (family) {
                strncpy(g_gfa_font_family[i], family, sizeof(g_gfa_font_family[i]) - 1);
                g_gfa_font_family[i][sizeof(g_gfa_font_family[i]) - 1] = '\0';
            } else {
                g_gfa_font_family[i][0] = '\0';
            }
            return i;
        }
    }
    game_log("[Java] GFA_CreateFont: sin slots libres\n");
    return -1;
}

void Zenonia_GFA_SetColor(jmethodID id, va_list args) {
    g_gfa_color = va_arg(args, int);
}

jint Zenonia_getLocaleID(jmethodID id, va_list args) {
    (void) id; (void) args;
    return 2; // default no-coreano/no-japones -> idioma 0 del motor
}

/**< @brief (IF)[F -- nChars, maxWidth -> {maxwidth, totalheight}. */
jint Zenonia_GFA_GetWordwrapPositionEx(jmethodID id, va_list args) {
    float maxWidth = (float) va_arg(args, double);
    JavaDynArray *ww = (JavaDynArray *) va_arg(args, jobject);
    int32_t *positions = (ww && ww->array) ? (int32_t *) ww->array : NULL;
    int ww_cap = ww ? ww->len : 0;

    int ww_count = 0;
    const uint32_t *sub = g_gfa_str;
    int sub_len = g_gfa_str_n;
    int pos = 0;
    int char_cnt = gfa_break_text(sub, sub_len, maxWidth);
    while (sub_len > 0 && char_cnt > 0 && char_cnt < sub_len) {
        char_cnt = gfa_break_text(sub, sub_len, maxWidth);
        sub += char_cnt;
        sub_len -= char_cnt;
        pos += char_cnt;
        if (positions && ww_count < ww_cap) positions[ww_count] = pos;
        ww_count++;
    }
    return ww_count;
}

jint Zenonia_GFA_SetFont(jmethodID id, va_list args) {
    int fontId = va_arg(args, int);
    int old = g_gfa_cur_font;
    g_gfa_cur_font = fontId;
    return old;
}

/**
 * @brief Ensure persistent pixel buffers with the current pixel size GFA_Init (if the engine re-initializes with another size, they are released and).
 */
jint Zenonia_GFA_CharHeight(jmethodID id, va_list args) {
    return (jint) g_gfa_text_size;
}

/**
 * @brief Ensure persistent pixel buffers with the current pixel size GFA_Init (if the engine re-initializes with another size, they are released and).
 */
jint Zenonia_GFA_CharWidth(jmethodID id, va_list args) {
    jint w = (jint) gfa_char_advance(0xBDC1);
    return w > 0 ? w : 1;
}

/**
 * @brief Replicates slot reuse of NexusFont.
 */
jint Zenonia_GFA_GetAscent(jmethodID id, va_list args) {
    if (gfa_font_ready()) return gfa_font_ascent(g_gfa_text_size);
    return (jint) g_gfa_text_size;
}

jint Zenonia_GFA_GetDescent(jmethodID id, va_list args) {
    if (gfa_font_ready()) return gfa_font_descent(g_gfa_text_size);
    jint h = (jint) g_gfa_text_size;
    return h > 0 ? h / 4 : 0;
}

jint Zenonia_GFA_GetCurrentFont(jmethodID id, va_list args) {
    return g_gfa_cur_font;
}

jint Zenonia_GFA_GetColor(jmethodID id, va_list args) {
    return g_gfa_color;
}

jint Zenonia_GFA_GetStringLength(jmethodID id, va_list args) {
    return g_gfa_str_n;
}

/**
 * @brief (Ljava/lang/String;I)V -- string (jstring = raw UTF-8 char* in FalseJNI), nChars (0 = full length).
 */
void Zenonia_GFA_SetString(jmethodID id, va_list args) {
    const char *s = (const char *) va_arg(args, jstring);
    int nChars = va_arg(args, int);
    g_gfa_str_n = s ? gfa_decode_utf8(s, g_gfa_str, GFA_MAX_STR) : 0;
    if (nChars > 0 && nChars < g_gfa_str_n) g_gfa_str_n = nChars;
    g_gfa_string_len = g_gfa_str_n;
}

/**
 * @brief ([B)V -- the parameter arrives as a real jbyteArray (allocated by the engine via NewByteArray + SetByteArrayRegion, confirmed in log) -- in].
 */
void Zenonia_GFA_SetStringFromKSC5601(jmethodID id, va_list args) {
    JavaDynArray *jda = (JavaDynArray *) va_arg(args, jobject);
    if (jda && jda->array) {
        g_gfa_str_n = gfa_decode_euckr((const unsigned char *) jda->array, jda->len,
                                       g_gfa_str, GFA_MAX_STR);
    } else {
        g_gfa_str_n = 0;
    }
    g_gfa_string_len = g_gfa_str_n;
}

/**
 * @brief Java: new String(data, "UTF-16LE").
 */
void Zenonia_GFA_SetStringFromUnicode(jmethodID id, va_list args) {
    JavaDynArray *jda = (JavaDynArray *) va_arg(args, jobject);
    if (jda && jda->array) {
        g_gfa_str_n = gfa_decode_utf16le((const unsigned char *) jda->array, jda->len,
                                         g_gfa_str, GFA_MAX_STR);
    } else {
        g_gfa_str_n = 0;
    }
    g_gfa_string_len = g_gfa_str_n;
}

/**
 * @brief Familia GFA_Draw*, GFA_Measure*: devuelven un float[] que el codigo nativo
 * desreferencia SIN chequear NULL (confirmado con vita-parse-core: Data abort
 * real dentro de GFA_DrawFont+0x60, justo despues del GetFloatArrayElements
 * que devolvia NULL porque el metodo no estaba registrado -- ver
 * port_progress.md Fase 3.5). Por eso estos NO pueden devolver NULL nunca en
 * el camino normal, a diferencia de los Object no registrados que dan NULL
 * "seguro" en otras partes del motor.
 * @note Ver docs/loader/java.md para el razonamiento de diseño.
 */
static JavaDynArray *gfa_persistent_floats(JavaDynArray **slot, int len) {
    if (!*slot) {
        *slot = jda_alloc(len, FIELD_TYPE_FLOAT);
    }
    return *slot;
}

/**
 * @brief Clear the GFA canvas (equivalent to clearing g_gfaIntBuf in Java).
 */
static uint32_t *gfa_clear_canvas(void) {
    JavaDynArray *px = gfa_pixels32();
    if (!px || !px->array) return NULL;
    memset(px->array, 0, (size_t) g_gfa_width * g_gfa_height * 4);
    return (uint32_t *) px->array;
}

/**< @brief Clear the GFA canvas (equivalent to clearing g_gfaIntBuf in Java). */
jobject Zenonia_GFA_DrawFont(jmethodID id, va_list args) {
    (void) args;
    static JavaDynArray *jda_slot = NULL;
    JavaDynArray *jda = gfa_persistent_floats(&jda_slot, 4);
    if (!jda) {
        game_log("[Java] GFA_DrawFont: jda_alloc failed\n");
        return NULL;
    }
    float *f = (float *) jda->array;
    float fH = (float)(jint) g_gfa_text_size; // Java usa GFA_CharHeight() (int)
    uint32_t *canvas = gfa_clear_canvas();
    float width;
    if (canvas && gfa_font_ready()) {
        float baseline = (fH - gfa_font_descent(g_gfa_text_size)) + 1.0f;
        width = gfa_font_draw_line(g_gfa_text_size, g_gfa_str, g_gfa_str_n,
                                   canvas, g_gfa_width, g_gfa_height,
                                   0.0f, baseline, (uint32_t) g_gfa_color);
    } else {
        width = gfa_text_width_n(g_gfa_str, g_gfa_str_n);
    }
    f[0] = 0.0f;
    f[1] = 0.0f;
    f[2] = width;
    f[3] = fH + 1.0f;
    return (jobject) jda;
}

/**< @brief ()[F -- NexusFont. */
jobject Zenonia_GFA_DrawText(jmethodID id, va_list args) {
    float x = (float) va_arg(args, double);
    float y = (float) va_arg(args, double);
    (void) va_arg(args, int);    // nChars -- el Java real no lo usa
    float maxWidth = (float) va_arg(args, double);

    static JavaDynArray *jda_slot = NULL;
    JavaDynArray *jda = gfa_persistent_floats(&jda_slot, 4);
    if (!jda) {
        game_log("[Java] GFA_DrawText: jda_alloc failed\n");
        return NULL;
    }
    float *f = (float *) jda->array;
    float fH = (float)(jint) g_gfa_text_size;
    uint32_t *canvas = gfa_clear_canvas();

    float max_w = 0.0f;
    float yy = y + fH;
    const uint32_t *sub = g_gfa_str;
    int sub_len = g_gfa_str_n;
    while (1) {
        int char_cnt = gfa_break_text(sub, sub_len, maxWidth);
        if (char_cnt >= sub_len) {
            if (canvas && gfa_font_ready())
                gfa_font_draw_line(g_gfa_text_size, sub, sub_len, canvas,
                                   g_gfa_width, g_gfa_height, x, yy,
                                   (uint32_t) g_gfa_color);
            float w = gfa_text_width_n(sub, sub_len);
            if (w > max_w) max_w = w;
            break;
        }
        int break_len = gfa_word_break_length(sub, sub_len, char_cnt);
        int show_len = break_len > 0 ? break_len : sub_len;
        if (canvas && gfa_font_ready())
            gfa_font_draw_line(g_gfa_text_size, sub, show_len, canvas,
                               g_gfa_width, g_gfa_height, x, yy,
                               (uint32_t) g_gfa_color);
        float w = gfa_text_width_n(sub, show_len);
        if (w > max_w) max_w = w;
        yy += fH;
        if (break_len > 0) {
            sub += break_len;
            sub_len -= break_len;
        } else {
            break; // Java: subStr = "" y la proxima iteracion corta
        }
    }

    f[0] = x;
    f[1] = y;
    f[2] = max_w;
    f[3] = (yy - y) + (gfa_font_ready() ? gfa_font_descent(g_gfa_text_size)
                                        : (jint)(g_gfa_text_size / 4));
    return (jobject) jda;
}

/**< @brief (IF)[F -- nChars, maxWidth -> {maxwidth, totalheight}. */
jobject Zenonia_GFA_MeasureText(jmethodID id, va_list args) {
    (void) va_arg(args, int);    // nChars -- el Java real no lo usa
    float maxWidth = (float) va_arg(args, double);

    static JavaDynArray *jda_slot = NULL;
    JavaDynArray *jda = gfa_persistent_floats(&jda_slot, 2);
    if (!jda) {
        game_log("[Java] GFA_MeasureText: jda_alloc failed\n");
        return NULL;
    }
    float *f = (float *) jda->array;
    float fH = (float)(jint) g_gfa_text_size;

    float max_w = 0.0f;
    float yy = fH;
    const uint32_t *sub = g_gfa_str;
    int sub_len = g_gfa_str_n;
    while (1) {
        int char_cnt = gfa_break_text(sub, sub_len, maxWidth);
        if (char_cnt >= sub_len) {
            float w = gfa_text_width_n(sub, sub_len);
            if (w > max_w) max_w = w;
            break;
        }
        int break_len = gfa_word_break_length(sub, sub_len, char_cnt);
        int show_len = break_len > 0 ? break_len : sub_len;
        float w = gfa_text_width_n(sub, show_len);
        if (w > max_w) max_w = w;
        yy += fH;
        if (break_len > 0) {
            sub += break_len;
            sub_len -= break_len;
        } else {
            break;
        }
    }

    f[0] = max_w;
    f[1] = yy + (gfa_font_ready() ? gfa_font_descent(g_gfa_text_size)
                                  : (jint)(g_gfa_text_size / 4));
    return (jobject) jda;
}

/**
 * @brief GFA_GetPixels32/16 YES they are safe by construction even if they return a empty array.
 */
jobject Zenonia_GFA_GetPixels32(jmethodID id, va_list args) {
    (void) args;
    return (jobject) gfa_pixels32();
}

/**
 * @brief GFA_GetPixels32/16 YES they are safe by construction even if they return a empty array.
 */
jobject Zenonia_GFA_GetPixels16(jmethodID id, va_list args) {
    (void) args;
    int count = g_gfa_width * g_gfa_height;
    if (count <= 0) count = 1;
    if (g_gfa_pixels16_jda && g_gfa_pixels16_jda->len != count) {
        jda_free(g_gfa_pixels16_jda);
        g_gfa_pixels16_jda = NULL;
    }
    if (!g_gfa_pixels16_jda) {
        g_gfa_pixels16_jda = jda_alloc(count, FIELD_TYPE_SHORT);
        if (g_gfa_pixels16_jda) memset(g_gfa_pixels16_jda->array, 0, count * sizeof(int16_t));
    }
    JavaDynArray *px32 = gfa_pixels32();
    if (g_gfa_pixels16_jda && px32 && px32->array) {
        uint32_t *src = (uint32_t *) px32->array;
        uint16_t *dst = (uint16_t *) g_gfa_pixels16_jda->array;
        for (int i = 0; i < count; i++) {
            uint32_t p = src[i];
            dst[i] = (uint16_t)((((p >> 16) & 0xF8) << 8) |
                                (((p >> 8) & 0xFC) << 3) |
                                ((p & 0xF8) >> 3));
        }
    }
    return (jobject) g_gfa_pixels16_jda;
}

MethodsBoolean methodsBoolean[] = {
    { 15, Zenonia_GFA_IsInitialized },
    { 16, Zenonia_GFA_Init },
};
MethodsByte methodsByte[] = {};
MethodsChar methodsChar[] = {};
MethodsDouble methodsDouble[] = {};
MethodsFloat methodsFloat[] = {};
MethodsInt methodsInt[] = {
    { 2, Zenonia_isAssetExist },
    { 4, Zenonia_getGLOptionLinear },
    { 18, Zenonia_GFA_CreateFont },
    { 20, Zenonia_GFA_GetWordwrapPositionEx },
    { 21, Zenonia_GFA_SetFont },
    { 22, Zenonia_GFA_CharWidth },
    { 23, Zenonia_GFA_CharHeight },
    { 24, Zenonia_GFA_GetAscent },
    { 25, Zenonia_GFA_GetDescent },
    { 26, Zenonia_GFA_GetCurrentFont },
    { 27, Zenonia_GFA_GetColor },
    { 28, Zenonia_GFA_GetStringLength },
    { 46, Zenonia_getLocaleID },
};
MethodsLong methodsLong[] = {};
MethodsObject methodsObject[] = {
    { 1, Zenonia_readAssets },
    { 3, Zenonia_readAssets },
    { 6, Zenonia_getPhoneModel },
    { 7, Zenonia_getAbsolueFilePath },
    { 37, Zenonia_GFA_DrawFont },
    { 38, Zenonia_GFA_DrawText },
    { 39, Zenonia_GFA_MeasureText },
    { 40, Zenonia_GFA_GetPixels32 },
    { 41, Zenonia_GFA_GetPixels16 },
    { 42, Zenonia_getPhoneNumber },
    { 43, Zenonia_getSimSerialNumber },
    { 44, Zenonia_getMacAddress },
    { 45, Zenonia_getDeviceID },
};
MethodsShort methodsShort[] = {};
MethodsVoid methodsVoid[] = {
    { 5, Zenonia_SetSpeed },
    { 8, Zenonia_OnUIStatusChange },
    { 9, Zenonia_OnSoundPlay },
    { 10, Zenonia_OnStopSound },
    { 11, Zenonia_VoidNoop },
    { 12, Zenonia_VoidNoop },
    { 13, Zenonia_OnVibrate },
    { 14, Zenonia_OnEvent },
    { 17, Zenonia_GFA_SetTextSize },
    { 19, Zenonia_GFA_SetColor },
    { 29, Zenonia_GFA_SetStringFromKSC5601 },
    { 30, Zenonia_GFA_SetStringFromUnicode },
    { 31, Zenonia_GFA_SetString },
    { 32, Zenonia_VoidNoop }, // GFA_SetTextAlign
    { 33, Zenonia_VoidNoop }, // GFA_SetAntiAlias
    { 34, Zenonia_VoidNoop }, // GFA_SetLocale
    { 35, Zenonia_VoidNoop }, // GFA_ReleaseFont
    { 36, Zenonia_VoidNoop }, // GFA_Release
};

/*
 * JNI Fields
 */

NameToFieldID nameToFieldId[] = {};

FieldsBoolean fieldsBoolean[] = {};
FieldsByte fieldsByte[] = {};
FieldsChar fieldsChar[] = {};
FieldsDouble fieldsDouble[] = {};
FieldsFloat fieldsFloat[] = {};
FieldsInt fieldsInt[] = {};
FieldsObject fieldsObject[] = {};
FieldsLong fieldsLong[] = {};
FieldsShort fieldsShort[] = {};

__FALSOJNI_IMPL_CONTAINER_SIZES
