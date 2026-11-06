#ifndef ANDROIDUI_H
#define ANDROIDUI_H

/**
 * @brief Which menu button (if any) falls under (sx,sy).
 */
void androidui_load(int screen_w, int screen_h);
void androidui_draw(int ui_status, int screen_w, int screen_h);

/**
 * @brief Which menu button (if any) falls under (sx,sy).
 */
typedef enum {
    ANDROIDUI_MENU_HIT_NONE = 0,
    ANDROIDUI_MENU_HIT_COMMUNITY,
    ANDROIDUI_MENU_HIT_OPTIONS,
    ANDROIDUI_MENU_HIT_NEWGAME,
    ANDROIDUI_MENU_HIT_CONTINUE,
    ANDROIDUI_MENU_HIT_HELP,
    ANDROIDUI_MENU_HIT_ABOUT,
} androidui_menu_hit;

androidui_menu_hit androidui_menu_hit_test(float sx, float sy, int screen_w, int screen_h);

/* Main-menu cursor for the physical buttons: a highlight frame the d-pad moves across the 6
 * buttons (left to right, the order they sit on the arc). Hidden until the first d-pad press;
 * touching the screen hides it again. */
void androidui_menu_cursor_move(int delta);
void androidui_menu_cursor_hide(void);
androidui_menu_hit androidui_menu_cursor_hit(void); /* NONE while hidden */

/**
 * @brief Which menu button (if any) falls under (sx,sy).
 */
typedef enum {
    ANDROIDUI_REPLY_HIT_NONE = 0,
    ANDROIDUI_REPLY_HIT_WRITE,
    ANDROIDUI_REPLY_HIT_LATER,
} androidui_reply_hit;

androidui_reply_hit androidui_reply_hit_test(float sx, float sy, int screen_w, int screen_h);

/**
 * @brief ui_status==5000 (UI_STATUS_REPLY_PAGE): "rate the app" popup that Natives.
 */
typedef enum {
    ANDROIDUI_BACKBTN_HIT_NONE = 0,
    ANDROIDUI_BACKBTN_HIT_BACK,
} androidui_backbtn_hit;

androidui_backbtn_hit androidui_backbtn_hit_test(float sx, float sy, int screen_w, int screen_h);

/**
 * @brief ui_status==5000 (UI_STATUS_REPLY_PAGE): "rate the app" popup that Natives.
 */
void androidui_scroll_info_text(int ui_status, float delta_px, int screen_w, int screen_h);

#endif
