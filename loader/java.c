/*
 * java.c
 *
 * "Java-side" native method handlers that libgameDSO.so (Zenonia 3, mismo
 * motor Gamevil Nexus2/Clet que Zenonia 2) llama de vuelta vía FalsoJNI
 * (GetStaticMethodID + CallStaticObjectMethod/CallStaticIntMethod/etc).
 * Todo lo que no se registra aca simplemente queda "not found" para
 * FalsoJNI (logueado, no fatal para metodos void/Object -- ver
 * plan_zenonia3_port.md seccion "Riesgos" y port_progress.md de Zenonia2
 * §9.10 para el caso contrario: metodos int/boolean SI son peligrosos si
 * no se registran).
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

// readAssets/isAssetExist se llaman siempre con el mismo path relativo (el
// motor llama isAssetExist(path) antes de decidir si vale la pena llamar
// readAssets(path)), asi que ambos deben resolver igual. Igual que en
// Zenonia 2: probar primero el path pelado (ux0:data/zenonia3/<name>) y si
// no existe, el prefijado con assets/ (que usan los hooks de dynlib.c para
// todo lo demas) -- sin asumir cual es la convencion real hasta confirmarla
// con un log de consola.
static int zenonia_resolve_asset_path(const char *name, char *out, size_t out_size) {
    snprintf(out, out_size, "ux0:data/zenonia3/%s", name);
    if (access(out, F_OK) == 0) return 1;

    snprintf(out, out_size, "ux0:data/zenonia3/assets/%s", name);
    if (access(out, F_OK) == 0) return 1;

    return 0;
}

// --- Puente para que readAssets sirva a los DOS consumidores distintos que
// tiene el motor ---
//
// El truco de "ArrayObject de Dalvik" (header de 16 bytes + datos crudos,
// ver Zenonia_readAssets) funciona para el consumidor directo por puntero
// (MC_knlGetResource y el resto de CMvResourceMgr) porque ese codigo nunca
// pasa por las funciones estandar de arrays de JNI -- lee offset+16
// directamente. Pero un consumidor DISTINTO (confirmado real: la familia de
// parsers "PZx"/"PZD" -- CGxPZDParser/CGxZeroPZDParser -- usada para
// TouchOemIME.pzx y otros .pzx, ver port_progress.md Fase 3.7) SI llama a
// GetArrayLength/GetByteArrayElements estandar sobre el mismo resultado de
// readAssets. Como nuestro bloque no es un JavaDynArray real (no paso por
// jda_alloc), FalsoJNI no lo encuentra ("Could not find the array") y esos
// parsers reciben NULL/longitud 0 -- causando un crash rio abajo (puntero
// NULL asumido valido tras un "exito" a medias del parser).
//
// Solucion: interceptar GetArrayLength/GetByteArrayElements/
// GetByteArrayRegion/ReleaseByteArrayElements de la tabla de funciones JNI
// (mutable en memoria pese al `const` del tipo publico -- jni_init() la
// aloca con malloc()) para reconocer nuestros propios bloques (llevando un
// registro de punteros devueltos por Zenonia_readAssets) y servirlos
// directamente desde el header Dalvik, cayendo al codigo real de FalsoJNI
// para cualquier otro array (los JavaDynArray genuinos de jda_alloc, usados
// por ejemplo en las funciones GFA_* de la Fase 3.5).
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

// Llamar DESPUES de jni_init() (main.c) -- necesita que la tabla de
// funciones ya este alocada.
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

/*
 * JNI Methods
 */

