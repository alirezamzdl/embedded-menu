/**
 * @file glcd_ks0108.h
 * @brief Portable driver for 128x64 KS0108 graphic LCDs (two controllers).
 *
 * - Keeps a 1 KB framebuffer in RAM and only sends the 64-byte blocks that
 *   changed (glcd_ks0108_flush()).
 * - Write-only: tie the module's R/W pin to GND. Busy flag is not read; the
 *   driver waits KS0108_WRITE_DELAY_US after every write instead.
 * - Hardware access goes through a small HAL you provide, so it works on
 *   any MCU (STM32 HAL/LL, AVR, ESP32...).
 * - glcd_ks0108_set_pixel() has the menu_fa_pixel_fn signature, so it can be
 *   passed straight to menu_fa_draw() / menu_fa_draw_row().
 *
 * Wiring: D0..D7 data bus, D/I (RS), E, CS1 (left half, x 0..63),
 * CS2 (right half, x 64..127), RST (optional), R/W to GND.
 *
 * Example HAL for STM32 HAL (data bus on GPIOA pins 0..7):
 *
 *   static void bus(uint8_t v)         { GPIOA->ODR = (GPIOA->ODR & 0xFF00u) | v; }
 *   static void rs(bool h)             { HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, h); }
 *   static void en(bool h)             { HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, h); }
 *   static void cs(uint8_t chip, bool active)
 *   {
 *       // Most modules have active-high CS; invert `active` if yours is active-low.
 *       HAL_GPIO_WritePin(GPIOB, chip ? GPIO_PIN_3 : GPIO_PIN_2, active);
 *   }
 *   static void rst(bool h)            { HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, h); }
 *   static void delay_us(uint16_t us)  { my_delay_us(us); }   // e.g. DWT or a timer
 *
 *   static const glcd_ks0108_hal_t hal = { bus, rs, en, cs, rst, delay_us };
 *   static glcd_ks0108_t lcd;
 *
 *   glcd_ks0108_init(&lcd, &hal);
 *   menu_fa_draw_row(&menu_fa_font8, "سلام", 0, 127, 0, false, glcd_ks0108_set_pixel, &lcd);
 *   glcd_ks0108_flush(&lcd);
 */
#ifndef GLCD_KS0108_H
#define GLCD_KS0108_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#define GLCD_KS0108_WIDTH   128
#define GLCD_KS0108_HEIGHT  64

/** Wait after each write (µs). Raise it if your module misses bytes. */
#ifndef KS0108_WRITE_DELAY_US
#define KS0108_WRITE_DELAY_US 5
#endif

/** Hardware access. All functions are required except set_rst. */
typedef struct {
    void (*write_bus)(uint8_t value);           /**< Put a byte on D0..D7 */
    void (*set_rs)(bool high);                  /**< D/I pin: high = data, low = command */
    void (*set_e)(bool high);                   /**< E (enable) pin */
    void (*set_cs)(uint8_t chip, bool active);  /**< chip 0 = CS1 (left), 1 = CS2 (right) */
    void (*set_rst)(bool high);                 /**< RST pin, or NULL if tied high */
    void (*delay_us)(uint16_t us);              /**< Busy-wait at least `us` microseconds */
} glcd_ks0108_hal_t;

/** Driver state: framebuffer plus which blocks need sending. */
typedef struct {
    const glcd_ks0108_hal_t *hal;
    uint8_t fb[GLCD_KS0108_HEIGHT / 8][GLCD_KS0108_WIDTH]; /**< [page][x], bit = y % 8 */
    uint8_t dirty[GLCD_KS0108_HEIGHT / 8];                 /**< bit 0 = left half, bit 1 = right */
} glcd_ks0108_t;

/** Reset and switch on the display, then clear it. */
void glcd_ks0108_init(glcd_ks0108_t *lcd, const glcd_ks0108_hal_t *hal);

/** Clear the framebuffer (call flush to show it). */
void glcd_ks0108_clear(glcd_ks0108_t *lcd);

/**
 * Set one pixel in the framebuffer. `ctx` is the glcd_ks0108_t. Pixels
 * outside the screen are ignored. Same signature as menu_fa_pixel_fn.
 */
void glcd_ks0108_set_pixel(void *ctx, int x, int y, bool on);

/** Send the changed parts of the framebuffer to the display. */
void glcd_ks0108_flush(glcd_ks0108_t *lcd);

#ifdef __cplusplus
}
#endif
#endif /* GLCD_KS0108_H */
