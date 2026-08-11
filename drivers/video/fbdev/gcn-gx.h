/*
 * drivers/video/fbdev/gcn-gx.h
 *
 * Nintendo GameCube/Wii GX GPU minimal driver
 * Provides hardware EFB->XFB copy to replace software RGB->YUV conversion
 * in gcnfb.c (vi_transcode_RGB*).
 *
 * Register reference derived from libogc (devkitPro/libogc).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _GCN_GX_H
#define _GCN_GX_H

#include <linux/types.h>

/* CP register indices (16-bit, word-indexed) */
#define CP_REG_STATUS		0	/* SR: status */
#define CP_REG_CTRL		1	/* CR: control */
#define CP_REG_CLR		2	/* clear (byte 0x04) */
#define CP_REG_FIFO_BASE_LO	16
#define CP_REG_FIFO_BASE_HI	17
#define CP_REG_FIFO_END_LO	18
#define CP_REG_FIFO_END_HI	19
#define CP_REG_FIFO_HIWM_LO	20
#define CP_REG_FIFO_HIWM_HI	21
#define CP_REG_FIFO_LOWM_LO	22
#define CP_REG_FIFO_LOWM_HI	23
#define CP_REG_RWDST_LO		24
#define CP_REG_RWDST_HI		25
#define CP_REG_WT_LO		26
#define CP_REG_WT_HI		27
#define CP_REG_RD_LO		28
#define CP_REG_RD_HI		29
/*
 * GX_ReadGPMetric() (libogc) reads _cpReg[32]/[33] as a 32-bit
 * perf-counter value (16-bit low/high halves), tied to whatever
 * GX_SetGPMetric() (BP 0x23, GX_PERF0_*) last selected. Index-to-byte
 * mapping matches CP_REG_RD_LO/HI above (index*2 = byte offset).
 */
#define CP_REG_PERF0_LO		32	/* byte offset 0x40 */
#define CP_REG_PERF0_HI		33	/* byte offset 0x42 */

/* CP control register bits */
#define CP_CR_GPRESET		BIT(0)	/* GP FIFO read enable */
#define CP_CR_RDINT_EN		BIT(2)	/* FIFO underflow interrupt */
#define CP_CR_WRINT_EN		BIT(3)	/* FIFO overflow interrupt */
#define CP_CR_LINKEN		BIT(4)	/* link CPU/GP FIFOs */

/* PE register indices (16-bit, word-indexed from PE base 0x0C001000) */
#define PE_REG_INTR_STATUS	5	/* byte offset 0x0a */
#define PE_REG_TOKEN		7	/* byte offset 0x0e */
/*
 * PE interrupt-status layout (libogc / YAGCD):
 *   bit 0: PETokenEnable  (interrupt enable)
 *   bit 1: PEFinishEnable (interrupt enable)
 *   bit 2: PEToken  (status - token was received)
 *   bit 3: PEFinish (status - draw-done fired after BP 0x45)
 *
 * Status bits are write-one-to-clear. PE event sources may be enabled while
 * their Flipper PIC lines remain masked for polling from VI IRQ context.
 */
#define PE_FINISH_BIT		0x0008	/* bit 3: PEFinish status */
#define PE_FINISH_ENABLE	0x0002	/* bit 1: enable finish signalling */
#define PE_TOKEN_BIT		0x0004	/* bit 2: PEToken status */
#define PE_TOKEN_ENABLE		0x0001	/* bit 0: enable token signalling */

/* BP command opcode — written to wgPipe before a 32-bit BP register value */
#define GX_CMD_LOAD_BP_REG	0x61

/* BP register addresses (upper byte of the 32-bit BP write value) */
#define BP_DISP_COPY_TL		0x49	/* EFB copy source top-left */
#define BP_DISP_COPY_WH		0x4a	/* EFB copy source width/height */
#define BP_DISP_COPY_DST	0x4d	/* EFB copy dest stride */
#define BP_DISP_COPY_ADDR	0x4b	/* EFB copy dest address (>>5) */
#define BP_DISP_COPY_CTRL	0x52	/* EFB copy control/execute */

/* dispCopyCntrl bits */
#define COPY_CTRL_CLAMP_TOP	BIT(0)
#define COPY_CTRL_CLAMP_BOT	BIT(1)
#define COPY_CTRL_GAMMA_SHIFT	7	/* 2 bits */
#define COPY_CTRL_YSCALE	BIT(10)
#define COPY_CTRL_CLEAR		BIT(11)
#define COPY_CTRL_FRAME2FIELD	BIT(12)
#define COPY_CTRL_TO_XFB	BIT(14)	/* select YUYV XFB instead of texture copy */

/* GX_GM_1_0 gamma (no correction) */
#define GX_GM_1_0		0

/* FIFO: 64KB, must be 32-byte aligned */
#define GX_FIFO_SIZE		(64 * 1024)
#define GX_FIFO_HIWATERMARK	(16 * 1024)

/* Texture tile buffer: max FB is 640×576 (PAL) RGB565 = 737,280 bytes.
 * Must live in MEM1: the GX texture unit is GameCube-era hardware that
 * cannot address MEM2 (0x10000000+).  kmalloc returns MEM2 on Wii Linux
 * (MEM1 and MEM2 are coalesced).  Use the DTS-reserved region instead. */
#define GX_TEX_BUF_SIZE		(640 * 576 * 2)
#define GX_TEX_BUF_SLOT_SIZE	(768 * 1024)

#endif /* _GCN_GX_H */
