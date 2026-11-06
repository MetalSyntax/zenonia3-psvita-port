/**
 * @file  config.h
 * @brief Port settings (ux0:data/zenonia3/config.txt) and button bindings
 *        (ux0:data/zenonia3/controls.txt), edited in-game from the
 *        Start + Select menu (vitamenu.c) or by hand.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>
#include <stdint.h>

/** Game actions: each one is a key code the engine receives through handleCletEvent. */
typedef enum {
    ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT,
    ACT_OK, ACT_BACK, ACT_SKIP, ACT_MAP, ACT_SAVE,
    ACT_COUNT
} action_id;

extern int cfg_fps;              /**< 30 (2 vblanks) or 22 (the speed the game asks for, SetSpeed(22)). */
extern int cfg_vbuttons_opacity; /**< Virtual D-pad/action buttons: 0 (invisible, still touchable) or 100. */
extern uint32_t cfg_binds[ACT_COUNT]; /**< SCE_CTRL_* mask per action. */

void config_defaults(void);
void config_load(void);  /**< Defaults, then whatever config.txt / controls.txt override. */
void config_save(void);

const char *action_label(int a);
int action_hal(int a);                 /**< Engine key code (MH_KEY_* / HAL_KEY_*). */
uint32_t config_bindable_buttons(void);
void config_bind(int a, uint32_t btn, int add); /**< A button belongs to one action only. */
void config_buttons_text(uint32_t mask, char *out, size_t size);

#endif
