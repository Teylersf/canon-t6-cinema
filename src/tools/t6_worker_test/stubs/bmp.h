#ifndef T6_TEST_BMP_H
#define T6_TEST_BMP_H
#include "dryos.h"
extern int mock_display_on;
#define DISPLAY_IS_ON mock_display_on
#define COLOR_BLACK 2
#define COLOR_ORANGE 19
#define COLOR_YELLOW 15
#define COLOR_EMPTY 0
#define NO_BG_ERASE 0xff
#define COLOR_GRAY(n) (38+(n)*41/100)
#define FONT_SMALL (0u<<16)
#define FONT_MONO_20 (1u<<16)
#define FONT_LARGE (4u<<16)
#define FONT(f,fg,bg) (((f)&0xffff0000u)|((unsigned)(bg)<<8)|(unsigned)(fg))
void bmp_fill(uint8_t color,int x,int y,int width,int height);
int bmp_printf(uint32_t font,int x,int y,const char *format,...);
#endif