NameToMethodID nameToMethodId[] = {
    { 1, "readAssets", METHOD_TYPE_OBJECT },
    // Typo real del motor (heredado de Zenonia 2, confirmado en Zenonia 3
    // con `strings libgameDSO.so`: existen ambas cadenas "readAssets" y
    // "readAssete" en el binario). Mismo handler para las dos.
    { 3, "readAssete", METHOD_TYPE_OBJECT },
    { 2, "isAssetExist", METHOD_TYPE_INT },
    { 4, "getGLOptionLinear", METHOD_TYPE_INT },
    { 5, "SetSpeed", METHOD_TYPE_VOID },
    { 6, "getPhoneModel", METHOD_TYPE_OBJECT },
    { 7, "getAbsolueFilePath", METHOD_TYPE_OBJECT },
    { 8, "OnUIStatusChange", METHOD_TYPE_VOID },
    { 9, "OnSoundPlay", METHOD_TYPE_VOID },
    // No-ops seguros (void): evitan spam de "not found" en el log.
    { 10, "OnStopSound", METHOD_TYPE_VOID },
    { 11, "hideLoadingDialog", METHOD_TYPE_VOID },
    { 12, "OnShowSaveButton", METHOD_TYPE_VOID },
    // Nuevos en el UIListener de Zenonia 3 (no existian en Zenonia 2):
    // OnVibrate(int) y OnEvent(int). Ambos void, ya serian seguros sin
    // registrar (ver nota de arriba), pero se registran para no spamear
    // el log y para tener el punto de entrada listo cuando llegue la Fase
    // de audio/vibracion.
    { 13, "OnVibrate", METHOD_TYPE_VOID },
    { 14, "OnEvent", METHOD_TYPE_VOID },
    // --- Puente de fuentes GFA ("Gamevil Font API") -- ver
    // zenonia3_java/sources/com/gamevil/nexus2/NexusFont.java (implementacion
    // real via android.graphics.Paint/Canvas/Bitmap) y port_progress.md Fase
    // 3.4 para el diagnostico completo. CGxFontAndroid::Create() (motor)
    // llama GFA_CreateFont y solo registra la fuente en CGxFACharCache si el
    // handle devuelto es >= 0 -- sin registrar, FalsoJNI devuelve -1 para
    // metodos int no encontrados (mismo patron que isAssetExist en Zenonia2
    // §9.10), el motor nunca llama a addFont/setFont, y el arbol interno del
    // char cache queda en NULL -> crash real en el primer findChar() (Data
    // abort confirmado con vita-parse-core + objdump -d).
    // NO hay rasterizado real de glifos todavia (Fase de texto real
    // pendiente): estos stubs replican la maquina de estados de Java
    // (slots de fuente, tamano/color actual, metricas aproximadas) para que
    // el motor progrese sin crashear, pero el texto se vera en blanco hasta
    // implementar un renderizador de fuente bitmap real.
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
    // Devuelven float[]/int[]/short[] que el motor desreferencia SIN
    // chequear NULL -- confirmado con un crash real (Data abort dentro de
    // GFA_DrawFont) para DrawFont; DrawText/MeasureText comparten la misma
    // estructura de wrapper nativo (ver port_progress.md Fase 3.5). Object,
    // NUNCA deben devolver NULL en el camino normal.
    { 37, "GFA_DrawFont", METHOD_TYPE_OBJECT },
    { 38, "GFA_DrawText", METHOD_TYPE_OBJECT },
    { 39, "GFA_MeasureText", METHOD_TYPE_OBJECT },
    { 40, "GFA_GetPixels32", METHOD_TYPE_OBJECT },
    { 41, "GFA_GetPixels16", METHOD_TYPE_OBJECT },
    // Alimentan CGsPhoneInfoV2::CheckPhoneNumber() -- ver nota junto a los
    // handlers mas abajo y port_progress.md Fase 3.6. Sin estos, el motor
    // nunca construye CGsInputKey/CGsUIMgr/CGsSound/etc.
    { 42, "getPhoneNumber", METHOD_TYPE_OBJECT },
    { 43, "getSimSerialNumber", METHOD_TYPE_OBJECT },
    { 44, "getMacAddress", METHOD_TYPE_OBJECT },
    { 45, "getDeviceID", METHOD_TYPE_OBJECT },
    // NexusUtils.getLocaleID(): 1=Corea, 3=Japon, 4=China, 5=Francia, 2=resto.
    // El consumidor nativo (CMvOptionSaveData, out_ghidra.c:~44978) mapea
    // 1->idioma coreano, 3->japones, cualquier otro->0 (default/ingles). Sin
    // registrar, FalsoJNI devolvia -1, que cae en el mismo default -- se
    // registra con 2 explicito para sacar el [JNI ERR] del log y dejar la
    // decision documentada (cambiar a 1 si se quiere el juego en coreano).
    { 46, "getLocaleID", METHOD_TYPE_INT },
};

// Estado de UI que reporta el motor via OnUIStatusChange. main.c lo usa para
// saber cuando dejar de mostrar el splash -- mismo mecanismo que Zenonia 2
// (logo/titulo eran ImageViews de Java, invisibles en el loader nativo),
// pendiente confirmar en Fase 5 si Zenonia 3 usa los mismos numeros de
// estado o si ZenoniaUIControllerView los cambio.
volatile int g_ui_status = -1;

