/**
 * @file demo_fa.c
 * @brief Persian menu on a simulated 128x64 graphic LCD (KS0108 style).
 *
 * The menu is drawn into a RAM framebuffer through menu_fa_draw_row() and
 * then printed to the terminal as ASCII art. On a real board, replace
 * set_pixel() with your display driver (for example the SetPixel() of a
 * KS0108 library) and flush the screen in present().
 *
 * Run `demo_fa` for the 8-pixel font or `demo_fa 16` for the 16-pixel font.
 *
 * Keys (type them, then press Enter):
 *   w = up   s = down   e = enter   b = back   h = home   q = quit
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "menu.h"
#include "menu_fa.h"

#define LCD_W     128
#define LCD_H     64
#define MAX_ROWS  8

/* Layout, set from the chosen font in main(). */
static const menu_fa_font_t *font = &menu_fa_font8;
static int row_h;      /* height of one item row */
static int items_y;    /* first item row, under the title bar */
static int rows;       /* item rows that fit on the screen */

/* ------------------------------------------------------------------------ */
/* Simulated display                                                        */
/* ------------------------------------------------------------------------ */

static bool fb[LCD_H][LCD_W];

/* The only display-specific function: menu_fa calls it for every pixel. */
static void set_pixel(void *ctx, int x, int y, bool on)
{
    (void)ctx;
    if (x >= 0 && x < LCD_W && y >= 0 && y < LCD_H) fb[y][x] = on;
}

static void clear(void)
{
    memset(fb, 0, sizeof(fb));
}

static void hline(int y)
{
    int x;
    for (x = 0; x < LCD_W; x++) set_pixel(NULL, x, y, true);
}

/* Print the framebuffer, with a frame so the screen edges are visible. */
static void present(void)
{
    int x, y;
    printf("+");
    for (x = 0; x < LCD_W; x++) putchar('-');
    printf("+\n");
    for (y = 0; y < LCD_H; y++) {
        putchar('|');
        for (x = 0; x < LCD_W; x++) putchar(fb[y][x] ? '#' : ' ');
        printf("|\n");
    }
    printf("+");
    for (x = 0; x < LCD_W; x++) putchar('-');
    printf("+\n");
}

/* ------------------------------------------------------------------------ */
/* Settings                                                                 */
/* ------------------------------------------------------------------------ */

static bool    alarm_on   = true;
static int32_t brightness = 70;
static uint8_t lang       = 0;

static const char *const lang_options[] = { "فارسی", "English" };

static const menu_value_t alarm_value      = MENU_VALUE_BOOL_INIT(&alarm_on);
static const menu_value_t brightness_value = MENU_VALUE_INT_INIT(&brightness, 0, 100, 10);
static const menu_value_t lang_value       = MENU_VALUE_ENUM_INIT(&lang, lang_options);

/* ------------------------------------------------------------------------ */
/* Drawing                                                                  */
/* ------------------------------------------------------------------------ */

/* Text to show for a value page. Numbers are written with ASCII digits;
 * menu_fa turns them into Persian digits. */
static const char *value_text(const menu_t *menu, const menu_page_t *p,
                              char *buf, size_t len)
{
    const char *label = menu_value_label(menu, p);
    if (label != NULL) return label;
    if (p->value->type == MENU_VALUE_BOOL) {
        return menu_value_get(menu, p) ? "روشن" : "خاموش";
    }
    snprintf(buf, len, "%ld", (long)menu_value_get(menu, p));
    return buf;
}

static void draw(menu_t *menu)
{
    menu_item_t items[MAX_ROWS];
    uint8_t n, i;
    char buf[12];

    clear();

    /* Title: current page name, centred-ish on the right, on a filled bar. */
    menu_fa_draw_row(font, menu_current(menu)->text, 0, LCD_W - 1, 0, true,
                     set_pixel, NULL);
    hline(font->height + 1);

    if (menu_mode(menu) == MENU_MODE_CONFIRM) {
        const char *answer = menu_confirm_yes(menu) ? "بله" : "خیر";
        menu_fa_draw_row(font, menu_active(menu)->text, 0, LCD_W - 1, items_y,
                         false, set_pixel, NULL);
        menu_fa_draw_row(font, "مطمئن هستید؟", 0, LCD_W - 1, items_y + row_h,
                         false, set_pixel, NULL);
        menu_fa_draw_row(font, answer, 40, 80, items_y + 2 * row_h, true,
                         set_pixel, NULL);
        present();
        return;
    }

    n = menu_view(menu, items, (uint8_t)rows);
    for (i = 0; i < n; i++) {
        const menu_page_t *p = items[i].page;
        int y = items_y + i * row_h;

        /* Title right-aligned, highlighted row inverted across the full width. */
        menu_fa_draw_row(font, p->text, 0, LCD_W - 2, y, items[i].selected,
                         set_pixel, NULL);

        /* Value on the left side of the row. */
        if (p->value != NULL) {
            const char *v = value_text(menu, p, buf, sizeof(buf));
            int w = menu_fa_width(font, v);
            if (items[i].editing) {
                /* Editing: show the value on its own inverted box. */
                menu_fa_draw_row(font, v, 1, w + 4, y, !items[i].selected,
                                 set_pixel, NULL);
            } else {
                menu_fa_draw(font, v, w + 2, y, items[i].selected, set_pixel, NULL);
            }
        }
    }
    present();
}

