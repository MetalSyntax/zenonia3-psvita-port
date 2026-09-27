/**
 * @brief main.c ARMv7 Shared Libraries loader. Zenonia 3. Same engine (Gamevil Nexus2/"Clet") as Zenonia 2.
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

/**
 * @brief LOGICAL resolution of the game: 400x240 fixed.
 */
#define GAME_W 400
#define GAME_H 240
#define SCREEN_W 960
#define SCREEN_H 544

FILE *log_file = NULL;

/**< @brief LOGICAL resolution of the game: 400x240 fixed. */
int gl_active = 0;

int _newlib_heap_size_user = 128 * 1024 * 1024; // 128 MB para newlib (malloc)
unsigned int sceLibcHeapSize = 4 * 1024 * 1024; // 4 MB para SCE Libc (system libs)

/**
 * @brief One log file per run, with timestamp, inside logs/.
 */
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
/**
 * @brief One log file per run, with timestamp, inside logs/.
 */
    psvDebugScreenInit();
    printf("[FATAL] %s\n", string);
    sceKernelDelayThread(10 * 1000 * 1000); // 10s para poder leerlo antes de morir
    sceKernelExitProcess(0);
}

so_module zenonia3_mod;

/**
 * @brief Not confirmed that the Zenonia 3 .
 */
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

/**
 * @brief Not confirmed that the Zenonia 3 .
 */
int (* Game_JNI_OnLoad)(void *vm, void *reserved);
void (* NativeInitDeviceInfo)(void *env, void *obj, int w, int h);
void (* NativeInitWithBufferSize)(void *env, void *obj, int w, int h);
void (* NativeRender)(void *env, void *obj);
void (* NativeResize)(void *env, void *obj, int w, int h);
void (* NativeResumeClet)(void *env, void *obj);
void (* handleCletEvent)(void *env, void *obj, int type, int p1, int p2, int p3);

/**
 * @brief Input: protocol pending reconfirmation against the real Java of Zenonia 3 (Phase 5 of the plan -- decompile ZenoniaUIControllerView and the).
 */

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

/**
 * @brief Actual NexusHal.
 */
#define NEXUS_HAL_REPLY_YESNO           19450815
#define NEXUS_HAL_FIRST_MOVE_REPLY_PAGE 20010913
/**
 * @brief The "write review"/"later" buttons on the review screen itself REPLY_PAGE (see Natives.java: showReplyMoveComponent(), img_btn_write/).
 */
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

/**
 * @brief NOTE: unlike Zenonia 2, no binary patch is carried here (apply_so_patches).
 */

extern volatile int g_ui_status;
extern void zenonia_install_array_hooks(void);
extern void zenonia_install_hide_dpad_hook(so_module *mod);
extern void zenonia_toggle_dpad_visibility(void);

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
/**
 * @brief vitaGL's internal vsync only expects 1 vblank (panel at 60Hz).
 */
    vglUseTripleBuffering(GL_FALSE);
/**
 * @brief vitaGL's internal vsync only expects 1 vblank (panel at 60Hz).
 */
    GLboolean vgl_init_ok = vglInitExtended(0, 960, 544, 6 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);
    game_log("[GL] vglInitExtended ret=%d\n", (int) vgl_init_ok);
#ifdef LOCK_FPS_30
/**
 * @brief Conservative factory clocks (CPU 333MHz / bus 166MHz / GPU 111MHz).
 */
    vglWaitVblankStart(GL_FALSE);
#endif
    gl_active = 1;
}

/**
 * @brief Conservative factory clocks (CPU 333MHz / bus 166MHz / GPU 111MHz).
 */
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

/**< @brief Conservative factory clocks (CPU 333MHz / bus 166MHz / GPU 111MHz). */
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
		log_active_frame_buf("post-gl_init");
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

/**
 * @brief Boot sequence -- replaces the single NativeInit() Zenonia 2 (which does not exist here).
 */
		game_log("Llamando JNI_OnLoad...\n");
		if (Game_JNI_OnLoad) Game_JNI_OnLoad(&jvm, NULL);
		game_log("Llamando NativeInitWithBufferSize(%d,%d)...\n", GAME_W, GAME_H);
		if (NativeInitWithBufferSize) NativeInitWithBufferSize(jniEnv, NULL, GAME_W, GAME_H);
		game_log("Llamando NativeInitDeviceInfo(%d,%d)...\n", GAME_W, GAME_H);
		if (NativeInitDeviceInfo) NativeInitDeviceInfo(jniEnv, NULL, GAME_W, GAME_H);
/**
 * @brief Boot sequence -- replaces the single NativeInit() Zenonia 2 (which does not exist here).
 */
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

/**
 * @brief Boot sequence -- replaces the single NativeInit() Zenonia 2 (which does not exist here).
 */
		int fps_count = 0;
		SceUInt64 fps_window_start = sceKernelGetProcessTimeWide();

		while (1) {
			sceCtrlPeekBufferPositive(0, &pad, 1);
			sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);

			if ((frame++ % 120) == 0) {
				game_log("frame %d alive, touch.reportNum=%d pad.buttons=0x%08x ui_status=%d\n", frame, touch.reportNum, (unsigned int) pad.buttons, g_ui_status);
				log_active_frame_buf("main-loop");
				GLenum gl_err = glGetError();
				if (gl_err != GL_NO_ERROR) game_log("[GL] glGetError=0x%04x en frame %d\n", gl_err, frame);
			}

