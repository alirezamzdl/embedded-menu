/**
 * @file glcd_ks0108.c
 * @brief Portable KS0108 128x64 driver. See glcd_ks0108.h.
 *
 * The KS0108 module has two controllers, each driving a 64x64 half. Each
 * controller's memory is 8 "pages" of 8 rows; one byte is a vertical strip
 * of 8 pixels (bit 0 at the top). The X ("Y address" in the datasheet)
 * auto-increments after each data write, so a whole 64-byte page row can be
 * sent after one address command.
 */
#include "glcd_ks0108.h"
#include <string.h>

/* KS0108 commands */
#define CMD_DISPLAY_ON   0x3Fu   /* display on */
#define CMD_START_LINE   0xC0u   /* | line 0..63: first row shown at the top */
#define CMD_SET_PAGE     0xB8u   /* | page 0..7 */
#define CMD_SET_ADDRESS  0x40u   /* | column 0..63 */

#define HALF_WIDTH (GLCD_KS0108_WIDTH / 2)
#define PAGES      (GLCD_KS0108_HEIGHT / 8)

/** Select one controller (0 = left, 1 = right), or none with chip > 1. */
static void select_chip(glcd_ks0108_t *lcd, uint8_t chip)
{
    lcd->hal->set_cs(0, chip == 0);
    lcd->hal->set_cs(1, chip == 1);
}

/** Write one byte: E high, then low latches the data on the falling edge. */
static void write_byte(glcd_ks0108_t *lcd, bool data, uint8_t value)
{
    const glcd_ks0108_hal_t *hal = lcd->hal;
    hal->set_rs(data);
    hal->write_bus(value);
    hal->delay_us(1);              /* address setup + data setup (>= 140 ns / 200 ns) */
    hal->set_e(true);
    hal->delay_us(1);              /* E high >= 450 ns */
    hal->set_e(false);
    hal->delay_us(KS0108_WRITE_DELAY_US);  /* instruction time (no busy check) */
}

void glcd_ks0108_init(glcd_ks0108_t *lcd, const glcd_ks0108_hal_t *hal)
{
    uint8_t chip;

    lcd->hal = hal;
    hal->set_e(false);
    select_chip(lcd, 2);

    if (hal->set_rst != NULL) {
        hal->set_rst(false);
        hal->delay_us(1000);
        hal->set_rst(true);
    }
    hal->delay_us(10000);          /* wait for the controllers to come out of reset */

    for (chip = 0; chip < 2; chip++) {
        select_chip(lcd, chip);
        write_byte(lcd, false, CMD_DISPLAY_ON);
        write_byte(lcd, false, CMD_START_LINE | 0u);
        write_byte(lcd, false, CMD_SET_PAGE | 0u);
        write_byte(lcd, false, CMD_SET_ADDRESS | 0u);
    }
    select_chip(lcd, 2);

    glcd_ks0108_clear(lcd);
    glcd_ks0108_flush(lcd);
}

void glcd_ks0108_clear(glcd_ks0108_t *lcd)
{
    memset(lcd->fb, 0, sizeof(lcd->fb));
    memset(lcd->dirty, 0x03, sizeof(lcd->dirty));   /* everything needs sending */
}

void glcd_ks0108_set_pixel(void *ctx, int x, int y, bool on)
{
    glcd_ks0108_t *lcd = (glcd_ks0108_t *)ctx;
    uint8_t *cell;
    uint8_t mask, before;

    if (x < 0 || x >= GLCD_KS0108_WIDTH || y < 0 || y >= GLCD_KS0108_HEIGHT) return;

    cell = &lcd->fb[y / 8][x];
    mask = (uint8_t)(1u << (y % 8));
    before = *cell;
    if (on) *cell = (uint8_t)(*cell | mask);
    else    *cell = (uint8_t)(*cell & ~mask);

    if (*cell != before) {
        lcd->dirty[y / 8] = (uint8_t)(lcd->dirty[y / 8] | (x < HALF_WIDTH ? 0x01u : 0x02u));
    }
}

void glcd_ks0108_flush(glcd_ks0108_t *lcd)
{
    uint8_t page, chip, i;

    for (page = 0; page < PAGES; page++) {
        for (chip = 0; chip < 2; chip++) {
            const uint8_t *src;
            if (!(lcd->dirty[page] & (1u << chip))) continue;

            select_chip(lcd, chip);
            write_byte(lcd, false, (uint8_t)(CMD_SET_PAGE | page));
            write_byte(lcd, false, CMD_SET_ADDRESS | 0u);
            src = &lcd->fb[page][chip * HALF_WIDTH];
            for (i = 0; i < HALF_WIDTH; i++) write_byte(lcd, true, src[i]);
        }
        lcd->dirty[page] = 0;
    }
    select_chip(lcd, 2);
}
