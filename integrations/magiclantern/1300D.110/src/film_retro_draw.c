/* T6 Cinema retro console, GPL-2.0. No allocations, file I/O or addresses. */
#include "film_retro_draw.h"

static void bounded_text(const struct film_retro_canvas *c, int x, int y,
                         int size, int color, const char *s, int limit)
{
    char text[73]; int i = 0;
    if (!s) s = "";
    if (limit > 72) limit = 72;
    while (i < limit && s[i]) { text[i] = s[i] >= 32 && s[i] < 127 ? s[i] : '?'; i++; }
    text[i] = 0; c->text(c->ctx, x, y, size, color, text);
}
static void line(const struct film_retro_canvas *c, int y, int color)
{ c->fill(c->ctx, 24, y, 672, 2, color); }
void film_retro_draw(const struct film_retro_view *v, const struct film_retro_canvas *c)
{
    static const char *fx[] = {"OFF", "SUBTLE", "CINEMA", "STRONG"};
    static const char *look_labels[] = {"FILM STOCK", "CINEMA EFFECTS", "STREAK TINT"};
    static const char *session_labels[] = {"AUTO NEW PHOTOS", "DEVELOP LATEST", "CANCEL / STOP"};
    const char *value[3]; int row, page, effect;
    char counter[8];
    if (!v || !c || !c->fill || !c->text) return;
    page = v->page == 1; effect = v->effect >= 0 && v->effect < 4 ? v->effect : 0;
    c->fill(c->ctx, 0, 0, 720, 480, FILM_RETRO_BLACK);
    /* Film perforations are plain bounded rectangles in the native palette. */
    for (row = 0; row < 12; row++) {
        c->fill(c->ctx, 4, 12 + row * 39, 8, 17, FILM_RETRO_DIM);
        c->fill(c->ctx, 708, 12 + row * 39, 8, 17, FILM_RETRO_DIM);
    }
    bounded_text(c, 28, 17, FILM_RETRO_LARGE, FILM_RETRO_AMBER, "T6 CINEMA", 20);
    bounded_text(c, 28, 57, FILM_RETRO_SMALL, FILM_RETRO_DIM, "RETRO CONTROL / EXPERIMENTAL / STILLS ONLY", 60);
    counter[0] = '0'; counter[1] = (char)('1' + page); counter[2] = '/';
    counter[3] = '0'; counter[4] = '2'; counter[5] = 0;
    bounded_text(c, 600, 25, FILM_RETRO_MEDIUM, FILM_RETRO_AMBER, counter, 5);
    line(c, 80, FILM_RETRO_AMBER);
    bounded_text(c, 28, 94, FILM_RETRO_MEDIUM, FILM_RETRO_AMBER,
                 v->help ? "CONTROL GUIDE" : page ? "SHOOT DESK" : "LOOK DECK", 24);
    if (v->help) {
        static const char *help[] = {
            "UP/DOWN       SELECT A ROW",
            "LEFT/RIGHT    CHANGE VALUE",
            "DIAL          CHANGE VALUE",
            "SET           CHANGE / RUN ACTION",
            "Q             SWITCH DECK",
            "DISP          TOGGLE THIS GUIDE",
            "MENU          CLOSE DASHBOARD",
            "SHUTTER / AV/ERASE / PLAY / LV / ZOOM",
            "LEAVE DASHBOARD FOR NATIVE CONTROLS"
        };
        for (row = 0; row < 9; row++)
            bounded_text(c, 32, 140 + row * 23, FILM_RETRO_SMALL,
                         row < 7 ? FILM_RETRO_PAPER : FILM_RETRO_DIM, help[row], 66);
    } else {
        value[0] = page ? (v->auto_on ? "ON" : "OFF") :
                          (v->stock_count > 0 ? v->stock_name : "NO STOCKS");
        value[1] = page ? (v->busy ? "WORKING - WAIT" : "PRESS SET") : fx[effect];
        value[2] = page ? "PRESS SET" : (v->amber ? "AMBER" : "BLUE");
        for (row = 0; row < 3; row++) {
            int y = 138 + row * 63, selected = row == v->selected;
            int color = selected ? FILM_RETRO_BLACK : FILM_RETRO_AMBER;
            c->fill(c->ctx, 24, y - 7, 672, 55, selected ? FILM_RETRO_AMBER : FILM_RETRO_BLACK);
            bounded_text(c, 36, y, FILM_RETRO_SMALL, color,
                         page ? session_labels[row] : look_labels[row], 30);
            bounded_text(c, 36, y + 17, FILM_RETRO_MEDIUM, color, value[row], 32);
        }
        bounded_text(c, 28, 338, FILM_RETRO_SMALL, FILM_RETRO_DIM,
                     v->movie ? "MOVIE MODE: NATIVE CONTROLS" :
                     v->busy ? "PROCESSING STILL JPEG / CANCEL REMAINS AVAILABLE" :
                     "SEPARATE JPEG OUTPUT / SOURCE CR2 PRESERVED", 68);
    }
    line(c, 368, FILM_RETRO_DIM);
    bounded_text(c, 28, 383, FILM_RETRO_SMALL, FILM_RETRO_PAPER, v->status, 68);
    bounded_text(c, 28, 412, FILM_RETRO_SMALL, FILM_RETRO_AMBER,
                 "Q PAGE   DISP GUIDE   MENU CLOSE", 55);
    bounded_text(c, 28, 439, FILM_RETRO_SMALL, FILM_RETRO_DIM,
                 "PHOTO EFFECTS ONLY / VIDEO EFFECTS UNAVAILABLE", 68);
}
