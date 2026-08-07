// SPDX-License-Identifier: GPL-2.0-only
/*
 * Nintendo GameCube/Wii XFB and display MMIO read-only diagnostic observer
 * Copyright (C) 2026 Bill Carson
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>

#include <asm/cacheflush.h>

#define GCN_VI_PHYS		0x0c002000
#define GCN_VI_SIZE		0x100
#define GCN_VI_TFBL		0x1c
#define GCN_VI_BFBL		0x24

#define GCN_GX_PHYS		0x0c000000
#define GCN_GX_SIZE		0x4000
#define GCN_GX_CP		0x0000
#define GCN_GX_PE		0x1000
#define GCN_GX_PI		0x3000

#define GCN_GPIO_PHYS		0x0d8000c0
#define GCN_GPIO_SIZE		0x40

#define GCN_XFB_PHYS		0x01698000
#define GCN_XFB_SIZE		0x00168000
#define GCN_XFB_PITCH		(640 * 2)
#define GCN_XFB_PAGE_SIZE	(GCN_XFB_PITCH * 480)
#define GCN_XFB_FBA_MASK	0x00ffffff

#define GCN_BAR_X		320
#define GCN_BAR_Y0		48
#define GCN_BAR_STEP		16
#define GCN_BAR_COUNT		7

static void __iomem *vi;
static void __iomem *gx;
static void __iomem *gpio;
static void *xfb;
static bool flush_selected;
module_param(flush_selected, bool, 0444);
MODULE_PARM_DESC(flush_selected,
		 "flush the selected XFB page after sampling (default: false)");

static char *snapshot_id = "unlabeled";
module_param(snapshot_id, charp, 0444);
MODULE_PARM_DESC(snapshot_id, "label included with every snapshot line");

static unsigned int snapshot_count = 2;
module_param(snapshot_count, uint, 0444);
MODULE_PARM_DESC(snapshot_count, "number of repeated MMIO snapshots (1-8)");

static unsigned int snapshot_delay_us = 1000;
module_param(snapshot_delay_us, uint, 0444);
MODULE_PARM_DESC(snapshot_delay_us,
		 "delay between repeated snapshots in microseconds");

static void gcn_log_mmio_snapshot(unsigned int sample)
{
	pr_info("gcn-xfb-observer: id=%s sample=%u CP status=%04x ctrl=%04x base=%04x%04x end=%04x%04x hiwm=%04x%04x lowm=%04x%04x rwdst=%04x%04x wt=%04x%04x rd=%04x%04x perf0=%04x%04x\n",
		snapshot_id, sample,
		in_be16(gx + GCN_GX_CP + 0x00),
		in_be16(gx + GCN_GX_CP + 0x02),
		in_be16(gx + GCN_GX_CP + 0x22),
		in_be16(gx + GCN_GX_CP + 0x20),
		in_be16(gx + GCN_GX_CP + 0x26),
		in_be16(gx + GCN_GX_CP + 0x24),
		in_be16(gx + GCN_GX_CP + 0x2a),
		in_be16(gx + GCN_GX_CP + 0x28),
		in_be16(gx + GCN_GX_CP + 0x2e),
		in_be16(gx + GCN_GX_CP + 0x2c),
		in_be16(gx + GCN_GX_CP + 0x32),
		in_be16(gx + GCN_GX_CP + 0x30),
		in_be16(gx + GCN_GX_CP + 0x36),
		in_be16(gx + GCN_GX_CP + 0x34),
		in_be16(gx + GCN_GX_CP + 0x3a),
		in_be16(gx + GCN_GX_CP + 0x38),
		in_be16(gx + GCN_GX_CP + 0x42),
		in_be16(gx + GCN_GX_CP + 0x40));
	pr_info("gcn-xfb-observer: id=%s sample=%u PE status=%04x token=%04x PI base=%08x end=%08x wptr=%08x\n",
		snapshot_id, sample,
		in_be16(gx + GCN_GX_PE + 0x0a),
		in_be16(gx + GCN_GX_PE + 0x0e),
		in_be32(gx + GCN_GX_PI + 0x0c),
		in_be32(gx + GCN_GX_PI + 0x10),
		in_be32(gx + GCN_GX_PI + 0x14));
	pr_info("gcn-xfb-observer: id=%s sample=%u GPIO bout=%08x bdir=%08x bin=%08x bilvl=%08x biflag=%08x bimask=%08x bimir=%08x enable=%08x out=%08x dir=%08x in=%08x ilvl=%08x iflag=%08x imask=%08x imir=%08x owner=%08x\n",
		snapshot_id, sample,
		in_be32(gpio + 0x00), in_be32(gpio + 0x04),
		in_be32(gpio + 0x08), in_be32(gpio + 0x0c),
		in_be32(gpio + 0x10), in_be32(gpio + 0x14),
		in_be32(gpio + 0x18), in_be32(gpio + 0x1c),
		in_be32(gpio + 0x20), in_be32(gpio + 0x24),
		in_be32(gpio + 0x28), in_be32(gpio + 0x2c),
		in_be32(gpio + 0x30), in_be32(gpio + 0x34),
		in_be32(gpio + 0x38), in_be32(gpio + 0x3c));
}

static int __init gcn_xfb_observer_init(void)
{
	u32 bottom;
	u32 offset;
	u32 top;
	u32 tfbl;
	unsigned int i;
	unsigned int sample;

	if (!snapshot_count || snapshot_count > 8 ||
	    snapshot_delay_us > USEC_PER_SEC)
		return -EINVAL;

	vi = ioremap(GCN_VI_PHYS, GCN_VI_SIZE);
	if (!vi)
		return -ENOMEM;
	gx = ioremap(GCN_GX_PHYS, GCN_GX_SIZE);
	if (!gx) {
		iounmap(vi);
		vi = NULL;
		return -ENOMEM;
	}
	gpio = ioremap(GCN_GPIO_PHYS, GCN_GPIO_SIZE);
	if (!gpio) {
		iounmap(gx);
		gx = NULL;
		iounmap(vi);
		vi = NULL;
		return -ENOMEM;
	}

	tfbl = in_be32(vi + GCN_VI_TFBL);
	bottom = in_be32(vi + GCN_VI_BFBL);
	top = (tfbl & GCN_XFB_FBA_MASK) << 5;
	if (top < GCN_XFB_PHYS || top >= GCN_XFB_PHYS + GCN_XFB_SIZE) {
		pr_err("gcn-xfb-observer: TFBL=%08x decodes outside XFB (%08x)\n",
		       tfbl, top);
		iounmap(vi);
		vi = NULL;
		iounmap(gpio);
		gpio = NULL;
		iounmap(gx);
		gx = NULL;
		return -ERANGE;
	}

	xfb = memremap(GCN_XFB_PHYS, GCN_XFB_SIZE, MEMREMAP_WB);
	if (!xfb) {
		iounmap(vi);
		vi = NULL;
		iounmap(gpio);
		gpio = NULL;
		iounmap(gx);
		gx = NULL;
		return -ENOMEM;
	}

	pr_info("gcn-xfb-observer: id=%s TFBL=%08x BFBL=%08x top=%08x\n",
		snapshot_id, tfbl, bottom, top);
	for (i = 0; i < GCN_BAR_COUNT; i++) {
		offset = top - GCN_XFB_PHYS +
			 (GCN_BAR_Y0 + i * GCN_BAR_STEP) * GCN_XFB_PITCH +
			 (GCN_BAR_X / 2) * sizeof(u32);
		pr_info("gcn-xfb-observer: id=%s row=%u phys=%08x xfb=%08x\n",
			snapshot_id, i, GCN_XFB_PHYS + offset,
			READ_ONCE(*(u32 *)(xfb + offset)));
	}
	for (sample = 0; sample < snapshot_count; sample++) {
		for (i = 0; i < GCN_VI_SIZE; i += 4 * sizeof(u32))
			pr_info("gcn-xfb-observer: id=%s sample=%u VI+%02x %08x %08x %08x %08x\n",
				snapshot_id, sample, i,
				in_be32(vi + i), in_be32(vi + i + 4),
				in_be32(vi + i + 8), in_be32(vi + i + 12));
		gcn_log_mmio_snapshot(sample);
		if (sample + 1 < snapshot_count && snapshot_delay_us)
			usleep_range(snapshot_delay_us, snapshot_delay_us + 100);
	}
	if (flush_selected) {
		offset = top - GCN_XFB_PHYS;
		flush_dcache_range((unsigned long)xfb + offset,
				   (unsigned long)xfb + offset +
				   GCN_XFB_PAGE_SIZE);
		pr_info("gcn-xfb-observer: flushed selected page top=%08x size=%08x\n",
			top, GCN_XFB_PAGE_SIZE);
	}

	return 0;
}

static void __exit gcn_xfb_observer_exit(void)
{
	if (xfb)
		memunmap(xfb);
	if (gpio)
		iounmap(gpio);
	if (gx)
		iounmap(gx);
	if (vi)
		iounmap(vi);
}

module_init(gcn_xfb_observer_init);
module_exit(gcn_xfb_observer_exit);

MODULE_AUTHOR("Bill Carson <anolisporcatus@gmail.com>");
MODULE_DESCRIPTION("Nintendo GameCube/Wii read-only XFB/MMIO diagnostic observer");
MODULE_LICENSE("GPL");