// El motor (compilado contra un NDK viejo pre-ART) lee el jbyteArray que
// esto devuelve accediendo directo al layout interno de ArrayObject de
// Dalvik (header de 16 bytes + datos crudos) en vez de usar
// GetByteArrayElements -- mismo mecanismo confirmado para Zenonia 2, y
// coherente con que Zenonia 3 sea el mismo motor (Fase 1).
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

    // fstat en vez de fseek(SEEK_END)+ftell: en Zenonia 2, ftell() devolvio
    // basura (bytes de la propia ruta) para al menos un archivo real y
    // corrompio el malloc posterior del motor. fstat no depende de la
    // posicion del cursor y evita esa clase de bug de raiz.
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

    // Dalvik ArrayObject espera el largo como entero de 32 bits en el offset 8
    *(uint32_t *)((char *)array_obj + 8) = (uint32_t)size;

    fread((char *) array_obj + 16, 1, size, f);
    fclose(f);

    zenonia_register_dalvik_array(array_obj);

    game_log("[Java] readAssets: Success. Size: %ld bytes\n", size);
    return array_obj;
}

// Registrado porque un methodID no encontrado hace que methodIntCall() de
// FalsoJNI devuelva -1 (ver FalsoJNI_ImplBridge.c) -- un valor no-cero que
// el motor interpreta como booleano C "true" (el archivo existe). Ese falso
// positivo fue la causa real de un crash en Zenonia 2 (§9.10 de su
// port_progress.md): el motor seguia adelante cargando un archivo que en
// realidad no se habia resuelto.
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
    // El motor tiene el typo "Absolue" en vez de "Absolute" (heredado de
    // Zenonia 2). Devuelve un string JNI (FalsoJNI lo implementa como char*)
    // con barra final para que el motor pueda concatenar el nombre del asset.
    return (jobject) "ux0:data/zenonia3/";
}

// --- getPhoneNumber/getSimSerialNumber/getMacAddress/getDeviceID ---
//
// Causa real de que el motor nunca llegue al menu (rastreada con la skill
// so-crash-triage, ver port_progress.md Fase 3.6): estos 4 metodos devuelven
// byte[] via getSystemProperty() -> MC_knlGetSystemProperty(), que a su vez
// alimenta CGsPhoneInfoV2::CheckPhoneNumber() -- una validacion de
// operador/SIM tipica de juegos moviles coreanos con DRM de carrier. Sin
// registrar, devuelven NULL (seguro, no crashea -- el motor chequea NULL
// antes de copiar), pero deja los 4 buffers de telefono/SIM/MAC/deviceID
// vacios, y CheckPhoneNumber() falla -> CGsPhoneInfoV2::InitPhoneInfo()
// devuelve 0 -> CMvApp::EvAppStart() salta la construccion de TODOS los
// subsistemas del motor (CGsInputKey, CGsUIMgr, CGsSound, CMvResourceMgr,
// etc., ver disasm real: `if (iVar1==0) return;`) sin crashear ahi mismo --
// el crash real ocurre mucho despues, en CGsInputKey::SetReleaseKey, cuando
// CMvApp::EvAppResume asume que ese singleton ya existe.
//
// Basta con que getPhoneNumber empiece con "01" + un digito para que
// CheckPhoneNumber pase de inmediato (numero de celular coreano valido) --
// se devuelven los otros 3 igual, por si algun otro camino del motor los usa.
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

// Firma real: OnSoundPlay(int sndID, int vol, boolean isLoop) -- el segundo
// parametro es VOLUMEN (0-100, tipico 50/75), el tercero el loop. Los logs
// viejos los etiquetaban al reves (misma correccion que Zenonia 2 §12.1).
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

// --- Puente GFA (fuentes) -- replica de NexusFont.java con rasterizado REAL
// via loader/font.c (stb_truetype + app0:font.ttf). La fuente de verdad de
// cada semantica es NexusFont.java (jadx); el formato de pixeles que consume
// el motor esta confirmado en out_ghidra.c (CopyPixelsToCharCacheBuffer usa
// SOLO el canal alfa, stride = ancho del bitmap de GFA_Init).
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
// String actual (g_strConv de NexusFont) como codepoints Unicode.
static uint32_t g_gfa_str[GFA_MAX_STR];
static int g_gfa_str_n = 0;
// Buffers de pixeles del "canvas" GFA, persistentes entre llamadas (el motor
// "libera" con DeleteLocalRef, no-op en FalsoJNI; y jda_alloc devuelve
// punteros a la tabla global que NO deben cachearse a traves de un realloc
// -- ver Fase 4.1; con la tabla de 1024 y estos persistentes no hay realloc).
static JavaDynArray *g_gfa_pixels32_jda = NULL;
static JavaDynArray *g_gfa_pixels16_jda = NULL;

