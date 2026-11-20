/**
 * @file demo.c
 * @brief Terminal demo for embedded-menu.
 *
 * Runs on a PC and draws the menu with printf, as if on a 4-line display.
 * On a real board, replace draw() with your LCD/OLED code and the key
 * reading in main() with your buttons / encoder.
 *
 * Keys (type them, then press Enter):
 *   w = up   s = down   e = enter   b = back   h = home
 *   g = jump straight to Settings > Brightness
 *   t = pretend 10 seconds passed (the menu times out after 30 s)
 *   q = quit
 */
#include <stdio.h>
#include <stdbool.h>
#include "menu.h"

#define ROWS        4        /* lines available for items on the "display" */
#define TIMEOUT_MS  30000u

/* ------------------------------------------------------------------------ */
/* Application settings the menu edits                                      */
/* ------------------------------------------------------------------------ */

static bool    led_on     = false;
static int32_t brightness = 50;
static uint8_t baud_index = 0;
static bool    debug_mode = false;
static bool    wifi_found = false;   /* pretend there is no WiFi module */

static uint16_t beep_short_ms = 100; /* used through the page's .user pointer */
static uint16_t beep_long_ms  = 500;

static const char *const baud_options[] = { "9600", "19200", "57600", "115200" };

static const menu_value_t led_value        = MENU_VALUE_BOOL_INIT(&led_on);
static const menu_value_t brightness_value = MENU_VALUE_INT_INIT(&brightness, 0, 100, 10);
static const menu_value_t baud_value       = MENU_VALUE_ENUM_INIT(&baud_index, baud_options);
static const menu_value_t debug_value      = MENU_VALUE_BOOL_INIT(&debug_mode);

/* ------------------------------------------------------------------------ */
/* Drawing: the only display-specific part                                  */
/* ------------------------------------------------------------------------ */

static void print_value(const menu_t *menu, const menu_page_t *page)
{
    const char *label = menu_value_label(menu, page);

    if (label != NULL) {
        printf("%s", label);
    } else if (page->value->type == MENU_VALUE_BOOL) {
        printf("%s", menu_value_get(menu, page) ? "ON" : "OFF");
    } else {
        printf("%ld", (long)menu_value_get(menu, page));
    }
}

static void draw(menu_t *menu)
{
    menu_item_t items[ROWS];
    uint8_t n, i, level;
    const menu_page_t *active = menu_active(menu);

    /* Breadcrumb title: Main > Settings */
    printf("\n====");
    for (level = 0; level <= menu_depth(menu); level++) {
        printf("%s %s", level ? " >" : "", menu_path(menu, level)->text);
    }
    printf(" ====\n");

    if (menu_mode(menu) == MENU_MODE_CONFIRM) {
        printf("   %s?\n", active->text);
        printf("   %s No    %s Yes\n",
               menu_confirm_yes(menu) ? " " : ">",
               menu_confirm_yes(menu) ? ">" : " ");
        return;
    }

    n = menu_view(menu, items, ROWS);
    for (i = 0; i < n; i++) {
        const menu_page_t *p = items[i].page;

        printf(" %c %s", items[i].selected ? '>' : ' ', p->text);
        if (p->first_child != NULL) printf(" ...");
        if (p->value != NULL) {
            printf(items[i].editing ? ": [ " : ": ");
            print_value(menu, p);
            if (items[i].editing) printf(" ]");
        }
        if (items[i].disabled) printf("  (n/a)");
        printf("\n");
    }
}

/* ------------------------------------------------------------------------ */
/* Handlers                                                                 */
/* ------------------------------------------------------------------------ */

/* Pages with children: every event they receive means "redraw". */
static void on_page(menu_t *menu, const menu_page_t *self, menu_event_t event)
{
    (void)self;
    if (event == MENU_EVT_TIMEOUT) printf("\n(no key for 30 s: back to Main)");
    draw(menu);
}

/* Value pages: the library already wrote the variable; we only react. */
static void on_value(menu_t *menu, const menu_page_t *self, menu_event_t event)
{
    (void)menu;
    if (event == MENU_EVT_VALUE_CHANGED) {
        printf("\n(saved %s to EEPROM)", self->text);
    }
}

/* One handler for both beeps: the duration comes from .user. */
static void on_beep(menu_t *menu, const menu_page_t *self, menu_event_t event)
{
    (void)menu;
    if (event == MENU_EVT_ACTION) {
        printf("\n(beep for %u ms)", (unsigned)*(const uint16_t *)self->user);
    }
}

static void on_factory_reset(menu_t *menu, const menu_page_t *self, menu_event_t event)
{
    (void)menu; (void)self;
    if (event == MENU_EVT_ACTION) {
        led_on = false; brightness = 50; baud_index = 0; debug_mode = false;
        printf("\n(settings reset)");
    }
}

static void on_debug_log(menu_t *menu, const menu_page_t *self, menu_event_t event)
{
    (void)menu; (void)self;
    if (event == MENU_EVT_ACTION) printf("\n(debug log dumped)");
}

static void on_about(menu_t *menu, const menu_page_t *self, menu_event_t event)
{
    (void)menu; (void)self;
    if (event == MENU_EVT_ACTION) printf("\n(embedded-menu demo)");
}

/* Visibility rules, checked every time the menu is drawn or moved. */
static menu_item_state_t only_in_debug(const menu_page_t *self)
{
    (void)self;
    return debug_mode ? MENU_ITEM_NORMAL : MENU_ITEM_HIDDEN;
}

