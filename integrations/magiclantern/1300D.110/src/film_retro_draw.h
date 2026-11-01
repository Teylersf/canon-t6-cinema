#ifndef FILM_RETRO_DRAW_H
#define FILM_RETRO_DRAW_H
/* Bounded native 720x480 layout; callbacks permit the identical drawing code
 * to be inspected on a host. Palette/font rendering is platform specific. */
enum { FILM_RETRO_BLACK, FILM_RETRO_AMBER, FILM_RETRO_DIM, FILM_RETRO_PAPER };
enum { FILM_RETRO_SMALL, FILM_RETRO_MEDIUM, FILM_RETRO_LARGE };
struct film_retro_view {
    int page, selected, help, busy, movie, auto_on, effect, amber, stock_count, stock;
    const char *stock_name, *status;
};
struct film_retro_canvas {
    void *ctx;
    void (*fill)(void *, int, int, int, int, int);
    void (*text)(void *, int, int, int, int, const char *);
};
void film_retro_draw(const struct film_retro_view *, const struct film_retro_canvas *);
#endif