/**< @brief The SCREEN size, not the buffer size. */
			if ((pad.buttons & SCE_CTRL_START) && (pad.buttons & SCE_CTRL_SELECT)) break;

/**< @brief The SCREEN size, not the buffer size. */
			if (g_ui_status == UI_STATUS_EXIT) break;

			unsigned int pressed = pad.buttons & ~old_buttons;
			unsigned int released = old_buttons & ~pad.buttons;

/**
 * @brief Boot sequence -- replaces the single NativeInit() Zenonia 2 (which does not exist here).
 */
			if (g_ui_status == 4 || g_ui_status == 5) {
				if (pad.buttons & SCE_CTRL_UP)   androidui_scroll_info_text(g_ui_status, -8.0f, SCREEN_W, SCREEN_H);
				if (pad.buttons & SCE_CTRL_DOWN) androidui_scroll_info_text(g_ui_status,  8.0f, SCREEN_W, SCREEN_H);
				pressed  &= ~(SCE_CTRL_UP | SCE_CTRL_DOWN);
				released &= ~(SCE_CTRL_UP | SCE_CTRL_DOWN);
			}

/**
 * @brief L+R toggles the virtual dpad/action-button cluster on/off at runtime.
 * L alone is also mapped to HAL_KEY_SAVE (btn_map), so while R is held the L
 * press/release edge is diverted here instead of falling into the generic
 * btn_map loop below -- otherwise every toggle would also fire a save.
 */
#define DPAD_TOGGLE_COMBO (SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER)
			static int l_mapped_to_save = 0;
			if ((pad.buttons & DPAD_TOGGLE_COMBO) == DPAD_TOGGLE_COMBO &&
			    (old_buttons & DPAD_TOGGLE_COMBO) != DPAD_TOGGLE_COMBO) {
				zenonia_toggle_dpad_visibility();
			}
			if (pressed & SCE_CTRL_LTRIGGER) {
				l_mapped_to_save = !(pad.buttons & SCE_CTRL_RTRIGGER);
				if (l_mapped_to_save)
					queue_input_event(MH_KEY_PRESSEVENT, HAL_KEY_SAVE, 0, 0);
			}
			if (released & SCE_CTRL_LTRIGGER) {
				if (l_mapped_to_save)
					queue_input_event(MH_KEY_RELEASEEVENT, HAL_KEY_SAVE, 0, 0);
			}
			pressed  &= ~SCE_CTRL_LTRIGGER;
			released &= ~SCE_CTRL_LTRIGGER;

			for (int i = 0; i < BTN_MAP_COUNT; i++) {
				if (btn_map[i].btn == SCE_CTRL_LTRIGGER)
					continue;
				if (pressed & btn_map[i].btn)
					queue_input_event(MH_KEY_PRESSEVENT, btn_map[i].hal, 0, 0);
				if (released & btn_map[i].btn)
					queue_input_event(MH_KEY_RELEASEEVENT, btn_map[i].hal, 0, 0);
			}
			old_buttons = pad.buttons;

/**< @brief The SCREEN size, not the buffer size. */
			if (touch.reportNum > 0) {
				int x = touch.report[0].x * GAME_W / 1920;
				int y = touch.report[0].y * GAME_H / 1088;

				if (!last_touch) {
/**
 * @brief Real FPS counter (to compare A/B builds of the RGB565_LUT/NEON_FIXED optimizations against the same point of reference in the log, instead).
 */
					int sx = touch.report[0].x * SCREEN_W / 1920;
					int sy = touch.report[0].y * SCREEN_H / 1088;

					if (g_ui_status == 1) {
/**
 * @brief The SCREEN size, not the buffer size.
 */
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
/**
 * @brief Natives.showTitleComponent().
 */
						androidui_backbtn_hit hit = androidui_backbtn_hit_test((float) sx, (float) sy, SCREEN_W, SCREEN_H);
						if (hit == ANDROIDUI_BACKBTN_HIT_BACK) {
							queue_input_event(MH_KEY_PRESSEVENT, HAL_KEY_BACK, 0, 0);
							queue_input_event(MH_KEY_RELEASEEVENT, HAL_KEY_BACK, 0, 0);
							last_touch_suppressed = 1;
						} else {
							last_touch_suppressed = 0;
						}
					} else if (g_ui_status == 5000) {
/**
 * @brief Natives.showTitleComponent().
 */
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

/**
 * @brief While the engine is in logo/title (Java UI states, invisible in the native loader -- pending confirmation real status numbers of Zenonia 3).
 */
			if (g_ui_status >= 0 && g_ui_status <= 1) splash_draw();

/**
 * @brief While the engine is in logo/title (Java UI states, invisible in the native loader -- pending confirmation real status numbers of Zenonia 3).
 */
			androidui_draw(g_ui_status, SCREEN_W, SCREEN_H);

#ifdef LOCK_FPS_30
/**
 * @brief While the engine is in logo/title (Java UI states, invisible in the native loader -- pending confirmation real status numbers of Zenonia 3).
 */
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
