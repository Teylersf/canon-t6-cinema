/*
 * Anamorphic-inspired post processing, not an optical lens simulation.
 * Highlights drive horizontal streaks and a wider-than-tall bloom. Vignette
 * and subtle lateral RGB offsets are optional. This cannot recover depth,
 * oval bokeh or an anamorphic field of view. It never squeezes the image.
 * Integer C99; rows are read once, no full-frame allocation. GPL v2.
 */
#ifndef SE_ANAMORPHIC_H
#define SE_ANAMORPHIC_H

#include "se.h"

#define SE_ANAMORPHIC_MAX_WIDTH 16384
#define SE_ANAMORPHIC_MAX_HEIGHT 65535
#define SE_ANAMORPHIC_MAX_RADIUS 2048
#define SE_ANAMORPHIC_MAX_VERTICAL 8

typedef struct {
    int strength;          /* 0..100: zero is a bit-exact bypass */
    int streak;            /* 0..100: multiscale horizontal flare */
    int bloom;             /* 0..100: anisotropic highlight bloom */
    int vignette;          /* 0..100: smooth edge darkening */
    int chromatic;         /* 0..100: lateral R/B offset, <=2 pixels */
    int threshold;         /* 0..255; 255 disables highlight extraction */
    int streak_radius;     /* 1..2048 output pixels */
    int vertical_radius;   /* 0..8 output rows of bloom */
    uint8_t tint[3];       /* streak RGB; bloom uses a paler tint */
} se_anamorphic;

void se_anamorphic_default(se_anamorphic *p);
/* Shared host/camera recipes: 0 Off, 1 Subtle, 2 Cinema, 3 Strong.
 * tint=0 cool blue, tint=1 warm amber. Width scales flare reach. */
void se_anamorphic_preset(se_anamorphic *p, int preset, int tint, int width);

typedef struct se_anamorphic_run se_anamorphic_run;
typedef int (*se_anamorphic_emit)(void *ctx, const uint8_t *row, int width);

/* Includes up to seven bytes of arena alignment padding. Zero means invalid
 * arguments. Out-of-range parameters are clamped consistently by all APIs. */
size_t se_anamorphic_bytes(const se_anamorphic *p, int width);
/* Reduce vertical bloom radius to fit. Returns zero if even radius 0 cannot
 * fit; never silently disables other requested effects. */
size_t se_anamorphic_fit(se_anamorphic *p, int width, size_t budget);
se_err se_anamorphic_begin(se_anamorphic_run **run, const se_anamorphic *p,
                          int width, int height, se_arena *ar);
/* Rows are contiguous RGB8, top to bottom. A nonzero emit result is sticky
 * SE_E_IO. Do not mutate/free input while rows() is running. Output pointers
 * belong to the run and are valid only until the callback returns. */
se_err se_anamorphic_rows(se_anamorphic_run *run, const uint8_t *rgb,
                         int nrows, se_anamorphic_emit emit, void *ctx);
/* Require exactly height input rows, then flush delayed rows. */
se_err se_anamorphic_end(se_anamorphic_run *run,
                        se_anamorphic_emit emit, void *ctx);

#endif
