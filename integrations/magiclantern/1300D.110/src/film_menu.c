/** T6 Cinema: a built-in ML menu, separate from the original ML pages.
 * GPL-2.0. Experimental; firmware 1.1.0 only. No automatic activation.
 */
#include "dryos.h"
#include "mem.h"
#include "config.h"
#include "menu.h"
#include "fio-ml.h"
#include "film_t6.h"
#include "bmp.h"
#include "lens.h"
#include "propvalues.h"
#include "gui.h"
#include "gui-common.h"
#include "film_retro_state.h"
#include "film_retro_draw.h"
#include "film_retro_native.h"

#if !defined(CONFIG_1300D_110)
#error T6 Cinema must only be compiled for 1300D firmware 1.1.0
#endif

/* Intentionally not CONFIG_INT: processing always starts OFF after boot. */
static int film_auto;
static volatile int film_request;
static volatile int film_cancel;
static volatile int film_processing;
extern int ml_started;
static struct film_retro_state retro;
static const struct event *retro_handoff_event;
static int retro_av_handoff;
static CONFIG_INT("t6film.stock", film_stock, 0);
static CONFIG_INT("t6film.effect", film_effect, 0);
static CONFIG_INT("t6film.amber", film_amber, 0);
static char film_names[FILM_T6_MAX_STOCKS][32];
static char film_files[FILM_T6_MAX_STOCKS][32];
static const char *film_choices[FILM_T6_MAX_STOCKS];
static int film_count;
static char film_status[48] = "Starting; hardware untested";
static struct menu_entry film_menu[];

/* Canon's formatter is not ISO printf: string precision is unsupported.
 * Bound text explicitly, then pass only plain %s to native formatting. */
static void film_copy_text(char *destination, size_t size, const char *source)
{
    size_t i = 0;
    if (!size) return;
    if (!source) source = "";
    while (i + 1 < size && source[i]) { destination[i] = source[i]; i++; }
    destination[i] = 0;
}

int film_t6_auto_enabled(void) { return film_auto; }
int film_t6_cancelled(void) { return film_cancel || ml_shutdown_requested; }
void film_t6_clear_cancel(void) { film_cancel = 0; }
int film_t6_take_request(void)
{
    if (!film_request) return 0;
    film_request = 0;
    return 1;
}

void film_t6_set_status(const char *status)
{
    film_copy_text(film_status, sizeof(film_status), status);
    if (retro.visible) menu_redraw();
}
void film_t6_set_processing(int processing) { film_processing = processing != 0; }

static int retro_context(void)
{
    return !ml_shutdown_requested && DISPLAY_IS_ON && !lv && !is_movie_mode() &&
        !lens_info.job_state && !get_halfshutter_pressed() && gui_menu_shown() &&
        is_menu_active("T6 Film") && is_menu_entry_selected("T6 Film", "Retro dashboard");
}

static enum film_retro_key retro_key(int code)
{
    switch (code) {
    case BGMT_PRESS_UP: return FILM_KEY_UP;
    case BGMT_PRESS_DOWN: return FILM_KEY_DOWN;
    case BGMT_PRESS_LEFT: return FILM_KEY_LEFT;
    case BGMT_PRESS_RIGHT: return FILM_KEY_RIGHT;
    case BGMT_UNPRESS_UP: return FILM_KEY_UP_RELEASE;
    case BGMT_UNPRESS_DOWN: return FILM_KEY_DOWN_RELEASE;
    case BGMT_UNPRESS_LEFT: return FILM_KEY_LEFT_RELEASE;
    case BGMT_UNPRESS_RIGHT: return FILM_KEY_RIGHT_RELEASE;
    case BGMT_PRESS_SET: return FILM_KEY_SET;
    case BGMT_UNPRESS_SET: return FILM_KEY_SET_RELEASE;
    case BGMT_Q: return FILM_KEY_Q;
    case BGMT_PRESS_DISP: return FILM_KEY_DISP;
    case BGMT_UNPRESS_DISP: return FILM_KEY_DISP_RELEASE;
    case BGMT_INFO: return FILM_KEY_INFO;
    case BGMT_MENU: return FILM_KEY_MENU;
    case BGMT_WHEEL_UP: case BGMT_WHEEL_LEFT: return FILM_KEY_DIAL_PREV;
    case BGMT_WHEEL_DOWN: case BGMT_WHEEL_RIGHT: return FILM_KEY_DIAL_NEXT;
    case BGMT_PLAY: case BGMT_LV: case BGMT_ISO:
    case BGMT_PRESS_ZOOM_IN: case BGMT_UNPRESS_ZOOM_IN:
    case BGMT_PRESS_ZOOM_OUT: case BGMT_UNPRESS_ZOOM_OUT:
    case BGMT_PRESS_HALFSHUTTER: case BGMT_UNPRESS_HALFSHUTTER:
    case BGMT_PRESS_FULLSHUTTER: case BGMT_UNPRESS_FULLSHUTTER:
        return FILM_KEY_NATIVE;
    default:
        /* OLC notifications and internal ML events are not button actions. */
        return code == GMT_OLC_INFO_CHANGED || code < 0 ? FILM_KEY_SYSTEM : FILM_KEY_UNKNOWN;
    }
}

