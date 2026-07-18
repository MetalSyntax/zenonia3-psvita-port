/*
 * main.c
 *
 * ARMv7 Shared Libraries loader. Zenonia 3.
 *
 * Mismo motor (Gamevil Nexus2/"Clet") que Zenonia 2 -- ver
 * ../Zenonia2-vita/loader/main.c y plan_zenonia3_port.md seccion 0 para el
 * detalle de que es identico y que cambio de ABI real entre ambos juegos.
 */

#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/dirent.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/display.h>
#include <psp2/power.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#include "debugScreen.h"
#include "so_util.h"
#include "audio.h"
#include "androidui.h"
#include <taihen.h>
#include <vitaGL.h>
#include <falso_jni/FalsoJNI.h>

#define printf psvDebugScreenPrintf
#define LOG_DIR "ux0:data/zenonia3/logs"

// Resolucion LOGICA del juego: 400x240 fijo. Confirmado por triple fuente
// (2026-07-10): el Java original pasa gameScreenWidth/Height = 400/240 a
// NativeInitDeviceInfo/NativeInitWithBufferSize (NexusGLActivity.java:85-86,
// jadx renombro la constante 400 como UI_STATUS_PURCHASE_PAGE), getDeviceInfo()
// defaultea di[3]=400 di[4]=0xf0 (out_ghidra.c:148015), y el codigo de dibujo
// del titulo esta hardcodeado a ese espacio (CMvTitleState::DrawZeroGrade
// hace DrawFillRect(0,0,400,240) y centra con (400-w)>>1 -- out_ghidra.c:105397).
// Inicializar a 800x480 hacia que el juego pintara solo el cuadrante superior
// izquierdo del framebuffer (sprites chicos + el resto blanco/negro, visto en
// consola real). El escalado a pantalla completa lo hace el MOTOR: glResize()
// arma el quad con vertices +-w/2 del tamano que le pasemos a NativeResize
// (out_ghidra.c:148107+), asi que NativeResize recibe SCREEN_W/H (960x544).
#define GAME_W 400
#define GAME_H 240
#define SCREEN_W 960
#define SCREEN_H 544

FILE *log_file = NULL;

// Una vez que vitaGL toma la pantalla, no se debe seguir escribiendo al
// framebuffer crudo de debugScreen -- ambos competirian por el mismo buffer.
int gl_active = 0;

int _newlib_heap_size_user = 128 * 1024 * 1024; // 128 MB para newlib (malloc)
unsigned int sceLibcHeapSize = 4 * 1024 * 1024; // 4 MB para SCE Libc (system libs)

// Un archivo de log por corrida, con timestamp, dentro de logs/ -- mantiene
// el historial completo entre pruebas en vez de pisar siempre el mismo
// log.txt (ver psvita-porting skill, hardware_debugging.md).
void init_log() {
    sceIoMkdir(LOG_DIR, 0777); // falla en silencio si ya existe

    char log_path[256];
    time_t t = time(NULL);
    snprintf(log_path, sizeof(log_path), LOG_DIR "/log_%u.txt", (unsigned int) t);

    log_file = fopen(log_path, "w");
    if (log_file) {
        fprintf(log_file, "--- ZENONIA 3 PORT LOG START (%s) ---\n", log_path);
        fflush(log_file);
    }
}

void game_log(const char *fmt, ...) {
    va_list list;
    char string[512];

    va_start(list, fmt);
    vsnprintf(string, sizeof(string), fmt, list);
    va_end(list);

    if (log_file) {
        fprintf(log_file, "%s", string);
        fflush(log_file);
    }
}

void fatal_error(const char *fmt, ...) {
    va_list list;
    char string[512];

    va_start(list, fmt);
    vsnprintf(string, sizeof(string), fmt, list);
    va_end(list);

    game_log("[FATAL] %s\n", string);
    // La pantalla de debug se inicializa solo aca: en un arranque sano no se
    // muestra nunca texto de consola.
    psvDebugScreenInit();
    printf("[FATAL] %s\n", string);
    sceKernelDelayThread(10 * 1000 * 1000); // 10s para poder leerlo antes de morir
    sceKernelExitProcess(0);
}

