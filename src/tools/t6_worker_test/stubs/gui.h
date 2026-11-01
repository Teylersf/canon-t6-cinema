/* Exact symbolic values from pinned 142472e9 platform/1300D.110/gui.h.
 * These model events, not a claim that every defined wheel axis is physical. */
#ifndef T6_TEST_GUI_H
#define T6_TEST_GUI_H
#define BGMT_MENU 6
#define BGMT_INFO 7
#define BGMT_PRESS_DISP 8
#define BGMT_UNPRESS_DISP 9
#define BGMT_PLAY 0xB
#define BGMT_Q 0x1C
#define BGMT_LV 0x1D
#define BGMT_PRESS_SET 4
#define BGMT_UNPRESS_SET 5
#define BGMT_PRESS_RIGHT 0x23
#define BGMT_UNPRESS_RIGHT 0x24
#define BGMT_PRESS_LEFT 0x25
#define BGMT_UNPRESS_LEFT 0x26
#define BGMT_PRESS_UP 0x27
#define BGMT_UNPRESS_UP 0x28
#define BGMT_PRESS_DOWN 0x29
#define BGMT_UNPRESS_DOWN 0x2A
#define BGMT_ISO 0x33
#define BGMT_PRESS_HALFSHUTTER 0x48
#define BGMT_PRESS_ZOOM_OUT 0x10
#define BGMT_UNPRESS_ZOOM_OUT 0x11
#define BGMT_PRESS_ZOOM_IN 0xE
#define BGMT_UNPRESS_ZOOM_IN 0xF
#define BGMT_WHEEL_LEFT 2
#define BGMT_WHEEL_RIGHT 3
#define BGMT_WHEEL_UP 0
#define BGMT_WHEEL_DOWN 1
#define GMT_OLC_INFO_CHANGED 0x61
#define BGMT_AV (event->type == 0 && event->param == 0x61 && ( \
    (is_movie_mode() && (event->arg == 0xe || event->arg == 0xa)) || \
    (shooting_mode == SHOOTMODE_P && event->arg == 0xa) || \
    (shooting_mode == SHOOTMODE_ADEP && event->arg == 0xa) || \
    (shooting_mode == SHOOTMODE_AV && event->arg == 0xf) || \
    (shooting_mode == SHOOTMODE_M && event->arg == 0xe) || \
    (shooting_mode == SHOOTMODE_TV && event->arg == 0x10)))
#endif