int film_retro_event_gate(struct event *event)
{
    struct film_retro_settings p;
    unsigned actions;
    int route;
    retro_handoff_event = NULL;
    if (!event || event->type != 0) return FILM_RETRO_PASS;
    if (!retro.visible && !retro.held && !retro_av_handoff) return FILM_RETRO_PASS;
    /* Use the existing T6 AV decoder only for a recognized AV event with an
     * object. Never inspect arbitrary OLC/system event payloads. */
    if (BGMT_AV && (retro.visible || retro_av_handoff)) {
        extern int update_bgmt_av_status(struct event *);
        int released = event->obj && update_bgmt_av_status(event) == 0;
        unsigned closing = film_retro_close(&retro);
        retro_av_handoff = !released;
        retro_handoff_event = event;
        /* Press and release may arrive before the menu task processes the
         * first request. Only the visible->closed transition posts a toggle. */
        if ((closing & FILM_RETRO_CLOSE) && !get_halfshutter_pressed()) gui_stop_menu();
        return FILM_RETRO_NATIVE;
    }
    p.stock_count = film_count; p.stock = film_stock; p.effect = film_effect;
    p.amber = film_amber; p.auto_on = film_auto;
    route = film_retro_step(&retro, &p, retro_key(event->param),
                            retro.visible ? retro_context() : 0, film_processing, &actions);
    if (actions & FILM_RETRO_CHANGED) {
        film_stock = p.stock; film_effect = p.effect; film_amber = p.amber; film_auto = p.auto_on;
    }
    if (actions & FILM_RETRO_DEVELOP) { film_cancel = 0; film_request = 1; }
    if (actions & FILM_RETRO_CANCEL) { film_auto = 0; film_request = 0; film_cancel = 1; }
    /* Native handle_ml_menu_keys owns the half-press close signal. Canon can
     * update the half-press state and send other notifications before that
     * queued button event. Do not add a second close during this interval:
     * the native GUI semaphore is a TOGGLE and would reopen the menu. */
    if ((actions & FILM_RETRO_CLOSE) && route != FILM_RETRO_PASS &&
        event->param != BGMT_PRESS_HALFSHUTTER && !get_halfshutter_pressed())
        gui_stop_menu();
    if (actions & FILM_RETRO_REDRAW) menu_redraw();
    if (route == FILM_RETRO_CONSUME) keyrepeat_ack(event->param);
    if (route == FILM_RETRO_NATIVE) {
        /* Shutter still runs ML's normal half-shutter menu-close handler.
         * Other handoff keys bypass only ML menu actions, not core checks. */
        if (event->param < BGMT_PRESS_HALFSHUTTER || event->param > BGMT_UNPRESS_FULLSHUTTER)
            retro_handoff_event = event;
    }
    return route;
}

int film_retro_menu_passthrough(struct event *event)
{
    return event && event == retro_handoff_event;
}

