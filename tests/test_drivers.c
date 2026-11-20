/**
 * @file test_drivers.c
 * @brief Host-side tests for the KS0108 and ST7920 example drivers.
 *
 * A mock HAL records what each driver sends, so the tests check the exact
 * command/data sequence without any hardware. Run with `make test`.
 */
#include <stdio.h>
#include <string.h>
#include "glcd_ks0108.h"
#include "glcd_st7920.h"
#include "menu_fa.h"

static int failures = 0;

#define CHECK(cond)                                                  \
    do {                                                             \
        if (!(cond)) {                                               \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            failures++;                                              \
        }                                                            \
    } while (0)

/* ---- Recorded writes: (chip, is_data, value) ---- */
#define LOG_MAX 20000
typedef struct { int chip; bool data; uint8_t value; } write_t;
static write_t log_buf[LOG_MAX];
static int log_len;

static void log_reset(void) { log_len = 0; }
static void log_add(int chip, bool data, uint8_t value)
{
    if (log_len < LOG_MAX) {
        log_buf[log_len].chip = chip;
        log_buf[log_len].data = data;
        log_buf[log_len].value = value;
        log_len++;
    }
}

static void no_delay(uint16_t us) { (void)us; }

/* ======================================================================== */
/* KS0108 mock: latch on the falling edge of E                              */
/* ======================================================================== */

static struct { uint8_t bus; bool rs, e, cs[2]; } ks;

static void ks_bus(uint8_t v)          { ks.bus = v; }
static void ks_rs(bool h)              { ks.rs = h; }
static void ks_cs(uint8_t chip, bool a) { ks.cs[chip & 1u] = a; }
static void ks_e(bool h)
{
    if (ks.e && !h) {                    /* falling edge: the controller latches */
        CHECK(ks.cs[0] != ks.cs[1]);     /* exactly one chip selected */
        log_add(ks.cs[0] ? 0 : 1, ks.rs, ks.bus);
    }
    ks.e = h;
}

static const glcd_ks0108_hal_t ks_hal = { ks_bus, ks_rs, ks_e, ks_cs, NULL, no_delay };

static void test_ks0108(void)
{
    static glcd_ks0108_t lcd;
    int i;

    log_reset();
    glcd_ks0108_init(&lcd, &ks_hal);

    /* Each chip: display on, start line 0, page 0, address 0. */
    CHECK(log_len >= 8);
    CHECK(log_buf[0].chip == 0 && !log_buf[0].data && log_buf[0].value == 0x3F);
    CHECK(log_buf[1].value == 0xC0 && log_buf[2].value == 0xB8 && log_buf[3].value == 0x40);
    CHECK(log_buf[4].chip == 1 && log_buf[4].value == 0x3F);

    /* Then the whole screen is cleared: 8 pages x 2 chips x (2 cmds + 64 zeros). */
    CHECK(log_len == 8 + 8 * 2 * (2 + 64));
    for (i = 8; i < log_len; i++) if (log_buf[i].data) CHECK(log_buf[i].value == 0);
    CHECK(!ks.cs[0] && !ks.cs[1]);      /* nothing selected afterwards */

    /* Nothing changed: flush sends nothing. */
    log_reset();
    glcd_ks0108_flush(&lcd);
    CHECK(log_len == 0);

    /* One pixel in the right half, page 1: only that block is sent. */
    glcd_ks0108_set_pixel(&lcd, 70, 10, true);
    log_reset();
    glcd_ks0108_flush(&lcd);
    CHECK(log_len == 2 + 64);
    CHECK(log_buf[0].chip == 1 && !log_buf[0].data && log_buf[0].value == (0xB8 | 1));
    CHECK(!log_buf[1].data && log_buf[1].value == 0x40);
    for (i = 0; i < 64; i++) {
        CHECK(log_buf[2 + i].data && log_buf[2 + i].chip == 1);
        CHECK(log_buf[2 + i].value == (i == 70 - 64 ? 0x04 : 0x00));   /* y 10 -> bit 2 */
    }

    /* Setting a pixel to its current value doesn't mark anything dirty. */
    glcd_ks0108_set_pixel(&lcd, 70, 10, true);
    glcd_ks0108_set_pixel(&lcd, -1, 0, true);     /* off-screen: ignored */
    glcd_ks0108_set_pixel(&lcd, 0, 64, true);
    log_reset();
    glcd_ks0108_flush(&lcd);
    CHECK(log_len == 0);

    /* Drawing Persian text through menu_fa marks the right blocks. */
    menu_fa_draw_row(&menu_fa_font8, "سلام", 0, 127, 0, true, glcd_ks0108_set_pixel, &lcd);
    log_reset();
    glcd_ks0108_flush(&lcd);
    CHECK(log_len == 2 * (2 + 64));       /* page 0, both halves */
}

