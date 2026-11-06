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
#include "config.h"
#include "vitamenu.h"
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
void game_log(const char *fmt, ...);
static SceUInt64 log_t0, log_last_flush;
static int log_line_start = 1;

/**
 * @brief logs/log_001.log ... log_999.log, numbered by logs/log_counter.txt (wraps 999 -> 001,
 * overwriting the oldest). Every line is prefixed with the time since boot.
 */
void init_log() {
    sceIoMkdir(LOG_DIR, 0777); // falla en silencio si ya existe

    int n = 0;
    FILE *c = fopen(LOG_DIR "/log_counter.txt", "r");
    if (c) {
        if (fscanf(c, "%d", &n) != 1) n = 0;
        fclose(c);
    }
    n = (n >= 1 && n < 999) ? n + 1 : 1;
    c = fopen(LOG_DIR "/log_counter.txt", "w");
    if (c) {
        fprintf(c, "%d\n", n);
        fclose(c);
    }

    char log_path[256];
    snprintf(log_path, sizeof(log_path), LOG_DIR "/log_%03d.log", n);
    log_t0 = sceKernelGetProcessTimeWide();
    log_file = fopen(log_path, "w");
    // Buffered: a fflush per line cost ~3s of a ~4.4s save load (370 asset lines, log_002).
    if (log_file) setvbuf(log_file, NULL, _IOFBF, 64 * 1024);

    char date[32] = "";
    time_t t = time(NULL);
    strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", localtime(&t));
    game_log("--- ZENONIA 3 PORT LOG START (%s, %s) ---\n", log_path, date);
}

