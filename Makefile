ifeq ($(origin CC),default)
CC = gcc
endif
CFLAGS ?= -std=c99 -Wall -Wextra -Wpedantic -O2

all: demo test_menu

demo: menu.c menu.h examples/demo.c
	$(CC) $(CFLAGS) -I. menu.c examples/demo.c -o $@

test_menu: menu.c menu.h tests/test_menu.c
	$(CC) $(CFLAGS) -I. menu.c tests/test_menu.c -o $@

test: test_menu
	./test_menu

clean:
	rm -f demo demo.exe test_menu test_menu.exe

.PHONY: all test clean
