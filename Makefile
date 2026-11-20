ifeq ($(origin CC),default)
CC = gcc
endif
CFLAGS ?= -std=c99 -Wall -Wextra -Wpedantic -O2

FA_SRC  = menu_fa.c menu_fa_font8.c menu_fa_font16.c
FA_HDR  = menu_fa.h menu_fa_flash.h
DRV_SRC = examples/drivers/glcd_ks0108.c examples/drivers/glcd_st7920.c
PYTHON ?= python

all: demo demo_fa test_menu test_menu_fa test_drivers

demo: menu.c menu.h examples/demo.c
	$(CC) $(CFLAGS) -I. menu.c examples/demo.c -o $@

demo_fa: menu.c menu.h $(FA_SRC) $(FA_HDR) examples/demo_fa.c
	$(CC) $(CFLAGS) -I. menu.c $(FA_SRC) examples/demo_fa.c -o $@

test_menu: menu.c menu.h tests/test_menu.c
	$(CC) $(CFLAGS) -I. menu.c tests/test_menu.c -o $@

test_menu_fa: $(FA_SRC) $(FA_HDR) tests/test_menu_fa.c
	$(CC) $(CFLAGS) -I. $(FA_SRC) tests/test_menu_fa.c -o $@

test_drivers: $(FA_SRC) $(DRV_SRC) tests/test_drivers.c
	$(CC) $(CFLAGS) -I. -Iexamples/drivers $(FA_SRC) $(DRV_SRC) tests/test_drivers.c -o $@

test: test_menu test_menu_fa test_drivers
	./test_menu
	./test_menu_fa
	./test_drivers

# Regenerate the fonts after editing tools/font8.txt or tools/font16.txt.
fonts:
	$(PYTHON) tools/gen_fa_fonts.py build tools/font8.txt  font8  > menu_fa_font8.c
	$(PYTHON) tools/gen_fa_fonts.py build tools/font16.txt font16 > menu_fa_font16.c

clean:
	rm -f demo demo.exe demo_fa demo_fa.exe test_menu test_menu.exe \
	      test_menu_fa test_menu_fa.exe test_drivers test_drivers.exe

.PHONY: all test fonts clean