static int retro_color(int color)
{
    static const int colors[] = {COLOR_BLACK, COLOR_ORANGE, COLOR_GRAY(35), COLOR_YELLOW};
    return color >= 0 && color < 4 ? colors[color] : COLOR_BLACK;
}
static void retro_fill(void *ctx, int x, int y, int w, int h, int color)
{
    (void)ctx;
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x > 720 - w || y > 480 - h) return;
    bmp_fill(retro_color(color), x, y, w, h);
}
static void retro_text(void *ctx, int x, int y, int size, int color, const char *text)
{
    int font = size == FILM_RETRO_LARGE ? FONT_LARGE :
               size == FILM_RETRO_MEDIUM ? FONT_MONO_20 : FONT_SMALL;
    (void)ctx;
    if (x < 0 || y < 0 || x >= 720 || y >= 480) return;
    bmp_printf(FONT(font, retro_color(color), NO_BG_ERASE), x, y, "%s", text);
}
static MENU_SELECT_FUNC(film_retro_open_menu)
{
    (void)priv; (void)delta;
    if (!retro_context()) { film_t6_set_status("Retro: leave LV/movie/capture first"); return; }
    film_retro_open(&retro); menu_redraw();
}
static MENU_UPDATE_FUNC(film_retro_draw_menu)
{
    struct film_retro_view v;
    const struct film_retro_canvas canvas = {NULL, retro_fill, retro_text};
    MENU_SET_VALUE("SET: open");
    if (!retro.visible || !entry->selected || !info->can_custom_draw || !retro_context()) return;
    memset(&v, 0, sizeof(v));
    v.page = retro.page; v.selected = retro.selected; v.help = retro.help;
    v.busy = film_processing; v.movie = is_movie_mode(); v.auto_on = film_auto;
    v.effect = film_effect; v.amber = film_amber; v.stock_count = film_count;
    v.stock = film_stock;
    v.stock_name = film_count && film_stock >= 0 && film_stock < film_count ?
                   film_names[film_stock] : "NO STOCKS";
    v.status = film_status;
    info->custom_drawing = CUSTOM_DRAW_THIS_MENU;
    film_retro_draw(&v, &canvas);
}

static void film_retro_poll(void *unused)
{
    (void)unused;
    while (!ml_started && !ml_shutdown_requested) msleep(250);
    while (!ml_shutdown_requested) {
        if (retro.visible) {
            if (!retro_context()) {
                /* Polling may see capture/mode changes before their queued
                 * GUI event. End only our custom screen: native ML owns all
                 * menu closure, avoiding a second semaphore toggle. */
                if (film_retro_close(&retro)) menu_redraw();
            }
            else menu_redraw(); /* native rendering owns the bitmap lock */
        }
        msleep(500); /* at most two status redraw requests each second */
    }
}
TASK_CREATE("t6retro", film_retro_poll, 0, 0x1f, 0x2000);

void film_t6_snapshot(struct film_t6_settings *settings)
{
    int selected = film_stock;
    if (selected < 0 || selected >= film_count) selected = 0;
    film_copy_text(settings->stock, sizeof(settings->stock),
                   film_count ? film_files[selected] : "");
    settings->effect = film_effect >= 0 && film_effect <= 3 ? film_effect : 0;
    settings->amber = film_amber != 0;
}

static int valid_stock_filename(const char *s)
{
    int n = strlen(s), i;
    if (n < 4 || n >= 32 || strcmp(s + n - 3, ".fs")) return 0;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
            return 0;
    }
    return !strstr(s, "..");
}

