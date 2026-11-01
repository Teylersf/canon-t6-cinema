/** T6 Cinema: a built-in ML menu, separate from the original ML pages.
 * GPL-2.0. Experimental; firmware 1.1.0 only. No automatic activation.
 */
#include "dryos.h"
#include "mem.h"
#include "config.h"
#include "menu.h"
#include "fio-ml.h"
#include "film_t6.h"

#if !defined(CONFIG_1300D_110)
#error T6 Cinema must only be compiled for 1300D firmware 1.1.0
#endif

/* Intentionally not CONFIG_INT: processing always starts OFF after boot. */
static int film_auto;
static volatile int film_request;
static volatile int film_cancel;
static CONFIG_INT("t6film.stock", film_stock, 0);
static CONFIG_INT("t6film.effect", film_effect, 0);
static CONFIG_INT("t6film.amber", film_amber, 0);
static char film_names[FILM_T6_MAX_STOCKS][32];
static char film_files[FILM_T6_MAX_STOCKS][32];
static const char *film_choices[FILM_T6_MAX_STOCKS];
static int film_count;
static char film_status[48] = "Starting; hardware untested";
static struct menu_entry film_menu[];

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
    snprintf(film_status, sizeof(film_status), "%s", status);
}

void film_t6_snapshot(struct film_t6_settings *settings)
{
    int selected = film_stock;
    if (selected < 0 || selected >= film_count) selected = 0;
    snprintf(settings->stock, sizeof(settings->stock), "%s",
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
    film_cancel = 0;
    film_request = 1; /* Coalesces repeated button presses; no unbounded queue. */
}

static MENU_SELECT_FUNC(film_stop)
{
    film_auto = 0;
    film_request = 0;
    film_cancel = 1;
}

static MENU_UPDATE_FUNC(film_status_update)
{
    MENU_SET_VALUE("%s", film_status);
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
};

static void film_menu_init(void *unused)
{
    (void)unused;
    film_choices[0] = "Loading ML/FILM";
    menu_add("T6 Film", film_menu, COUNT(film_menu));
}
INIT_FUNC("t6film.menu", film_menu_init);
