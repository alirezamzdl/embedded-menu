/**
 * @file glcd_st7920.c
 * @brief Portable ST7920 128x64 serial-mode driver. See glcd_st7920.h.
 *
 * Serial protocol: every byte is sent as three bytes while CS is high:
 *   1. sync: 11111 RW RS 0   (0xF8 for a command, 0xFA for data)
 *   2. high nibble << 0, low 4 bits zero
 *   3. low nibble << 4,  low 4 bits zero
 *
 * Graphics RAM: the 128x64 screen is mapped as a 256x32 area. Rows 0..31
 * are rows 0..31 at horizontal word 0..7; rows 32..63 continue the same
 * 32 rows at horizontal word 8..15. A row is written by setting the
 * vertical then the horizontal address (two commands in extended mode)
 * and then sending 16 bytes; the address auto-increments.
 */
#include "glcd_st7920.h"
#include <string.h>

/* Basic instruction set */
#define CMD_BASIC          0x30u   /* 8-bit interface, basic instructions */
#define CMD_DISPLAY_ON     0x0Cu
#define CMD_CLEAR          0x01u
#define CMD_ENTRY_MODE     0x06u   /* address increments */
/* Extended instruction set */
#define CMD_EXTENDED       0x34u
#define CMD_GRAPHICS_ON    0x36u
#define CMD_GDRAM_ADDRESS  0x80u   /* | address (send vertical, then horizontal) */

#define BYTES_PER_ROW (GLCD_ST7920_WIDTH / 8)

/** Send one command (rs = false) or data byte (rs = true). */
static void send(glcd_st7920_t *lcd, bool rs, uint8_t value)
{
    const glcd_st7920_hal_t *hal = lcd->hal;
    uint8_t buf[3];

    buf[0] = (uint8_t)(rs ? 0xFAu : 0xF8u);
    buf[1] = (uint8_t)(value & 0xF0u);
    buf[2] = (uint8_t)((value << 4) & 0xF0u);

    hal->set_cs(true);
    hal->spi_write(buf, 3);
    hal->set_cs(false);
    hal->delay_us(ST7920_WRITE_DELAY_US);
}

void glcd_st7920_init(glcd_st7920_t *lcd, const glcd_st7920_hal_t *hal)
{
    lcd->hal = hal;
    hal->set_cs(false);

    if (hal->set_rst != NULL) {
        hal->set_rst(false);
        hal->delay_us(1000);
        hal->set_rst(true);
    }
    hal->delay_us(40000);          /* > 40 ms after power-on / reset */

    send(lcd, false, CMD_BASIC);
    hal->delay_us(100);
    send(lcd, false, CMD_BASIC);   /* the datasheet sends it twice */
    send(lcd, false, CMD_DISPLAY_ON);
    send(lcd, false, CMD_CLEAR);   /* clears the text RAM */
    hal->delay_us(1600);           /* clear takes 1.6 ms */
    send(lcd, false, CMD_ENTRY_MODE);
    send(lcd, false, CMD_EXTENDED);
    send(lcd, false, CMD_GRAPHICS_ON);

    glcd_st7920_clear(lcd);
    glcd_st7920_flush(lcd);
}

void glcd_st7920_clear(glcd_st7920_t *lcd)
{
    memset(lcd->fb, 0, sizeof(lcd->fb));
    memset(lcd->dirty, 0xFF, sizeof(lcd->dirty));   /* every row needs sending */
}

void glcd_st7920_set_pixel(void *ctx, int x, int y, bool on)
{
    glcd_st7920_t *lcd = (glcd_st7920_t *)ctx;
    uint8_t *cell;
    uint8_t mask, before;

    if (x < 0 || x >= GLCD_ST7920_WIDTH || y < 0 || y >= GLCD_ST7920_HEIGHT) return;

    cell = &lcd->fb[y][x / 8];
    mask = (uint8_t)(0x80u >> (x % 8));
    before = *cell;
    if (on) *cell = (uint8_t)(*cell | mask);
    else    *cell = (uint8_t)(*cell & ~mask);

    if (*cell != before) {
        lcd->dirty[y / 8] = (uint8_t)(lcd->dirty[y / 8] | (1u << (y % 8)));
    }
}

void glcd_st7920_flush(glcd_st7920_t *lcd)
{
    uint8_t y, i;

    for (y = 0; y < GLCD_ST7920_HEIGHT; y++) {
        uint8_t vertical, horizontal;
        if (!(lcd->dirty[y / 8] & (1u << (y % 8)))) continue;

        /* Lower half of the screen continues the upper half's rows. */
        vertical   = (uint8_t)(y < 32 ? y : y - 32);
        horizontal = (uint8_t)(y < 32 ? 0 : 8);
        send(lcd, false, (uint8_t)(CMD_GDRAM_ADDRESS | vertical));
        send(lcd, false, (uint8_t)(CMD_GDRAM_ADDRESS | horizontal));
        for (i = 0; i < BYTES_PER_ROW; i++) send(lcd, true, lcd->fb[y][i]);
    }
    memset(lcd->dirty, 0, sizeof(lcd->dirty));
}