so_module zenonia3_mod;

// No confirmado que el .so de Zenonia 3 lo importe (ver Fase 1 del plan: no
// aparece en `objdump -T | grep UND`), pero se deja disponible por si algun
// build futuro lo necesita -- no hace dano tenerlo sin registrar en
// dynlib.c.
int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
	va_list list;
	char string[512];

	va_start(list, fmt);
	vsnprintf(string, sizeof(string), fmt, list);
	va_end(list);

	game_log("[ANDROID] %s: %s\n", tag, string);
	return 0;
}

extern so_default_dynlib default_dynlib[];
extern int default_dynlib_size;

// Punteros a funciones JNI del juego. A diferencia de Zenonia 2: no hay
// NativeInit (no existe en este .so) ni setInputEvent (idem) -- ver tabla
// de diferencias de ABI en plan_zenonia3_port.md.
int (* Game_JNI_OnLoad)(void *vm, void *reserved);
void (* NativeInitDeviceInfo)(void *env, void *obj, int w, int h);
void (* NativeInitWithBufferSize)(void *env, void *obj, int w, int h);
void (* NativeRender)(void *env, void *obj);
void (* NativeResize)(void *env, void *obj, int w, int h);
void (* NativeResumeClet)(void *env, void *obj);
void (* handleCletEvent)(void *env, void *obj, int type, int p1, int p2, int p3);

// --- Input: protocolo pendiente de reconfirmar contra el Java real de
// Zenonia 3 (Fase 5 del plan -- decompilar ZenoniaUIControllerView y el
// NexusHal equivalente, no asumir que es igual a Zenonia2UIControllerView).
// Se arranca con los mismos codigos MH_* y keycodes HAL de Zenonia 2 porque
// son constantes del MOTOR (Nexus2/Clet), no de la capa de juego -- pero
// deben confirmarse con un log real antes de dar esto por bueno.
//
// Diferencia real de ABI: no existe setInputEvent en este .so. Solo hay
// handleCletEvent, que ahora toma 4 ints (antes 3) -- ver Fase 1 del plan y
// out_ghidra.c:147900. Se sigue encolando un evento por frame y entregandolo
// justo antes de NativeRender (mismo orden que NexusGLRenderer.drawFrame en
// Zenonia 2), sin la entrega inmediata que hacia setInputEvent, porque acá
// no existe ese canal.

#define MH_KEY_PRESSEVENT       2
#define MH_KEY_RELEASEEVENT     3
#define MH_POINTER_PRESSEVENT   23
#define MH_POINTER_RELEASEEVENT 24
#define MH_POINTER_MOVEEVENT    25

#define HAL_KEY_UP    (-1)
#define HAL_KEY_DOWN  (-2)
#define HAL_KEY_LEFT  (-3)
#define HAL_KEY_RIGHT (-4)
#define HAL_KEY_OK    (-5)
#define HAL_KEY_MAP   (-6)
#define HAL_KEY_SAVE  (-10)
#define HAL_KEY_BACK  (-16)
#define HAL_KEY_SKIP  (35)

// Constantes reales de NexusHal.java -- el boton "Continuar" del menu no
// manda un press/release de tecla como los demas, manda un UNICO evento con
// este tipo/parametro (ver Natives.java: img_menu_continue.setOnClickListener
// -> handleCletEvent(NexusHal.REPLY_YESNO, NexusHal.FIRST_MOVE_REPLY_PAGE, 0, 0)).
#define NEXUS_HAL_REPLY_YESNO           19450815
#define NEXUS_HAL_FIRST_MOVE_REPLY_PAGE 20010913
// Los botones "escribir resena"/"mas tarde" de la propia pantalla de
// REPLY_PAGE (ver Natives.java: showReplyMoveComponent(), img_btn_write/
// img_btn_later OnClickListener) mandan el mismo tipo REPLY_YESNO con estos
// otros dos parametros.
#define NEXUS_HAL_YES_MOVE_REPLY_PAGE   20010911
#define NEXUS_HAL_NO_MOVE_REPLY_PAGE    20010912