static menu_item_state_t needs_wifi(const menu_page_t *self)
{
    (void)self;
    return wifi_found ? MENU_ITEM_NORMAL : MENU_ITEM_DISABLED;
}

/* ------------------------------------------------------------------------ */
/* The tree, entirely const: on a microcontroller it stays in Flash.        */
/*                                                                          */
/* Main                                                                     */
/* |-- Settings                                                             */
/* |   |-- LED             (bool)                                           */
/* |   |-- Brightness      (int 0..100, step 10)                            */
/* |   |-- Baudrate        (enum)                                           */
/* |   |-- Debug mode      (bool)                                           */
/* |   |-- Dump debug log  (hidden unless Debug mode is ON)                 */
/* |   |-- WiFi            (disabled: no module)                            */
/* |   `-- Factory reset   (asks Yes/No)                                    */
/* |-- Tools                                                                */
/* |   |-- Beep short      (shared handler, .user = duration)               */
/* |   `-- Beep long                                                        */
/* `-- About                                                                */
/* ------------------------------------------------------------------------ */

MENU_PAGE_DECLARE(pg_main);
MENU_PAGE_DECLARE(pg_settings);
MENU_PAGE_DECLARE(pg_led);
MENU_PAGE_DECLARE(pg_brightness);
MENU_PAGE_DECLARE(pg_baud);
MENU_PAGE_DECLARE(pg_debug);
MENU_PAGE_DECLARE(pg_debug_log);
MENU_PAGE_DECLARE(pg_wifi);
MENU_PAGE_DECLARE(pg_reset);
MENU_PAGE_DECLARE(pg_tools);
MENU_PAGE_DECLARE(pg_beep_short);
MENU_PAGE_DECLARE(pg_beep_long);
MENU_PAGE_DECLARE(pg_about);

MENU_PAGE_DEFINE(pg_main, .text = "Main", .handler = on_page,
                 .first_child = &pg_settings);

MENU_PAGE_DEFINE(pg_settings, .text = "Settings", .handler = on_page,
                 .parent = &pg_main, .first_child = &pg_led,
                 .next_sibling = &pg_tools);
MENU_PAGE_DEFINE(pg_led, .text = "LED", .handler = on_value,
                 .value = &led_value, .parent = &pg_settings,
                 .next_sibling = &pg_brightness);
MENU_PAGE_DEFINE(pg_brightness, .text = "Brightness", .handler = on_value,
                 .value = &brightness_value, .parent = &pg_settings,
                 .next_sibling = &pg_baud);
MENU_PAGE_DEFINE(pg_baud, .text = "Baudrate", .handler = on_value,
                 .value = &baud_value, .parent = &pg_settings,
                 .next_sibling = &pg_debug);
MENU_PAGE_DEFINE(pg_debug, .text = "Debug mode", .handler = on_value,
                 .value = &debug_value, .parent = &pg_settings,
                 .next_sibling = &pg_debug_log);
MENU_PAGE_DEFINE(pg_debug_log, .text = "Dump debug log", .handler = on_debug_log,
                 .state = only_in_debug, .parent = &pg_settings,
                 .next_sibling = &pg_wifi);
MENU_PAGE_DEFINE(pg_wifi, .text = "WiFi", .state = needs_wifi,
                 .parent = &pg_settings, .next_sibling = &pg_reset);
MENU_PAGE_DEFINE(pg_reset, .text = "Factory reset", .handler = on_factory_reset,
                 .flags = MENU_FLAG_CONFIRM, .parent = &pg_settings);

MENU_PAGE_DEFINE(pg_tools, .text = "Tools", .handler = on_page,
                 .parent = &pg_main, .first_child = &pg_beep_short,
                 .next_sibling = &pg_about);
MENU_PAGE_DEFINE(pg_beep_short, .text = "Beep short", .handler = on_beep,
                 .user = &beep_short_ms, .parent = &pg_tools,
                 .next_sibling = &pg_beep_long);
MENU_PAGE_DEFINE(pg_beep_long, .text = "Beep long", .handler = on_beep,
                 .user = &beep_long_ms, .parent = &pg_tools);

MENU_PAGE_DEFINE(pg_about, .text = "About", .handler = on_about,
                 .parent = &pg_main);

/* ------------------------------------------------------------------------ */
/* Main loop                                                                */
/* ------------------------------------------------------------------------ */

static menu_t menu;

int main(void)
{
    int ch;

    menu_init(&menu, &pg_main);        /* sends ENTER to Main: first draw */
    menu_set_timeout(&menu, TIMEOUT_MS);

    /* Input code only moves the menu; the handlers do all the drawing. */
    while ((ch = getchar()) != EOF && ch != 'q') {
        switch (ch) {
        case 'w': menu_input(&menu, MENU_KEY_UP);    break;
        case 's': menu_input(&menu, MENU_KEY_DOWN);  break;
        case 'e': menu_input(&menu, MENU_KEY_ENTER); break;
        case 'b': menu_input(&menu, MENU_KEY_BACK);  break;
        case 'h': menu_input(&menu, MENU_KEY_HOME);  break;
        case 'g': menu_goto(&menu, &pg_brightness);  break;
        case 't': menu_tick(&menu, 10000u);          break;
        default:                                     break;
        }
    }
    return 0;
}
