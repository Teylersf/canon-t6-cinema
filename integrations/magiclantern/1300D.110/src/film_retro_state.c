#include "film_retro_state.h"

static int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int wrap(int v, int count) {
    if (count <= 0) return 0;
    if (v < 0) return count - 1;
    return v >= count ? 0 : v;
}
void film_retro_open(struct film_retro_state *s) {
    if (!s) return;
    s->visible = 1; s->page = s->selected = s->help = 0;
    /* A key released after reopening must still match its swallowed press. */
}
unsigned film_retro_close(struct film_retro_state *s) {
    if (!s || !s->visible) return 0;
    s->visible = 0; s->help = 0;
    return FILM_RETRO_CLOSE | FILM_RETRO_REDRAW;
}
static unsigned press_bit(enum film_retro_key key) {
    switch (key) {
    case FILM_KEY_UP: case FILM_KEY_UP_RELEASE: return 1u;
    case FILM_KEY_DOWN: case FILM_KEY_DOWN_RELEASE: return 2u;
    case FILM_KEY_LEFT: case FILM_KEY_LEFT_RELEASE: return 4u;
    case FILM_KEY_RIGHT: case FILM_KEY_RIGHT_RELEASE: return 8u;
    case FILM_KEY_SET: case FILM_KEY_SET_RELEASE: return 16u;
    case FILM_KEY_DISP: case FILM_KEY_DISP_RELEASE: return 32u;
    default: return 0;
    }
}
static int release(enum film_retro_key key) {
    return key >= FILM_KEY_UP_RELEASE && key <= FILM_KEY_DISP_RELEASE;
}
int film_retro_step(struct film_retro_state *s, struct film_retro_settings *p,
                    enum film_retro_key key, int context, int processing, unsigned *actions)
{
    unsigned bit, a = 0;
    int delta, route = FILM_RETRO_CONSUME;
    if (actions) *actions = 0;
    if (!s || !p || !actions) return FILM_RETRO_PASS;
    bit = press_bit(key);
    if (release(key)) {
        if (bit && (s->held & bit)) { s->held &= ~bit; return FILM_RETRO_CONSUME; }
        return FILM_RETRO_PASS;
    }
    if (!s->visible) return FILM_RETRO_PASS;
    if (!context) {
        *actions = film_retro_close(s);
        return key == FILM_KEY_NATIVE || key == FILM_KEY_UNKNOWN ?
            FILM_RETRO_NATIVE : FILM_RETRO_PASS;
    }
    p->stock_count = clamp(p->stock_count, 0, 16);
    p->stock = clamp(p->stock, 0, p->stock_count ? p->stock_count - 1 : 0);
    p->effect = clamp(p->effect, 0, 3);
    p->amber = p->amber != 0; p->auto_on = p->auto_on != 0;
    s->page = clamp(s->page, 0, 1); s->selected = clamp(s->selected, 0, 2);
    if (key == FILM_KEY_SYSTEM) return FILM_RETRO_PASS;
    if (key == FILM_KEY_NATIVE || key == FILM_KEY_UNKNOWN) {
        *actions = film_retro_close(s);
        return FILM_RETRO_NATIVE;
    }
    if (key == FILM_KEY_MENU) { *actions = film_retro_close(s); return FILM_RETRO_CONSUME; }
    if (bit) s->held |= bit;
    if (key == FILM_KEY_DISP || key == FILM_KEY_INFO) {
        s->help = !s->help; a |= FILM_RETRO_REDRAW;
    } else if (key == FILM_KEY_Q) {
        s->page ^= 1; s->selected = 0; s->help = 0; a |= FILM_RETRO_REDRAW;
    } else if (s->help) {
        if (key == FILM_KEY_SET) s->help = 0;
        a |= FILM_RETRO_REDRAW;
    } else if (key == FILM_KEY_UP || key == FILM_KEY_DOWN) {
        s->selected = wrap(s->selected + (key == FILM_KEY_UP ? -1 : 1), 3);
        a |= FILM_RETRO_REDRAW;
    } else if (key == FILM_KEY_LEFT || key == FILM_KEY_RIGHT || key == FILM_KEY_SET ||
               key == FILM_KEY_DIAL_PREV || key == FILM_KEY_DIAL_NEXT) {
        delta = key == FILM_KEY_LEFT || key == FILM_KEY_DIAL_PREV ? -1 : 1;
        if (s->page == 0) {
            if (s->selected == 0) p->stock = wrap(p->stock + delta, p->stock_count);
            if (s->selected == 1) p->effect = wrap(p->effect + delta, 4);
            if (s->selected == 2) p->amber ^= 1;
            a |= FILM_RETRO_CHANGED;
        } else if (s->selected == 0) {
            p->auto_on ^= 1; a |= FILM_RETRO_CHANGED;
        } else if (key == FILM_KEY_SET) {
            if (s->selected == 1 && !processing && p->stock_count) a |= FILM_RETRO_DEVELOP;
            if (s->selected == 2) {
                p->auto_on = 0;
                a |= FILM_RETRO_CANCEL | FILM_RETRO_CHANGED;
            }
        }
        a |= FILM_RETRO_REDRAW;
    } else {
        /* No unrecognized semantic key may become a menu action. */
        a |= film_retro_close(s); route = FILM_RETRO_NATIVE;
    }
    *actions = a;
    return route;
}