/* ------------------------------------------------------------------------ */
/* Handlers                                                                 */
/* ------------------------------------------------------------------------ */

static void on_page(menu_t *menu, const menu_page_t *self, menu_event_t event)
{
    (void)self; (void)event;
    draw(menu);
}

static void on_reset(menu_t *menu, const menu_page_t *self, menu_event_t event)
{
    (void)menu; (void)self;
    if (event == MENU_EVT_ACTION) {
        alarm_on = true; brightness = 70; lang = 0;
    }
}

/* ------------------------------------------------------------------------ */
/* The tree (const, in Flash). Titles are plain UTF-8 strings.              */
/* ------------------------------------------------------------------------ */

MENU_PAGE_DECLARE(pg_main);
MENU_PAGE_DECLARE(pg_settings);
MENU_PAGE_DECLARE(pg_alarm);
MENU_PAGE_DECLARE(pg_brightness);
MENU_PAGE_DECLARE(pg_lang);
MENU_PAGE_DECLARE(pg_reset);
MENU_PAGE_DECLARE(pg_wifi);
MENU_PAGE_DECLARE(pg_clock);
MENU_PAGE_DECLARE(pg_about);

MENU_PAGE_DEFINE(pg_main, .text = "منوی اصلی", .handler = on_page,
                 .first_child = &pg_settings);

/* Mixed Persian / Latin titles, numbers, brackets and the لا ligature. */

MENU_PAGE_DEFINE(pg_settings, .text = "تنظیمات", .handler = on_page,
                 .parent = &pg_main, .first_child = &pg_alarm,
                 .next_sibling = &pg_wifi);
MENU_PAGE_DEFINE(pg_alarm, .text = "زنگ هشدار", .value = &alarm_value,
                 .parent = &pg_settings, .next_sibling = &pg_brightness);
MENU_PAGE_DEFINE(pg_brightness, .text = "نور صفحه", .value = &brightness_value,
                 .parent = &pg_settings, .next_sibling = &pg_lang);
MENU_PAGE_DEFINE(pg_lang, .text = "زبان", .value = &lang_value,
                 .parent = &pg_settings, .next_sibling = &pg_reset);
MENU_PAGE_DEFINE(pg_reset, .text = "بازگشت به تنظیمات کارخانه",
                 .handler = on_reset, .flags = MENU_FLAG_CONFIRM,
                 .parent = &pg_settings);

MENU_PAGE_DEFINE(pg_wifi, .text = "WiFi (2.4GHz)", .parent = &pg_main,
                 .next_sibling = &pg_clock);
MENU_PAGE_DEFINE(pg_clock, .text = "ساعت 12:30 «لاله»", .parent = &pg_main,
                 .next_sibling = &pg_about);
MENU_PAGE_DEFINE(pg_about, .text = "درباره‌ی دستگاه v1.2", .parent = &pg_main);

/* ------------------------------------------------------------------------ */

static menu_t menu;

int main(int argc, char **argv)
{
    int ch;

    if (argc > 1 && strcmp(argv[1], "16") == 0) font = &menu_fa_font16;
    row_h   = font->height + 1;
    items_y = font->height + 3;
    rows    = (LCD_H - items_y) / row_h;
    if (rows > MAX_ROWS) rows = MAX_ROWS;

    menu_init(&menu, &pg_main);

    while ((ch = getchar()) != EOF && ch != 'q') {
        switch (ch) {
        case 'w': menu_input(&menu, MENU_KEY_UP);    break;
        case 's': menu_input(&menu, MENU_KEY_DOWN);  break;
        case 'e': menu_input(&menu, MENU_KEY_ENTER); break;
        case 'b': menu_input(&menu, MENU_KEY_BACK);  break;
        case 'h': menu_input(&menu, MENU_KEY_HOME);  break;
        default:                                     break;
        }
    }
    return 0;
}