// --- Decodificadores de string (NexusFont recibe UTF-8 (jstring), UTF-16LE
// o EUC-KR/KSC5601 segun la funcion) ---

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

// Metricas con fallback si la fuente no cargo (mismas aproximaciones que los
// stubs de la Fase 3.4, para que el juego no pierda el layout por completo).
static float gfa_char_advance(uint32_t cp) {
    if (gfa_font_ready()) return gfa_font_advance(g_gfa_text_size, cp);
    return g_gfa_text_size * 0.6f;
}

static float gfa_text_width_n(const uint32_t *cps, int n) {
    if (gfa_font_ready()) return gfa_font_text_width(g_gfa_text_size, cps, n);
    return g_gfa_text_size * 0.6f * n;
}

// Paint.breakText(text, true, maxWidth): caracteres desde el inicio cuyo
// avance acumulado entra en maxWidth.
static int gfa_break_text(const uint32_t *cps, int n, float max_width) {
    if (gfa_font_ready()) return gfa_font_break_text(g_gfa_text_size, cps, n, max_width);
    float w = 0; int i;
    for (i = 0; i < n; i++) { w += g_gfa_text_size * 0.6f; if (w > max_width) break; }
    return i;
}

// BreakIterator de palabras, aproximado: una frontera despues de cada corrida
// de espacios, y cada caracter CJK/Hangul es su propia "palabra" (igual que
// el BreakIterator real para ideografos). Devuelve la cantidad de caracteres
// hasta la ultima frontera de palabra que entra en fit_chars (breakLength del
// Java); 0 si ninguna frontera entra.
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

// Asegura los buffers de pixeles persistentes con el tamano actual de
// GFA_Init (si el motor re-inicializa con otro tamano, se liberan y realocan
// -- jda_free deja el slot de la tabla reutilizable sin mover el resto).
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

// (IIIIZI)Z -- width,height,bpp,colorkey,antialias,locale.
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

// JNI promueve float a double al pasarlo por un va_list variadico (regla del
// lenguaje C, independiente de la ABI de punto flotante del target) -- por
// eso se lee con va_arg(args, double) y se castea a float, no al reves.
void Zenonia_GFA_SetTextSize(jmethodID id, va_list args) {
    g_gfa_text_size = (float) va_arg(args, double);
}

// Replica la reutilizacion de slots de NexusFont.GFA_CreateFont: hasta
// GFA_MAX_FONTS familias distintas, devuelve el mismo handle si la familia ya
// esta registrada, -1 si no quedan slots libres (igual que el original).
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

// (F[I)I -- maxWidth, wwPositions[]. Replica exacta del loop de
// NexusFont.GFA_GetWordwrapPositionEx: mientras el resto no entre en
// maxWidth, corta en breakText(maxWidth) caracteres, acumula la posicion y
// la escribe en wwPositions. Devuelve la cantidad de cortes.
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

// Igual que NexusFont.GFA_CharHeight(): literalmente el tamano de texto
// actual, sin redondeo especial.
jint Zenonia_GFA_CharHeight(jmethodID id, va_list args) {
    return (jint) g_gfa_text_size;
}

// NexusFont.GFA_CharWidth(): el ancho de avance del caracter Hangul '뷁'
// (U+BDC1) -- o sea, el ancho de celda de un caracter coreano completo.
jint Zenonia_GFA_CharWidth(jmethodID id, va_list args) {
    jint w = (jint) gfa_char_advance(0xBDC1);
    return w > 0 ? w : 1;
}

// GFA_GetAscent = -ceil(paint.ascent()) -> POSITIVO (altura sobre la linea
// base); GFA_GetDescent = ceil(paint.descent()) -> positivo.
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

// (Ljava/lang/String;I)V -- string (jstring = char* UTF-8 crudo en FalsoJNI),
// nChars (0 = largo completo). Java: g_strConv = string.substring(0, nChars).
void Zenonia_GFA_SetString(jmethodID id, va_list args) {
    const char *s = (const char *) va_arg(args, jstring);
    int nChars = va_arg(args, int);
    g_gfa_str_n = s ? gfa_decode_utf8(s, g_gfa_str, GFA_MAX_STR) : 0;
    if (nChars > 0 && nChars < g_gfa_str_n) g_gfa_str_n = nChars;
    g_gfa_string_len = g_gfa_str_n;
}