void film_t6_load_stocks(void)
{
    char *index = fio_malloc(4096);
    char *body = fio_malloc(4096);
    char path[64];
    int got, i, start = 0;
    film_count = 0;
    if (!index || !body) goto done;
    got = (int)read_file("ML/FILM/INDEX.TXT", index, 4095);
    if (got <= 0 || got >= 4095) goto done;
    index[got] = 0;
    for (i = 0; i <= got && film_count < FILM_T6_MAX_STOCKS; i++) {
        char *line;
        int n, k;
        if (index[i] && index[i] != '\r' && index[i] != '\n') continue;
        index[i] = 0;
        line = index + start;
        start = i + 1;
        if (!valid_stock_filename(line)) continue;
        snprintf(path, sizeof(path), "ML/FILM/%s", line);
        n = (int)read_file(path, body, 4095);
        if (n <= 0 || n >= 4095) continue;
        body[n] = 0;
        snprintf(film_files[film_count], 32, "%s", line);
        snprintf(film_names[film_count], 32, "%s", line);
        /* A bounded NAME line is sufficient for the picker. The full stock
         * is parsed by the engine for each snapshotted job. */
        for (k = 0; k + 5 < n; k++) {
            if ((k == 0 || body[k - 1] == '\n') && !strncmp(body + k, "NAME ", 5)) {
                int j = 0;
                k += 5;
                while (k < n && body[k] != '\r' && body[k] != '\n' && j < 31)
                    film_names[film_count][j++] = body[k++];
                film_names[film_count][j] = 0;
                break;
            }
        }
        film_choices[film_count] = film_names[film_count];
        film_count++;
    }
done:
    if (index) fio_free(index);
    if (body) fio_free(body);
    if (!film_count) film_choices[0] = "No ML/FILM stocks";
    if (film_stock < 0 || film_stock >= film_count) film_stock = 0;
    film_menu[2].max = film_count > 0 ? film_count - 1 : 0;
    film_t6_set_status(film_count ? "Ready; hardware untested" : "Missing ML/FILM stocks");
}

static MENU_SELECT_FUNC(film_develop_latest)
{
    (void)priv; (void)delta;
    film_cancel = 0;
    film_request = 1; /* Coalesces repeated button presses; no unbounded queue. */
}

static MENU_SELECT_FUNC(film_stop)
{
    (void)priv; (void)delta;
    film_auto = 0;
    film_request = 0;
    film_cancel = 1;
}

static MENU_UPDATE_FUNC(film_status_update)
{
    char value[35]; /* pinned MENU_MAX_VALUE_LEN, including terminator */
    (void)entry;
    film_copy_text(value, sizeof(value), film_status);
    MENU_SET_VALUE("%s", value);
}

static struct menu_entry film_menu[] = {
    {
        .name = "Auto new photos", .priv = &film_auto, .max = 1,
        .choices = CHOICES("OFF", "ON"),
        .help = "New CR2 previews to separate JPEGs. Always OFF at boot.",
    },
    {
        .name = "Develop latest", .select = film_develop_latest,
        .help = "Develop latest numbered CR2 in current DCIM folder once.",
    },
    {
        .name = "Film stock", .priv = &film_stock, .max = 0,
        .choices = film_choices,
        .help = "JPEG-preview grade; RAW and embedded preview stay untouched.",
    },
    {
        .name = "Cinema effects", .priv = &film_effect, .max = 3,
        .choices = CHOICES("OFF", "Subtle", "Cinema", "Strong"),
        .help = "Still JPEG streaks, bloom, vignette and edge color fringing.",
    },
    {
        .name = "Streak tint", .priv = &film_amber, .max = 1,
        .choices = CHOICES("Blue", "Amber"),
        .help = "Anamorphic-inspired image effect; does not change the lens.",
    },
    {
        .name = "Cancel / stop auto", .select = film_stop,
        .help = "Cancel at next row/read boundary; partial output is removed.",
    },
    {
        .name = "Status", .update = film_status_update,
        .help = "Experimental 1300D.110 build; physical camera testing needed.",
    },
    {
        .name = "Retro dashboard", .select = film_retro_open_menu,
        .update = film_retro_draw_menu,
        .help = "Amber film console. D-pad/SET/Q/DISP; MENU closes.",
        .help2 = "Shutter, AV/erase, playback, LV and zoom retain native controls.",
    },
};

static void film_menu_init(void *unused)
{
    (void)unused;
    film_choices[0] = "Loading ML/FILM";
    menu_add("T6 Film", film_menu, COUNT(film_menu));
}
INIT_FUNC("t6film.menu", film_menu_init);
