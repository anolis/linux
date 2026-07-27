// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Nintendo GameCube/Wii framebuffer udbg output support.
 * Copyright (C) 2008-2009 The GameCube Linux Team
 * Copyright (C) 2008,2009 Albert Herranz
 *
 * Based on arch/ppc/platforms/gcn-con.c
 *
 * Nintendo GameCube early debug console
 * Copyright (C) 2004-2005 The GameCube Linux Team
 *
 * Based on console.c by tmbinc.
 *
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/font.h>
#include <linux/of_address.h>
#include <linux/printk.h>
#include <linux/string.h>

#include <asm/udbg.h>

#include "gcnvi_udbg.h"

/*
 * Console settings.
 *
 */
#define SCREEN_WIDTH	640
#define SCREEN_HEIGHT	480

#define FONT_XSIZE  8
#define FONT_YSIZE  16
#define FONT_XFACTOR 1
#define FONT_YFACTOR 1
#define FONT_XGAP   2
#define FONT_YGAP   0

#define COLOR_WHITE 0xFF80FF80
#define COLOR_BLACK 0x00800080

struct console_data {
	unsigned char *framebuffer;
	int xres, yres, stride;

	const unsigned char *font;

	int cursor_x, cursor_y;
	u32 foreground, background;

	int border_left, border_right, border_top, border_bottom;

	int scrolled_lines;
};

static struct console_data *default_console;

static void console_drawc(struct console_data *con, int x, int y,
			  unsigned char c)
{
	int ax, ay;
	u32 *ptr;
	u32 color2x[2];
	int bits;

	x >>= 1;
	ptr = (u32 *)(con->framebuffer + con->stride * y + x * 4);

	for (ay = 0; ay < FONT_YSIZE; ay++) {
#if FONT_XFACTOR == 2
		u32 color;

		for (ax = 0; ax < 8; ax++) {
			if ((con->font[c * FONT_YSIZE + ay] << ax) & 0x80)
				color = con->foreground;
			else
				color = con->background;
#if FONT_YFACTOR == 2
			/* pixel doubling: we write u32 */
			ptr[ay * 2 * con->stride / 4 + ax] = color;
			/* line doubling */
			ptr[(ay * 2 + 1) * con->stride / 4 + ax] = color;
#else
			ptr[ay * con->stride / 4 + ax] = color;
#endif
		}
#else
		for (ax = 0; ax < 4; ax++) {
			bits = (con->font[c * FONT_YSIZE + ay] << (ax * 2));
			if (bits & 0x80)
				color2x[0] = con->foreground;
			else
				color2x[0] = con->background;
			if (bits & 0x40)
				color2x[1] = con->foreground;
			else
				color2x[1] = con->background;
			ptr[ay * con->stride / 4 + ax] =
			    (color2x[0] & 0xFFFF00FF) |
			    (color2x[1] & 0x0000FF00);
		}
#endif
	}
}

static void console_putc(struct console_data *con, char c)
{
	const int scroll_height = FONT_YSIZE * FONT_YFACTOR + FONT_YGAP;
	size_t copy_len;
	int cnt;
	u32 *ptr;

	switch (c) {
	case '\n':
		con->cursor_y += scroll_height;
		con->cursor_x = con->border_left;
		break;
	default:
		console_drawc(con, con->cursor_x, con->cursor_y, c);
		con->cursor_x += FONT_XSIZE * FONT_XFACTOR + FONT_XGAP;
		if ((con->cursor_x + (FONT_XSIZE * FONT_XFACTOR)) >
		    con->border_right) {
			con->cursor_y += scroll_height;
			con->cursor_x = con->border_left;
		}
	}
	if ((con->cursor_y + FONT_YSIZE * FONT_YFACTOR) >= con->border_bottom) {
		copy_len = con->stride * (con->yres - scroll_height);
		memmove(con->framebuffer,
			con->framebuffer + con->stride * scroll_height,
			copy_len);
		cnt = con->stride * scroll_height / sizeof(*ptr);
		ptr = (u32 *)(con->framebuffer + copy_len);
		while (cnt--)
			*ptr++ = con->background;
		con->cursor_y -= scroll_height;
	}
}