#define UI_STATUS_EXIT 104

typedef struct { int type, p1, p2, p3; } input_event;
static input_event event_queue[16];
static int eq_head = 0, eq_tail = 0;

static void queue_input_event(int type, int p1, int p2, int p3) {
    static int in_log = 0;
    if (in_log < 40) {
        game_log("[INPUT] event type=%d p1=%d p2=%d p3=%d\n", type, p1, p2, p3);
        in_log++;
    }
    int next = (eq_tail + 1) % 16;
    if (next != eq_head) {
        event_queue[eq_tail].type = type;
        event_queue[eq_tail].p1 = p1;
        event_queue[eq_tail].p2 = p2;
        event_queue[eq_tail].p3 = p3;
        eq_tail = next;
    }
}

static const struct { unsigned int btn; int hal; } btn_map[] = {
    { SCE_CTRL_UP,       HAL_KEY_UP },
    { SCE_CTRL_DOWN,     HAL_KEY_DOWN },
    { SCE_CTRL_LEFT,     HAL_KEY_LEFT },
    { SCE_CTRL_RIGHT,    HAL_KEY_RIGHT },
    { SCE_CTRL_CROSS,    HAL_KEY_OK },
    { SCE_CTRL_CIRCLE,   HAL_KEY_BACK },
    { SCE_CTRL_TRIANGLE, HAL_KEY_SKIP },
    { SCE_CTRL_SQUARE,   HAL_KEY_MAP },
    { SCE_CTRL_LTRIGGER, HAL_KEY_SAVE },
};
#define BTN_MAP_COUNT (sizeof(btn_map) / sizeof(btn_map[0]))

// NOTA: a diferencia de Zenonia 2, aca NO se porta ningun parche binario
// (apply_so_patches). El bug de Zenonia2 (puntero de heap tratado como
// signed en CMvLayerData::PreLoad) es un patron de motor viejo que puede
// reaparecer en este .so, pero en un offset DISTINTO -- no reusar el numero
// 0xaec38 a ciegas. Si aparece el mismo sintoma (NULL "imposible" en datos
// que deberian haberse cargado), re-derivar el offset con vita-parse-core +
// objdump -d sobre ESTE binario (ver Fase 7 del plan).

extern volatile int g_ui_status;
extern void zenonia_install_array_hooks(void);
extern void zenonia_install_hide_dpad_hook(so_module *mod);

static GLuint splash_tex = 0;

