# SPDX-License-Identifier: GPL-2.0

CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
LDFLAGS ?= -static

all: wii-ave-reg

wii-ave-reg: wii-ave-reg.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	$(RM) wii-ave-reg

.PHONY: all clean
