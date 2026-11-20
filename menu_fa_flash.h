/**
 * @file menu_fa_flash.h
 * @brief Program-memory helpers shared by menu_fa.c and the font files.
 *
 * On AVR, const data is copied to RAM at startup unless it is marked PROGMEM
 * and read with pgm_read_*(). On ARM and most other targets const data
 * already stays in Flash and is read directly, so these macros do nothing.
 * Internal header: applications don't need to include it.
 */
#ifndef MENU_FA_FLASH_H
#define MENU_FA_FLASH_H

#if defined(__AVR__)
#include <avr/pgmspace.h>
#define MENU_FA_FLASH            PROGMEM
#define MENU_FA_READ_BYTE(addr)  pgm_read_byte(addr)
#define MENU_FA_READ_WORD(addr)  pgm_read_word(addr)
#else
#define MENU_FA_FLASH
#define MENU_FA_READ_BYTE(addr)  (*(addr))
#define MENU_FA_READ_WORD(addr)  (*(addr))
#endif

#endif /* MENU_FA_FLASH_H */
