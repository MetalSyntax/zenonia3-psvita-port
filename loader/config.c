/**
 * @file  config.c
 * @brief Port settings + button bindings, see config.h.
 *
 * config.txt:   fps = 30 | 22, virtual_buttons = 0 | 100
 * controls.txt: ACTION = BUTTON[, BUTTON...]   (NONE = unbound)
 */

#include "config.h"

#include <psp2/ctrl.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>

extern void game_log(const char *fmt, ...);

#ifndef CONFIG_PATH /* overridable for the host-side round-trip check */
#define CONFIG_PATH   "ux0:data/zenonia3/config.txt"
#define CONTROLS_PATH "ux0:data/zenonia3/controls.txt"
#endif

int cfg_fps = 30;
int cfg_vbuttons_opacity = 0;
uint32_t cfg_binds[ACT_COUNT];

/* Key codes: NexusHal.java MH_KEY_* (the engine also accepts them from its own touch buttons). */
static const struct { const char *id, *label; int hal; uint32_t def; } actions[ACT_COUNT] = {
    [ACT_UP]    = { "UP",    "Move up",                 -1,  SCE_CTRL_UP },
    [ACT_DOWN]  = { "DOWN",  "Move down",               -2,  SCE_CTRL_DOWN },
    [ACT_LEFT]  = { "LEFT",  "Move left",               -3,  SCE_CTRL_LEFT },
    [ACT_RIGHT] = { "RIGHT", "Move right",              -4,  SCE_CTRL_RIGHT },
    [ACT_OK]    = { "OK",    "Attack / OK",             -5,  SCE_CTRL_CROSS },
    [ACT_BACK]  = { "BACK",  "Back / Cancel / Menu",    -16, SCE_CTRL_CIRCLE },
    [ACT_SKIP]  = { "SKIP",  "Skip / Special (#)",      35,  SCE_CTRL_TRIANGLE },
    [ACT_MAP]   = { "MAP",   "Map / Quick item",        -6,  SCE_CTRL_SQUARE },
    [ACT_SAVE]  = { "SAVE",  "Save",                    -10, SCE_CTRL_LTRIGGER },
};

/* START is reserved: it cancels a capture and opens the menu with SELECT. */
static const struct { const char *name; uint32_t btn; } buttons[] = {
    { "CROSS", SCE_CTRL_CROSS }, { "CIRCLE", SCE_CTRL_CIRCLE },
    { "SQUARE", SCE_CTRL_SQUARE }, { "TRIANGLE", SCE_CTRL_TRIANGLE },
    { "L1", SCE_CTRL_LTRIGGER }, { "R1", SCE_CTRL_RTRIGGER },
    { "UP", SCE_CTRL_UP }, { "DOWN", SCE_CTRL_DOWN },
    { "LEFT", SCE_CTRL_LEFT }, { "RIGHT", SCE_CTRL_RIGHT },
    { "SELECT", SCE_CTRL_SELECT },
};
#define BUTTON_COUNT (int) (sizeof(buttons) / sizeof(buttons[0]))

const char *action_label(int a) { return actions[a].label; }
int action_hal(int a) { return actions[a].hal; }

uint32_t config_bindable_buttons(void) {
    uint32_t m = 0;
    for (int i = 0; i < BUTTON_COUNT; i++) m |= buttons[i].btn;
    return m;
}

void config_bind(int a, uint32_t btn, int add) {
    for (int i = 0; i < ACT_COUNT; i++) cfg_binds[i] &= ~btn;
    cfg_binds[a] = add ? (cfg_binds[a] | btn) : btn;
}

void config_buttons_text(uint32_t mask, char *out, size_t size) {
    out[0] = '\0';
    for (int i = 0; i < BUTTON_COUNT; i++) {
        if (!(mask & buttons[i].btn)) continue;
        size_t len = strlen(out);
        snprintf(out + len, size - len, "%s%s", len ? ", " : "", buttons[i].name);
    }
    if (!out[0]) snprintf(out, size, "NONE");
}

