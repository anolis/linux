# SPDX-License-Identifier: GPL-2.0

CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra

all: wii-ave-reg

wii-ave-reg: wii-ave-reg.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	$(RM) wii-ave-reg

.PHONY: all clean