static void splash_load(void) {
    FILE *f = fopen("app0:splash.rgba", "rb");
    if (!f) {
        game_log("splash: app0:splash.rgba no encontrado (ok si todavia no se generaron los assets de LiveArea)\n");
        return;
    }
    void *data = malloc(960 * 544 * 4);
    if (!data) { fclose(f); return; }
    fread(data, 1, 960 * 544 * 4, f);
    fclose(f);

    glGenTextures(1, &splash_tex);
    glBindTexture(GL_TEXTURE_2D, splash_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 960, 544, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    free(data);
}

static void splash_draw(void) {
    if (!splash_tex) return;

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrthof(0, 960, 544, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, splash_tex);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    static const float verts[] = { 0, 0,  960, 0,  0, 544,  960, 544 };
    static const float uvs[]   = { 0, 0,  1, 0,    0, 1,    1, 1 };
    glVertexPointer(2, GL_FLOAT, 0, verts);
    glTexCoordPointer(2, GL_FLOAT, 0, uvs);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

void log_active_frame_buf(const char *label) {
    SceDisplayFrameBuf fb;
    memset(&fb, 0, sizeof(fb));
    fb.size = sizeof(fb);
    int ret = sceDisplayGetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
    game_log("[DISPLAY] %s: sceDisplayGetFrameBuf ret=0x%08x base=%p w=%d h=%d pitch=%d\n",
             label, ret, fb.base, fb.width, fb.height, fb.pitch);
}

void gl_init() {
    // Sin MSAA / sin triple buffering: config confirmada funcionando en
    // hardware real para el mismo motor (Zenonia 2) y para Prince of Persia.
    vglUseTripleBuffering(GL_FALSE);
    // GL_FALSE aca es el resultado sano a 960x544 (resolucion nativa, nunca
    // cae al fallback) -- no tratarlo como fallo de init.
    vglInitExtended(0, 960, 544, 6 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);
#ifdef LOCK_FPS_30
    // El vsync interno de vitaGL solo espera 1 vblank (panel a 60Hz) -- con
    // RGB565_CONVERT_MODE=NATIVE el frame a veces entra en ese vblank y a
    // veces no, y el resultado es un framerate que rebota entre ~60 y ~40
    // (el jitter/stutter tipico de estar justo en el borde del vblank). Se
    // apaga el vsync de vitaGL y se toma control manual del pacing en el
    // loop principal (sceDisplayWaitVblankStartMulti(2)) para forzar
    // siempre 2 vblanks por frame -- 30fps estable y sin tearing en vez de
    // "hasta 60 pero irregular".
    vglWaitVblankStart(GL_FALSE);
#endif
    gl_active = 1;
}

// Clocks conservadores de fabrica (CPU 333MHz / bus 166MHz / GPU 111MHz) --
// subir a los maximos estables conocidos en homebrew de Vita es standard
// practice (mismo approach usado en la mayoria de ports vitaGL) y no toca
// ninguna logica del juego, solo la frecuencia real del hardware.
void raise_clocks() {
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
}

int main() {
    raise_clocks();
    init_log();
	game_log("Iniciando Zenonia 3 port (SoLoader)\n");

	int res = so_file_load(&zenonia3_mod, "ux0:data/zenonia3/libgameDSO.so", 0x98000000);
	if (res < 0) {
		game_log("Error critico cargando libgameDSO.so: 0x%08X\n", res);
        sceKernelDelayThread(5000000);
	} else {
		game_log("Libreria cargada con exito.\n");
		game_log("mod: text_base=0x%08x num_dynsym=%d dynsym=%p dynstr=%p hash=%p soname=%s\n",
			(unsigned int) zenonia3_mod.text_base, zenonia3_mod.num_dynsym,
			(void*)zenonia3_mod.dynsym, (void*)zenonia3_mod.dynstr, (void*)zenonia3_mod.hash,
			zenonia3_mod.soname ? zenonia3_mod.soname : "(null)");

		so_relocate(&zenonia3_mod);
		so_resolve(&zenonia3_mod, default_dynlib, default_dynlib_size, 0);

		// Parche binario para CMvLayerData::PreLoad (igual a Zenonia 2, diferente offset)
		uint32_t text_base = zenonia3_mod.text_base;
		if (*(uint16_t *)(text_base + 0x9a7b4) == 0xdd24) {
			uint16_t patch_beq = 0xd024;
			kuKernelCpuUnrestrictedMemcpy((void *)(text_base + 0x9a7b4), &patch_beq, 2);
			game_log("Parche aplicado: CMvLayerData::PreLoad (ble -> beq)\n");
		}
		so_flush_caches(&zenonia3_mod);
		so_initialize(&zenonia3_mod);

		game_log("SoLoader inicializado. Iniciando vitaGL...\n");
		gl_init();
		game_log("vitaGL inicializado.\n");
		audio_init();
		splash_load();
		androidui_load(SCREEN_W, SCREEN_H);

		jni_init();
		zenonia_install_array_hooks(); // ver java.c -- reconoce nuestros bloques readAssets en GetArrayLength/GetByteArrayElements
		JNIEnv *jniEnv = &jni;

		Game_JNI_OnLoad = (void *)so_symbol(&zenonia3_mod, "JNI_OnLoad");
		NativeInitDeviceInfo = (void *)so_symbol(&zenonia3_mod, "Java_com_gamevil_nexus2_Natives_NativeInitDeviceInfo");
		NativeInitWithBufferSize = (void *)so_symbol(&zenonia3_mod, "Java_com_gamevil_nexus2_Natives_NativeInitWithBufferSize");
		NativeRender = (void *)so_symbol(&zenonia3_mod, "Java_com_gamevil_nexus2_Natives_NativeRender");
		NativeResize = (void *)so_symbol(&zenonia3_mod, "Java_com_gamevil_nexus2_Natives_NativeResize");
		NativeResumeClet = (void *)so_symbol(&zenonia3_mod, "Java_com_gamevil_nexus2_Natives_NativeResumeClet");
		handleCletEvent = (void *)so_symbol(&zenonia3_mod, "Java_com_gamevil_nexus2_Natives_handleCletEvent");
		zenonia_install_hide_dpad_hook(&zenonia3_mod); // ver dynlib.c -- oculta cruceta+botones de accion (HIDE_VIRTUAL_GAMEPAD)

		game_log("Symbols: JNI_OnLoad=%p NativeInitDeviceInfo=%p NativeInitWithBufferSize=%p NativeRender=%p NativeResize=%p NativeResumeClet=%p handleCletEvent=%p\n",
			(void*)Game_JNI_OnLoad, (void*)NativeInitDeviceInfo, (void*)NativeInitWithBufferSize,
			(void*)NativeRender, (void*)NativeResize, (void*)NativeResumeClet, (void*)handleCletEvent);

		// Secuencia de arranque -- reemplaza al NativeInit() unico de
		// Zenonia 2 (que no existe aca).
		//
		// ORDEN CONFIRMADO CON UN CRASH REAL (ver port_progress.md Fase 3,
		// bug #2): NativeInitWithBufferSize DEBE llamarse ANTES que
		// NativeInitDeviceInfo. NativeInitWithBufferSize dispara
		// startClet() -> GxCreateGlobalHeap() -> Gcx_MM_Init(), que crea el
		// pool de memoria PROPIO del motor (un allocator custom tipo slab,
		// separado de malloc/new) del que depende Gcx_MM_Alloc/Calloc.
		// NativeInitDeviceInfo usa ese mismo allocator para el buffer de
		// pixeles interno (via getDeviceInfo() -> Gcx_MM_Calloc) -- si se
		// llama primero, el pool todavia no existe, Gcx_MM_Alloc devuelve
		// NULL, y el memset(NULL,0,n) subsiguiente hace Data Abort (visto
		// en consola real, resuelto con vita-parse-core + objdump -d contra
		// libgameDSO.so: LR cae en Gcx_MM_Calloc+0x13, justo despues del
		// blx a memset@plt).
		game_log("Llamando JNI_OnLoad...\n");
		if (Game_JNI_OnLoad) Game_JNI_OnLoad(&jvm, NULL);
		game_log("Llamando NativeInitWithBufferSize(%d,%d)...\n", GAME_W, GAME_H);
		if (NativeInitWithBufferSize) NativeInitWithBufferSize(jniEnv, NULL, GAME_W, GAME_H);
		game_log("Llamando NativeInitDeviceInfo(%d,%d)...\n", GAME_W, GAME_H);
		if (NativeInitDeviceInfo) NativeInitDeviceInfo(jniEnv, NULL, GAME_W, GAME_H);
		// El tamano de PANTALLA, no el del buffer -- igual que Android, donde
		// NativeResize recibe el tamano de la GLSurfaceView real y el motor
		// arma solo el quad de escalado (ver nota en GAME_W/GAME_H).
		game_log("Llamando NativeResize(%d,%d)...\n", SCREEN_W, SCREEN_H);
		if (NativeResize) NativeResize(jniEnv, NULL, SCREEN_W, SCREEN_H);
		game_log("Llamando NativeResumeClet...\n");
		if (NativeResumeClet) NativeResumeClet(jniEnv, NULL);

		game_log("Iniciando Bucle Principal...\n");

		sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);

		SceCtrlData pad;
		SceTouchData touch;
		int last_touch = 0;
		int last_touch_suppressed = 0;
		int last_tx = 0, last_ty = 0;
		unsigned int old_buttons = 0;
		int frame = 0;

		// Contador de FPS real (para comparar builds A/B de las
		// optimizaciones RGB565_LUT/NEON_FIXED contra un mismo punto de
		// referencia en el log, en vez de "a ojo"). Ventana de ~2s en vez de
		// por-frame para no agregar overhead de log al loop caliente.
		int fps_count = 0;
		SceUInt64 fps_window_start = sceKernelGetProcessTimeWide();

		while (1) {
			sceCtrlPeekBufferPositive(0, &pad, 1);
			sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);

			if ((frame++ % 120) == 0) {
				game_log("frame %d alive, touch.reportNum=%d pad.buttons=0x%08x ui_status=%d\n", frame, touch.reportNum, (unsigned int) pad.buttons, g_ui_status);
			}

			// Salida de emergencia: START+SELECT juntos
			if ((pad.buttons & SCE_CTRL_START) && (pad.buttons & SCE_CTRL_SELECT)) break;

			// UI_STATUS_EXIT (104): en Android, ZenoniaUIControllerView.setUIState()
			// llama NexusGLActivity.myActivity.finishApp() para este estado --
			// una llamada puramente de Android que este loader no tiene forma
			// de reproducir. Sin este chequeo, el loop seguia corriendo para
			// siempre sin dibujar nada util (pantalla negra permanente) --
			// confirmado como la causa mas probable del "pantalla negra" al
			// navegar Triangulo(SKIP)+Circulo(BACK) en el menu: BACK ahi
			// dispara el flujo de salida del motor.
			if (g_ui_status == UI_STATUS_EXIT) break;

			unsigned int pressed = pad.buttons & ~old_buttons;
			unsigned int released = old_buttons & ~pad.buttons;

			// ABOUT/HELP (ui_status 5/4): en el APK original el scroll de
			// aboutWebView/helpWebView era gesto tactil -- sin capa Android
			// (ver androidui.c) no hay forma de reproducir eso, asi que se
			// reusa D-pad arriba/abajo para scrollear en vez de reenviarlo al
			// motor (que de todos modos no hace nada con UP/DOWN en estos 2
			// estados). Mantenido presionado = scroll continuo.
			if (g_ui_status == 4 || g_ui_status == 5) {
				if (pad.buttons & SCE_CTRL_UP)   androidui_scroll_info_text(g_ui_status, -8.0f, SCREEN_W, SCREEN_H);
				if (pad.buttons & SCE_CTRL_DOWN) androidui_scroll_info_text(g_ui_status,  8.0f, SCREEN_W, SCREEN_H);
				pressed  &= ~(SCE_CTRL_UP | SCE_CTRL_DOWN);
				released &= ~(SCE_CTRL_UP | SCE_CTRL_DOWN);
			}

			for (int i = 0; i < BTN_MAP_COUNT; i++) {
				if (pressed & btn_map[i].btn)
					queue_input_event(MH_KEY_PRESSEVENT, btn_map[i].hal, 0, 0);
				if (released & btn_map[i].btn)
					queue_input_event(MH_KEY_RELEASEEVENT, btn_map[i].hal, 0, 0);
			}
			old_buttons = pad.buttons;

			// Touch: panel 1920x1088 -> espacio de JUEGO (GAME_W x GAME_H =
			// 400x240). Confirmado en UIFullTouch.java (la ruta de touch
			// in-game real de Zenonia 3): convertScreenX/Y multiplican por
			// gameScreenWidth/Height ANTES de setTouchEvent(23/25/24, x, y,
			// pointerId) -- el motor recibe coordenadas de juego, no de
			// pantalla. p3 es el pointerId (un solo dedo: 0). El evento 25
			// (MOVE) se manda cuando el dedo se arrastra -- Android lo manda
			// en cada ACTION_MOVE; aca, cuando cambia la celda de pixel.
			if (touch.reportNum > 0) {
				int x = touch.report[0].x * GAME_W / 1920;
				int y = touch.report[0].y * GAME_H / 1088;

				if (!last_touch) {
					// En el APK original, el logo/titulo/menu (ui_status
					// 1/2) son ImageView/ImageButton de Android que
					// CONSUMEN el toque antes de que llegue al
					// GLSurfaceView -- el motor nunca recibe un evento de
					// puntero crudo ahi, solo la tecla sintetica que el
					// listener de Android manda (ver androidui.c y
					// Natives.java: showTitleComponent()/
					// showMenuItemComponent()). Replicar eso: en vez de
					// reenviar el toque como MH_POINTER_*, interceptarlo
					// aca y mandar el evento equivalente -- o nada, si cae
					// afuera de cualquier boton (el fondo del menu no tiene
					// listener propio en el original).
					int sx = touch.report[0].x * SCREEN_W / 1920;
					int sy = touch.report[0].y * SCREEN_H / 1088;

					if (g_ui_status == 1) {
						// Natives.showTitleComponent(): gb_titleImg (fondo,
						// pantalla completa) manda BACK press+release ante
						// CUALQUIER toque.
						queue_input_event(MH_KEY_PRESSEVENT, HAL_KEY_BACK, 0, 0);
						queue_input_event(MH_KEY_RELEASEEVENT, HAL_KEY_BACK, 0, 0);
						last_touch_suppressed = 1;
					} else if (g_ui_status == 2) {
						androidui_menu_hit hit = androidui_menu_hit_test((float) sx, (float) sy, SCREEN_W, SCREEN_H);
						switch (hit) {
							case ANDROIDUI_MENU_HIT_COMMUNITY:
								queue_input_event(MH_KEY_PRESSEVENT, HAL_KEY_LEFT, 0, 0);
								queue_input_event(MH_KEY_RELEASEEVENT, HAL_KEY_LEFT, 0, 0);
								last_touch_suppressed = 1;
								break;
							case ANDROIDUI_MENU_HIT_OPTIONS:
								queue_input_event(MH_KEY_PRESSEVENT, HAL_KEY_UP, 0, 0);
								queue_input_event(MH_KEY_RELEASEEVENT, HAL_KEY_UP, 0, 0);
								last_touch_suppressed = 1;
								break;
							case ANDROIDUI_MENU_HIT_NEWGAME:
								queue_input_event(MH_KEY_PRESSEVENT, HAL_KEY_OK, 0, 0);
								queue_input_event(MH_KEY_RELEASEEVENT, HAL_KEY_OK, 0, 0);
								last_touch_suppressed = 1;
								break;
							case ANDROIDUI_MENU_HIT_CONTINUE:
								queue_input_event(NEXUS_HAL_REPLY_YESNO, NEXUS_HAL_FIRST_MOVE_REPLY_PAGE, 0, 0);
								last_touch_suppressed = 1;
								break;
							case ANDROIDUI_MENU_HIT_HELP:
								queue_input_event(MH_KEY_PRESSEVENT, HAL_KEY_DOWN, 0, 0);
								queue_input_event(MH_KEY_RELEASEEVENT, HAL_KEY_DOWN, 0, 0);
								last_touch_suppressed = 1;
								break;
							case ANDROIDUI_MENU_HIT_ABOUT:
								queue_input_event(MH_KEY_PRESSEVENT, HAL_KEY_RIGHT, 0, 0);
								queue_input_event(MH_KEY_RELEASEEVENT, HAL_KEY_RIGHT, 0, 0);
								last_touch_suppressed = 1;
								break;
							default:
								last_touch_suppressed = 0;
								break;
						}
					} else if (g_ui_status == 5 || g_ui_status == 4) {
						// Natives.showAboutComponent()/showHelpComponent(): el
						// mismo boton "uiback" (arriba a la derecha) manda BACK
						// press+release en las 2 pantallas.
						androidui_backbtn_hit hit = androidui_backbtn_hit_test((float) sx, (float) sy, SCREEN_W, SCREEN_H);
						if (hit == ANDROIDUI_BACKBTN_HIT_BACK) {
							queue_input_event(MH_KEY_PRESSEVENT, HAL_KEY_BACK, 0, 0);
							queue_input_event(MH_KEY_RELEASEEVENT, HAL_KEY_BACK, 0, 0);
							last_touch_suppressed = 1;
						} else {
							last_touch_suppressed = 0;
						}
					} else if (g_ui_status == 5000) {
						// Natives.showReplyMoveComponent(): popup de "valorar
						// la app" mostrado al tocar Continuar -- botones
						// "escribir resena"/"mas tarde", cada uno con su
						// propio OnClickListener (no touch crudo al motor).
						androidui_reply_hit hit = androidui_reply_hit_test((float) sx, (float) sy, SCREEN_W, SCREEN_H);
						switch (hit) {
							case ANDROIDUI_REPLY_HIT_WRITE:
								queue_input_event(NEXUS_HAL_REPLY_YESNO, NEXUS_HAL_YES_MOVE_REPLY_PAGE, 0, 0);
								last_touch_suppressed = 1;
								break;
							case ANDROIDUI_REPLY_HIT_LATER:
								queue_input_event(NEXUS_HAL_REPLY_YESNO, NEXUS_HAL_NO_MOVE_REPLY_PAGE, 0, 0);
								last_touch_suppressed = 1;
								break;
							default:
								last_touch_suppressed = 0;
								break;
						}
					} else {
						last_touch_suppressed = 0;
					}

					if (!last_touch_suppressed) {
						queue_input_event(MH_POINTER_PRESSEVENT, x, y, 0);
					}
					last_touch = 1;
				} else if (!last_touch_suppressed && (x != last_tx || y != last_ty)) {
					queue_input_event(MH_POINTER_MOVEEVENT, x, y, 0);
				}
				last_tx = x; last_ty = y;
			} else if (last_touch) {
				if (!last_touch_suppressed) {
					queue_input_event(MH_POINTER_RELEASEEVENT, last_tx, last_ty, 0);
				}
				last_touch = 0;
				last_touch_suppressed = 0;
			}

			glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);

			if (handleCletEvent && eq_head != eq_tail) {
				input_event *ev = &event_queue[eq_head];
				eq_head = (eq_head + 1) % 16;
				handleCletEvent(jniEnv, NULL, ev->type, ev->p1, ev->p2, ev->p3);
			}

			if (NativeRender) NativeRender(jniEnv, NULL);

			// Mientras el motor este en logo/titulo (estados de UI de Java,
			// invisibles en el loader nativo -- pendiente confirmar los
			// numeros de estado reales de Zenonia 3 en Fase 5), tapar con
			// el splash.
			if (g_ui_status >= 0 && g_ui_status <= 1) splash_draw();

			// Logo/titulo/menu (ui_status -1/1/2): el .so nativo solo dibuja
			// un fondo generico (CMvTitleState::DrawZeroGrade/DrawTeamLogo) --
			// el arte real (logo, fondo de titulo, fondo+botones de menu) lo
			// dibujaba Android como ImageView/ImageButton superpuestos al
			// GLSurfaceView (ver androidui.c). Sin capa Java, hay que
			// replicarlo aca.
			androidui_draw(g_ui_status, SCREEN_W, SCREEN_H);

#ifdef LOCK_FPS_30
			// 2 vblanks = 30fps a un panel de 60Hz. Si el frame ya tardo mas
			// de eso (build lenta, RGB565_CONVERT_MODE=SCALAR/LUT), esta
			// llamada practicamente no espera -- no empeora nada, solo actua
			// como cap cuando el frame es lo bastante rapido para pasarlo.
			sceDisplayWaitVblankStartMulti(2);
#endif
			vglSwapBuffers(GL_FALSE);

			fps_count++;
			SceUInt64 now = sceKernelGetProcessTimeWide();
			if (now - fps_window_start >= 2000000) {
				double secs = (now - fps_window_start) / 1000000.0;
				game_log("[PERF] FPS=%.1f (frames=%d en %.2fs)\n", fps_count / secs, fps_count, secs);
				fps_count = 0;
				fps_window_start = now;
			}
		}
	}

    if (log_file) {
        fclose(log_file);
    }
	sceKernelExitProcess(0);
	return 0;
}
