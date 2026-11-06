/**
 * @file  vitamenu.h
 * @brief In-game "PS Vita options" menu (Start + Select): button remapping, FPS, virtual buttons.
 */

#ifndef VITAMENU_H
#define VITAMENU_H

#include <stdint.h>

int vitamenu_active(void);
void vitamenu_open(void);
void vitamenu_close(void);                         /**< Saves config.txt + controls.txt. */
void vitamenu_update(uint32_t held, uint32_t pressed);
void vitamenu_draw(int screen_w, int screen_h);

#endif