/* ======================================================================== */
/* ST7920 mock: decode the 3-byte serial frames sent while CS is high       */
/* ======================================================================== */

static bool st_cs;

static void st_spi(const uint8_t *d, uint16_t n)
{
    CHECK(st_cs);
    CHECK(n == 3);
    if (n == 3) {
        CHECK(d[0] == 0xF8 || d[0] == 0xFA);
        CHECK((d[1] & 0x0F) == 0 && (d[2] & 0x0F) == 0);
        log_add(0, d[0] == 0xFA, (uint8_t)(d[1] | (d[2] >> 4)));
    }
}
static void st_set_cs(bool h) { st_cs = h; }

static const glcd_st7920_hal_t st_hal = { st_spi, st_set_cs, NULL, no_delay };

/* Index of the first write of row y in a full flush that starts at `start`. */
static void check_row(int at, int y, const uint8_t *expect16)
{
    int i;
    CHECK(!log_buf[at].data && log_buf[at].value == (0x80 | (y < 32 ? y : y - 32)));
    CHECK(!log_buf[at + 1].data && log_buf[at + 1].value == (0x80 | (y < 32 ? 0 : 8)));
    for (i = 0; i < 16; i++) {
        CHECK(log_buf[at + 2 + i].data);
        CHECK(log_buf[at + 2 + i].value == expect16[i]);
    }
}

static void test_st7920(void)
{
    static glcd_st7920_t lcd;
    static const uint8_t init_cmds[] = { 0x30, 0x30, 0x0C, 0x01, 0x06, 0x34, 0x36 };
    uint8_t row[16];
    int i;

    log_reset();
    glcd_st7920_init(&lcd, &st_hal);

    for (i = 0; i < 7; i++) CHECK(!log_buf[i].data && log_buf[i].value == init_cmds[i]);
    /* Then 64 rows x (2 address commands + 16 zero bytes). */
    CHECK(log_len == 7 + 64 * 18);
    memset(row, 0, sizeof(row));
    check_row(7, 0, row);
    check_row(7 + 40 * 18, 40, row);
    CHECK(!st_cs);

    log_reset();
    glcd_st7920_flush(&lcd);
    CHECK(log_len == 0);

    /* Top-left pixel of the lower half: row 40 -> vertical 8, horizontal 8. */
    glcd_st7920_set_pixel(&lcd, 0, 40, true);
    log_reset();
    glcd_st7920_flush(&lcd);
    CHECK(log_len == 18);
    memset(row, 0, sizeof(row));
    row[0] = 0x80;
    check_row(0, 40, row);

    /* Rightmost pixel of row 5. */
    glcd_st7920_set_pixel(&lcd, 127, 5, true);
    log_reset();
    glcd_st7920_flush(&lcd);
    memset(row, 0, sizeof(row));
    row[15] = 0x01;
    CHECK(log_len == 18);
    check_row(0, 5, row);

    /* Clearing it again marks the row; off-screen pixels are ignored. */
    glcd_st7920_set_pixel(&lcd, 127, 5, false);
    glcd_st7920_set_pixel(&lcd, 128, 5, true);
    log_reset();
    glcd_st7920_flush(&lcd);
    CHECK(log_len == 18);

    /* A 16 px Persian row marks exactly 16 rows. */
    menu_fa_draw_row(&menu_fa_font16, "تنظیمات", 0, 127, 20, true, glcd_st7920_set_pixel, &lcd);
    log_reset();
    glcd_st7920_flush(&lcd);
    CHECK(log_len == 16 * 18);
}

int main(void)
{
    test_ks0108();
    test_st7920();

    if (failures == 0) {
        printf("All driver tests passed\n");
        return 0;
    }
    printf("%d check(s) failed\n", failures);
    return 1;
}