static void console_init(struct console_data *con, void *framebuffer,
			 int xres, int yres, int stride)
{
	int c;
	u32 *p;

	con->framebuffer = framebuffer;
	con->xres = xres;
	con->yres = yres;
	con->border_left = 0;
	con->border_top = 0;
	con->border_right = con->xres;
	con->border_bottom = con->yres;
	con->stride = stride;
	con->cursor_x = con->cursor_y = 0;

	con->font = font_vga_8x16.data;

	con->foreground = COLOR_WHITE;
	con->background = COLOR_BLACK;

	con->scrolled_lines = 0;

	/* clear screen */
	c = con->xres * con->yres / 2;
	p = (u32 *)con->framebuffer;
	while (c--)
		*p++ = con->background;

	default_console = con;
}

/*
 * Video hardware setup.
 *
 */

/* Hardware registers */
#define VI_TFBL                 0x1c
#define VI_TFBR                 0x20
#define VI_BFBL                 0x24
#define VI_BFBR                 0x28
#define VI_DPV                  0x2c

/* NTSC settings (640x480) */
static const u32 vi_Mode640X480NtscYUV16[32] = {
	0x0F060001, 0x476901AD, 0x02EA5140, 0x00030018,
	0x00020019, 0x410C410C, 0x40ED40ED, 0x00435A4E,
	0x00000000, 0x00435A4E, 0x00000000, 0x00000000,
	0x110701AE, 0x10010001, 0x00010001, 0x00010001,
	0x00000000, 0x00000000, 0x28500100, 0x1AE771F0,
	0x0DB4A574, 0x00C1188E, 0xC4C0CBE2, 0xFCECDECF,
	0x13130F08, 0x00080C0F, 0x00FF0000, 0x00000000,
	0x02800000, 0x000000FF, 0x00FF00FF, 0x00FF00FF
};

static void vi_setup_video(void __iomem *io_base, u32 xfb_start)
{
	const u32 *regs = vi_Mode640X480NtscYUV16;
	int i;

	/* initialize video registers */
	for (i = 0; i < 7; i++)
		out_be32(io_base + i * sizeof(__u32), regs[i]);

	out_be32(io_base + VI_TFBR, regs[VI_TFBR / sizeof(__u32)]);
	out_be32(io_base + VI_BFBR, regs[VI_BFBR / sizeof(__u32)]);
	out_be32(io_base + VI_DPV, regs[VI_DPV / sizeof(__u32)]);
	for (i = 16; i < 32; i++)
		out_be32(io_base + i * sizeof(__u32), regs[i]);

	/* set framebuffer address, interlaced mode */
	out_be32(io_base + VI_TFBL, 0x10000000 | (xfb_start >> 5));
	xfb_start += 2 * SCREEN_WIDTH;	/* line length */
	out_be32(io_base + VI_BFBL, 0x10000000 | (xfb_start >> 5));
}

/*
 * udbg functions.
 *
 */

/* OF bindings */
static const struct of_device_id gcnvi_udbg_ids[] __initconst = {
	{ .compatible = "nintendo,hollywood-vi", },
	{ .compatible = "nintendo,flipper-vi", },
};

static struct console_data gcnvi_udbg_console;

/*
 * Transmits a character.
 */
static void gcnvi_udbg_putc(char ch)
{
	if (default_console)
		console_putc(default_console, ch);
}

/*
 * Initializes udbg support.
 */
void __init gcnvi_udbg_init(void)
{
	struct device_node *np;
	u32 xfb_start, xfb_size;
	void *screen_base;
	void __iomem *io_base;

	np = of_find_matching_node(NULL, gcnvi_udbg_ids);
	if (!np)
		return;

	if (of_property_read_u32(np, "xfb-start", &xfb_start) ||
	    of_property_read_u32(np, "xfb-size", &xfb_size)) {
		of_node_put(np);
		return;
	}

	io_base = of_iomap(np, 0);

	of_node_put(np);

	if (!io_base)
		return;

	if (xfb_size < 2 * SCREEN_WIDTH * SCREEN_HEIGHT) {
		iounmap(io_base);
		return;
	}

	screen_base = (void *)ioremap(xfb_start, xfb_size);
	if (!screen_base) {
		iounmap(io_base);
		return;
	}

	vi_setup_video(io_base, xfb_start);
	iounmap(io_base);
	console_init(&gcnvi_udbg_console, screen_base,
		     SCREEN_WIDTH, SCREEN_HEIGHT, 2 * SCREEN_WIDTH);

	udbg_putc = gcnvi_udbg_putc;
	pr_info("gcnvi_udbg: ready, XFB 0x%08x+0x%x\n",
		xfb_start, xfb_size);
}