// ([B)V -- el parametro llega como un jbyteArray real (alocado por el motor
// via NewByteArray + SetByteArrayRegion, confirmado en el log) -- en FalsoJNI
// eso es un JavaDynArray*, no un puntero crudo (a diferencia de jstring, que
// FalsoJNI SI pasa como char* crudo -- no confundir los dos casos).
// Java: new String(data, "KSC5601") -- EUC-KR via tabla generada (cp949).
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

// Java: new String(data, "UTF-16LE").
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

// --- Familia GFA_Draw*/Measure*: devuelven un float[] que el codigo nativo
// desreferencia SIN chequear NULL (confirmado con vita-parse-core: Data
// abort real dentro de GFA_DrawFont+0x60, justo despues del
// GetFloatArrayElements que devolvia NULL porque el metodo no estaba
// registrado -- ver port_progress.md Fase 3.5). Por eso estos NO pueden
// devolver NULL nunca en el camino normal, a diferencia de los Object no
// registrados que dan NULL "seguro" en otras partes del motor.
//
// DrawFont/DrawText/MeasureText se llaman decenas de veces POR FRAME en el
// menu (layout de texto continuo) y el motor "libera" el resultado solo con
// DeleteLocalRef -- que FalsoJNI ignora. Alocar un jda nuevo por llamada
// filtraba una entrada de tabla + el buffer por cada texto medido (miles por
// minuto), y cada realloc de la tabla jda (jda_extend) invalidaba los
// punteros persistentes cacheados (los "Array 0x... not found" sobre
// GetPixels32 en log_1783658068 eran exactamente eso). El motor consume el
// float[] inmediatamente (GetFloatArrayElements + Release dentro de la misma
// llamada nativa, confirmado en el log), asi que un unico jda persistente
// reutilizado por funcion es seguro.
static JavaDynArray *gfa_persistent_floats(JavaDynArray **slot, int len) {
    if (!*slot) {
        *slot = jda_alloc(len, FIELD_TYPE_FLOAT);
    }
    return *slot;
}

// Limpia el canvas GFA (equivalente al clear de g_gfaIntBuf en Java).
static uint32_t *gfa_clear_canvas(void) {
    JavaDynArray *px = gfa_pixels32();
    if (!px || !px->array) return NULL;
    memset(px->array, 0, (size_t) g_gfa_width * g_gfa_height * 4);
    return (uint32_t *) px->array;
}

// ()[F -- NexusFont.GFA_DrawFont(): limpia el bitmap, dibuja el string actual
// en (0, charH - descent + 1) y devuelve {0, 0, anchoMedido, charH + 1}.
// El motor luego pide GFA_GetPixels32 y usa ceil(rect[2]) x ceil(rect[3])
// pixeles con SOLO el canal alfa (drawCharToCharCacheBuffer).
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

// (FFIF)[F -- x, y, nChars, maxWidth. NexusFont.GFA_DrawText(): dibujo
// multilinea con word-wrap dentro del bitmap; devuelve {x, y, anchoMax,
// altoTotal}. nChars viene ignorado tambien en el Java real (usa g_strConv).
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

// (IF)[F -- nChars, maxWidth -> {anchoMax, altoTotal}. Mismo loop que
// DrawText pero solo midiendo (NexusFont.GFA_MeasureText).
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

// GFA_GetPixels32/16 SI son seguros por construccion aunque devuelvan un
// array vacio: el codigo nativo que los llama copia via
// GetArrayLength+GetIntArrayRegion/GetShortArrayRegion (no desreferencia un
// puntero crudo como GFA_DrawFont/DrawText/MeasureText) -- confirmado
// leyendo el desensamblado de GFA_GetPixels32/16 en out_ghidra.c. Se
// registran igual, con un buffer real (en cero, sin rasterizado todavia) en
// vez de depender de que ese camino tolere un array NULL/vacio sin probarlo.
// Los pixeles ya quedaron rasterizados en el buffer por el ultimo
// GFA_DrawFont/DrawText -- aca solo se devuelve el array (el motor copia con
// GetIntArrayRegion y usa el canal alfa).
jobject Zenonia_GFA_GetPixels32(jmethodID id, va_list args) {
    (void) args;
    return (jobject) gfa_pixels32();
}

// Camino de 16bpp (GFA_Init bpp=16): el Java usa un bitmap RGB565 con
// colorkey. Este build inicializa con bpp=32 (confirmado en el log), asi que
// esto queda como conversion best-effort por si algun flujo lo pide.
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
