/* Portable dashboard control state. GPL-2.0. No Canon event numbers here. */
#ifndef FILM_RETRO_STATE_H
#define FILM_RETRO_STATE_H

enum film_retro_key {
    FILM_KEY_SYSTEM, FILM_KEY_UNKNOWN, FILM_KEY_NATIVE,
    FILM_KEY_UP, FILM_KEY_DOWN, FILM_KEY_LEFT, FILM_KEY_RIGHT,
    FILM_KEY_SET, FILM_KEY_Q, FILM_KEY_DISP, FILM_KEY_INFO,
    FILM_KEY_MENU, FILM_KEY_DIAL_PREV, FILM_KEY_DIAL_NEXT,
    FILM_KEY_UP_RELEASE, FILM_KEY_DOWN_RELEASE,
    FILM_KEY_LEFT_RELEASE, FILM_KEY_RIGHT_RELEASE,
    FILM_KEY_SET_RELEASE, FILM_KEY_DISP_RELEASE
};
enum film_retro_route { FILM_RETRO_CONSUME = 0, FILM_RETRO_PASS = 1, FILM_RETRO_NATIVE = 2 };
enum film_retro_action {
    FILM_RETRO_REDRAW = 1, FILM_RETRO_CHANGED = 2,
    FILM_RETRO_DEVELOP = 4, FILM_RETRO_CANCEL = 8, FILM_RETRO_CLOSE = 16
};
struct film_retro_state {
    int visible, page, selected, help;
    unsigned held; /* releases remain paired after the screen closes */
};
struct film_retro_settings { int stock_count, stock, effect, amber, auto_on; };

void film_retro_open(struct film_retro_state *state);
unsigned film_retro_close(struct film_retro_state *state);
/* context=0 means menu/selection/display/capture/mode is no longer suitable.
 * processing is our still worker, not Canon capture: cancel remains usable.
 * Unknown/system events never dereference firmware payloads in this layer. */
int film_retro_step(struct film_retro_state *state, struct film_retro_settings *settings,
                    enum film_retro_key key, int context, int processing, unsigned *actions);
#endif
