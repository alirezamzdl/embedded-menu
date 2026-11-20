/**
 * @file glcd_st7920.h
 * @brief Portable driver for 128x64 ST7920 graphic LCDs in serial (SPI) mode.
 *
 * - Keeps a 1 KB framebuffer in RAM and only sends the rows that changed
 *   (glcd_st7920_flush()).
 * - Serial mode: PSB pin to GND, then RS = CS (active high), R/W = SID
 *   (data), E = SCLK. Only three wires plus optional RST.
 * - Write-only: the busy flag can't be read in serial mode, so the driver
 *   waits ST7920_WRITE_DELAY_US after every byte.
 * - Hardware access goes through a small HAL you provide.
 * - glcd_st7920_set_pixel() has the menu_fa_pixel_fn signature, so it can be
 *   passed straight to menu_fa_draw() / menu_fa_draw_row().
 *
 * SPI settings: 8-bit, MSB first, data sampled on the rising edge of SCLK
 * (SPI mode 0 or 3), at most about 1 MHz (some modules need 500 kHz).
 *
 * Example HAL for STM32 HAL:
 *
 *   static void spi(const uint8_t *d, uint16_t n) { HAL_SPI_Transmit(&hspi1, (uint8_t *)d, n, 10); }
 *   static void cs(bool h)             { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, h); }
 *   static void rst(bool h)            { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, h); }
 *   static void delay_us(uint16_t us)  { my_delay_us(us); }   // e.g. DWT or a timer
 *
 *   static const glcd_st7920_hal_t hal = { spi, cs, rst, delay_us };
 *   static glcd_st7920_t lcd;
 *
 *   glcd_st7920_init(&lcd, &hal);
 *   menu_fa_draw_row(&menu_fa_font16, "سلام", 0, 127, 0, false, glcd_st7920_set_pixel, &lcd);
 *   glcd_st7920_flush(&lcd);
 */
#ifndef GLCD_ST7920_H
#define GLCD_ST7920_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#define GLCD_ST7920_WIDTH   128
#define GLCD_ST7920_HEIGHT  64

/** Wait after each byte (µs). The datasheet gives 72 µs per instruction. */
#ifndef ST7920_WRITE_DELAY_US
#define ST7920_WRITE_DELAY_US 72
#endif

/** Hardware access. All functions are required except set_rst. */
typedef struct {
    void (*spi_write)(const uint8_t *data, uint16_t len); /**< Send bytes on SID/SCLK */
    void (*set_cs)(bool high);                  /**< RS/CS pin (active high in serial mode) */
    void (*set_rst)(bool high);                 /**< RST pin, or NULL if tied high */
    void (*delay_us)(uint16_t us);              /**< Busy-wait at least `us` microseconds */
} glcd_st7920_hal_t;

/** Driver state: framebuffer plus which rows need sending. */
typedef struct {
    const glcd_st7920_hal_t *hal;
    uint8_t fb[GLCD_ST7920_HEIGHT][GLCD_ST7920_WIDTH / 8]; /**< [y][x / 8], bit 7 = leftmost */
    uint8_t dirty[GLCD_ST7920_HEIGHT / 8];                 /**< one bit per row */
} glcd_st7920_t;

/** Reset, switch to graphics mode, then clear the display. */
void glcd_st7920_init(glcd_st7920_t *lcd, const glcd_st7920_hal_t *hal);

/** Clear the framebuffer (call flush to show it). */
void glcd_st7920_clear(glcd_st7920_t *lcd);

/**
 * Set one pixel in the framebuffer. `ctx` is the glcd_st7920_t. Pixels
 * outside the screen are ignored. Same signature as menu_fa_pixel_fn.
 */
void glcd_st7920_set_pixel(void *ctx, int x, int y, bool on);

/** Send the changed rows of the framebuffer to the display. */
void glcd_st7920_flush(glcd_st7920_t *lcd);

#ifdef __cplusplus
}
#endif
#endif /* GLCD_ST7920_H */