void config_defaults(void) {
    cfg_fps = 30;
    cfg_vbuttons_opacity = 0;
    for (int i = 0; i < ACT_COUNT; i++) cfg_binds[i] = actions[i].def;
}

/* Splits "key = value" in place, trimmed and upper-cased key. 0 if the line has no '='. */
static int split_line(char *line, char **key, char **val) {
    char *eq = strchr(line, '=');
    if (!eq || line[0] == '#') return 0;
    *eq = '\0';
    char *k = line, *v = eq + 1;
    while (isspace((unsigned char) *k)) k++;
    while (isspace((unsigned char) *v)) v++;
    for (char *e = k + strlen(k); e > k && isspace((unsigned char) e[-1]); ) *--e = '\0';
    for (char *e = v + strlen(v); e > v && isspace((unsigned char) e[-1]); ) *--e = '\0';
    for (char *p = k; *p; p++) *p = (char) toupper((unsigned char) *p);
    *key = k; *val = v;
    return 1;
}

static uint32_t parse_buttons(char *val) {
    uint32_t mask = 0;
    for (char *tok = strtok(val, ", \t"); tok; tok = strtok(NULL, ", \t")) {
        for (char *p = tok; *p; p++) *p = (char) toupper((unsigned char) *p);
        for (int i = 0; i < BUTTON_COUNT; i++)
            if (!strcmp(tok, buttons[i].name)) mask |= buttons[i].btn;
    }
    return mask;
}

void config_load(void) {
    config_defaults();
    char line[256], *key, *val;

    FILE *f = fopen(CONFIG_PATH, "r");
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            if (!split_line(line, &key, &val)) continue;
            int n = 0;
            sscanf(val, "%d", &n);
            if (!strcmp(key, "FPS")) cfg_fps = n == 22 ? 22 : 30;
            else if (!strcmp(key, "VIRTUAL_BUTTONS")) cfg_vbuttons_opacity = n > 0 ? 100 : 0;
        }
        fclose(f);
    }

    f = fopen(CONTROLS_PATH, "r");
    if (f) {
        for (int i = 0; i < ACT_COUNT; i++) cfg_binds[i] = 0;
        while (fgets(line, sizeof(line), f)) {
            if (!split_line(line, &key, &val)) continue;
            for (int i = 0; i < ACT_COUNT; i++)
                if (!strcmp(key, actions[i].id)) cfg_binds[i] = parse_buttons(val);
        }
        fclose(f);
    }
    game_log("[CONFIG] fps=%d virtual_buttons=%d%%\n", cfg_fps, cfg_vbuttons_opacity);
}

void config_save(void) {
    FILE *f = fopen(CONFIG_PATH, "w");
    if (f) {
        fprintf(f, "# 30 or 22 (22 = the speed the original game runs at)\nfps = %d\n", cfg_fps);
        fprintf(f, "# On-screen D-pad/action buttons opacity: 0 (hidden, still touchable) or 100\n"
                   "virtual_buttons = %d\n", cfg_vbuttons_opacity);
        fclose(f);
    }
    f = fopen(CONTROLS_PATH, "w");
    if (f) {
        char text[96];
        fprintf(f, "# ACTION = BUTTON[, BUTTON...]  (CROSS CIRCLE SQUARE TRIANGLE L1 R1 UP DOWN LEFT RIGHT SELECT, or NONE)\n");
        for (int i = 0; i < ACT_COUNT; i++) {
            config_buttons_text(cfg_binds[i], text, sizeof(text));
            fprintf(f, "%s = %s\n", actions[i].id, text);
        }
        fclose(f);
    }
    game_log("[CONFIG] guardado: fps=%d virtual_buttons=%d%%\n", cfg_fps, cfg_vbuttons_opacity);
}