void game_log(const char *fmt, ...) {
    va_list list;
    char string[512];

    va_start(list, fmt);
    vsnprintf(string, sizeof(string), fmt, list);
    va_end(list);

    if (log_file) {
        if (log_line_start) {
            unsigned int ms = (unsigned int) ((sceKernelGetProcessTimeWide() - log_t0) / 1000);
            fprintf(log_file, "[%02u:%02u:%02u.%03u] ", ms / 3600000, ms / 60000 % 60, ms / 1000 % 60, ms % 1000);
        }
        size_t len = strlen(string);
        log_line_start = len > 0 && string[len - 1] == '\n';
        fputs(string, log_file);
        // Written out once a second, and right away for anything that may precede a crash.
        // ponytail: a hard crash can lose up to ~1s of routine lines; the crash dump covers that.
        SceUInt64 now = sceKernelGetProcessTimeWide();
        if (now - log_last_flush >= 1000000 || strstr(string, "ERR") || strstr(string, "WARN") ||
            strstr(string, "FATAL")) {
            fflush(log_file);
            log_last_flush = now;
        }
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

/**
 * @brief Main-menu button -> the event its Android ImageButton sent (Natives.java). Shared by touch
 * and the d-pad cursor. 0 if nothing was hit.
 */
static int press_menu_button(androidui_menu_hit hit) {
    int key;
    switch (hit) {
        case ANDROIDUI_MENU_HIT_COMMUNITY: key = HAL_KEY_LEFT;  break;
        case ANDROIDUI_MENU_HIT_OPTIONS:   key = HAL_KEY_UP;    break;
        case ANDROIDUI_MENU_HIT_NEWGAME:   key = HAL_KEY_OK;    break;
        case ANDROIDUI_MENU_HIT_HELP:      key = HAL_KEY_DOWN;  break;
        case ANDROIDUI_MENU_HIT_ABOUT:     key = HAL_KEY_RIGHT; break;
        case ANDROIDUI_MENU_HIT_CONTINUE:
            queue_input_event(NEXUS_HAL_REPLY_YESNO, NEXUS_HAL_FIRST_MOVE_REPLY_PAGE, 0, 0);
            return 1;
        default:
            return 0;
    }
    queue_input_event(MH_KEY_PRESSEVENT, key, 0, 0);
    queue_input_event(MH_KEY_RELEASEEVENT, key, 0, 0);
    return 1;
}

/**
 * @brief Actions held this frame: bound buttons (config.c) + the left stick, which always moves.
 */
static uint32_t actions_held(uint32_t buttons, const SceCtrlData *pad) {
    uint32_t acts = 0;
    for (int a = 0; a < ACT_COUNT; a++)
        if (buttons & cfg_binds[a]) acts |= 1u << a;
    if (pad->lx < 64) acts |= 1u << ACT_LEFT; else if (pad->lx > 192) acts |= 1u << ACT_RIGHT;
    if (pad->ly < 64) acts |= 1u << ACT_UP;   else if (pad->ly > 192) acts |= 1u << ACT_DOWN;
    return acts;
}

/**
 * @brief NOTE: unlike Zenonia 2, no binary patch is carried here (apply_so_patches).
 */

extern volatile int g_ui_status;
extern void zenonia_install_array_hooks(void);
extern void zenonia_install_vbuttons_hook(so_module *mod);

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
    gl_active = 1;
}

/**
 * @brief 30fps: vitaGL's display-queue thread waits 2 vblanks after each flip, so CPU work overlaps
 * the wait (a main-thread sceDisplayWaitVblankStartMulti(2) counts from the END of the frame: any
 * frame >16.7ms of work fell to 20fps). 22fps: what the game asks for (SetSpeed(22), the Android
 * NexusGLThread sleeps to 1000/22 ms); not a vblank divisor, so the main loop sleeps to the period
 * and vsync stays at 1 vblank.
 */
static void apply_fps(void) {
    eglSwapInterval(0, cfg_fps == 22 ? 1 : 2);
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
    config_load();
    config_save(); // writes config.txt/controls.txt with every key, so they can be edited by hand
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
		apply_fps();
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
		zenonia_install_vbuttons_hook(&zenonia3_mod); // ver dynlib.c -- opacidad 0/100 de cruceta+botones de accion

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
		sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

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

			if (g_ui_status == UI_STATUS_EXIT) break;

			unsigned int pressed = pad.buttons & ~old_buttons;

/**
 * @brief Start + Select opens/closes the PS Vita options menu (vitamenu.c). Buttons still held
 * when it closes are ignored until released, so the closing Circle doesn't reach the game.
 */
#define MENU_COMBO (SCE_CTRL_START | SCE_CTRL_SELECT)
			static uint32_t suppressed = 0;
			int menu_was_open = vitamenu_active();
			if ((pad.buttons & MENU_COMBO) == MENU_COMBO && (old_buttons & MENU_COMBO) != MENU_COMBO) {
				if (menu_was_open) vitamenu_close(); else vitamenu_open();
			} else if (menu_was_open) {
				vitamenu_update(pad.buttons, pressed);
			}
			if (menu_was_open && !vitamenu_active()) {
				apply_fps();
				suppressed = pad.buttons;
			}
			// Hotkeys (outside the menu): R + L toggles the on-screen buttons, R + Select the 30/22 fps.
			// The combo's buttons are kept out of the actions until released (L is Save by default).
#define COMBO_VBUTTONS (SCE_CTRL_RTRIGGER | SCE_CTRL_LTRIGGER)
#define COMBO_FPS      (SCE_CTRL_RTRIGGER | SCE_CTRL_SELECT)
			if (!vitamenu_active() && !menu_was_open) {
				if ((pad.buttons & COMBO_VBUTTONS) == COMBO_VBUTTONS && (old_buttons & COMBO_VBUTTONS) != COMBO_VBUTTONS) {
					cfg_vbuttons_opacity = cfg_vbuttons_opacity ? 0 : 100;
					config_save();
				}
				if ((pad.buttons & COMBO_FPS) == COMBO_FPS && (old_buttons & COMBO_FPS) != COMBO_FPS) {
					cfg_fps = cfg_fps == 30 ? 22 : 30;
					apply_fps();
					config_save();
				}
				if ((pad.buttons & SCE_CTRL_RTRIGGER) && (pad.buttons & (SCE_CTRL_LTRIGGER | SCE_CTRL_SELECT)))
					suppressed |= pad.buttons & (SCE_CTRL_RTRIGGER | SCE_CTRL_LTRIGGER | SCE_CTRL_SELECT);
			}
			suppressed &= pad.buttons;
			old_buttons = pad.buttons;

			// Edges of the action state, not of raw buttons: remaps, the stick and opening the menu
			// (all actions released) all come out as plain press/release events.
			static uint32_t old_acts = 0;
			uint32_t acts = vitamenu_active() ? 0 : actions_held(pad.buttons & ~suppressed, &pad);
			if (g_ui_status == 4 || g_ui_status == 5) {
				// ABOUT/HELP: up/down scroll the port's own text view instead of reaching the engine.
				if (acts & (1u << ACT_UP))   androidui_scroll_info_text(g_ui_status, -8.0f, SCREEN_W, SCREEN_H);
				if (acts & (1u << ACT_DOWN)) androidui_scroll_info_text(g_ui_status,  8.0f, SCREEN_W, SCREEN_H);
				acts &= ~((1u << ACT_UP) | (1u << ACT_DOWN));
			}
			static uint32_t old_raw_acts = 0;
			uint32_t raw_acts = acts;
			if (g_ui_status == 2) {
				// MAINMENU: the d-pad moves a highlight over the 6 buttons and OK presses it, instead of
				// each direction firing a button straight away (Left = COMMUNITY, Up = OPTIONS, ...).
				uint32_t fresh = raw_acts & ~old_raw_acts;
				if (fresh & ((1u << ACT_LEFT) | (1u << ACT_UP)))    androidui_menu_cursor_move(-1);
				if (fresh & ((1u << ACT_RIGHT) | (1u << ACT_DOWN))) androidui_menu_cursor_move(1);
				if (fresh & (1u << ACT_OK)) {
					if (!press_menu_button(androidui_menu_cursor_hit()))
						androidui_menu_cursor_move(0); // OK with no highlight just shows it
				}
				acts &= ~((1u << ACT_UP) | (1u << ACT_DOWN) | (1u << ACT_LEFT) | (1u << ACT_RIGHT) | (1u << ACT_OK));
			}
			old_raw_acts = raw_acts;
			for (int a = 0; a < ACT_COUNT; a++) {
				uint32_t bit = 1u << a;
				if ((acts ^ old_acts) & bit)
					queue_input_event((acts & bit) ? MH_KEY_PRESSEVENT : MH_KEY_RELEASEEVENT, action_hal(a), 0, 0);
			}
			old_acts = acts;

/**< @brief The SCREEN size, not the buffer size. */
			if (touch.reportNum > 0 && !vitamenu_active()) {
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
						androidui_menu_cursor_hide();
						last_touch_suppressed = press_menu_button(androidui_menu_hit_test((float) sx, (float) sy, SCREEN_W, SCREEN_H));
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

			if (vitamenu_active()) {
				// Paused: NativeRender() is not called, the game freezes under the menu.
				vitamenu_draw(SCREEN_W, SCREEN_H);
			} else {
				// Drain every queued event: Android delivers input asynchronously, and the engine itself fires
				// press+release back-to-back for event 0x1f. One-per-frame added latency and, once the 16-slot
				// queue filled, silently dropped events (a lost RELEASE = stuck direction).
				while (handleCletEvent && eq_head != eq_tail) {
					input_event *ev = &event_queue[eq_head];
					eq_head = (eq_head + 1) % 16;
					handleCletEvent(jniEnv, NULL, ev->type, ev->p1, ev->p2, ev->p3);
				}

				if (NativeRender) NativeRender(jniEnv, NULL);

				// Logo/title: the port's own splash under the Android-UI art (see androidui.c).
				if (g_ui_status >= 0 && g_ui_status <= 1) splash_draw();
				androidui_draw(g_ui_status, SCREEN_W, SCREEN_H);
			}

			if (cfg_fps == 22) {
				// Fixed 1000/22 ms period from frame to frame, like NexusGLThread; resyncs after a hitch.
				static SceUInt64 next_frame = 0;
				const SceUInt64 period = 1000000 / 22;
				SceUInt64 t = sceKernelGetProcessTimeWide();
				if (t > next_frame + period) next_frame = t;
				next_frame += period;
				if (next_frame > t) sceKernelDelayThread((SceUInt) (next_frame - t));
			}
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
