#ifndef T6_TEST_MENU_H
#define T6_TEST_MENU_H
#include "dryos.h"
struct menu_entry;
struct menu_display_info { char *value; int can_custom_draw, custom_drawing; };
typedef void (*mock_select)(void *, int);
typedef void (*mock_update)(struct menu_entry *, struct menu_display_info *);
struct menu_entry {
    const char *name; void *priv; int max; const char **choices;
    mock_select select; mock_update update; const char *help, *help2; int selected;
};
#define MENU_SELECT_FUNC(f) void f(void *priv, int delta)
#define MENU_UPDATE_FUNC(f) void f(struct menu_entry *entry, struct menu_display_info *info)
#define MENU_SET_VALUE(...) snprintf(info->value, 35, __VA_ARGS__)
#define CHOICES(...) (const char *[]) {__VA_ARGS__}
#define COUNT(a) ((int)(sizeof(a)/sizeof((a)[0])))
enum { CUSTOM_DRAW_DISABLE, CUSTOM_DRAW_THIS_ENTRY, CUSTOM_DRAW_THIS_MENU };
void menu_redraw(void);
void gui_stop_menu(void);
int gui_menu_shown(void);
int is_menu_active(char *name);
int is_menu_entry_selected(char *menu, char *name);
void keyrepeat_ack(int key);
void menu_add(char *name, struct menu_entry *entries, int count);
#endif
