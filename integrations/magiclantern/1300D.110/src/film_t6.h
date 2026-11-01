/* T6 Cinema, GPL-2.0. No camera addresses or firmware patches live here. */
#ifndef FILM_T6_H
#define FILM_T6_H

#define FILM_T6_MAX_STOCKS 16
#define FILM_T6_PATH 96

struct film_t6_settings {
    char stock[32];
    int effect; /* 0 off, 1 subtle, 2 cinema, 3 strong */
    int amber;
};

int film_t6_auto_enabled(void);
int film_t6_take_request(void);
int film_t6_bench_pending(void);
int film_t6_take_bench_request(void);
int film_t6_cancelled(void);
void film_t6_clear_cancel(void);
void film_t6_snapshot(struct film_t6_settings *settings);
void film_t6_load_stocks(void);
void film_t6_set_status(const char *status);
void film_t6_set_processing(int processing);

#endif
