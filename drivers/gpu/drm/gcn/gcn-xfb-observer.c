// SPDX-License-Identifier: GPL-2.0-only
/*
 * Nintendo GameCube/Wii XFB read-only diagnostic observer
 * Copyright (C) 2026 Bill Carson
 */

#include <linux/io.h>
#include <linux/module.h>

#define GCN_VI_PHYS		0x0c002000
#define GCN_VI_SIZE		0x100
#define GCN_VI_TFBL		0x1c
#define GCN_VI_BFBL		0x24

#define GCN_XFB_PHYS		0x01698000
#define GCN_XFB_SIZE		0x00168000
#define GCN_XFB_PITCH		(640 * 2)
#define GCN_XFB_FBA_MASK	0x00ffffff

#define GCN_BAR_X		320
#define GCN_BAR_Y0		48
#define GCN_BAR_STEP		16
#define GCN_BAR_COUNT		7

static void __iomem *vi;
static void __iomem *xfb;

static int __init gcn_xfb_observer_init(void)
{
	u32 bottom;
	u32 offset;
	u32 top;
	u32 tfbl;
	unsigned int i;

	vi = ioremap(GCN_VI_PHYS, GCN_VI_SIZE);
	if (!vi)
		return -ENOMEM;

	tfbl = in_be32(vi + GCN_VI_TFBL);
	bottom = in_be32(vi + GCN_VI_BFBL);
	top = (tfbl & GCN_XFB_FBA_MASK) << 5;
	if (top < GCN_XFB_PHYS || top >= GCN_XFB_PHYS + GCN_XFB_SIZE) {
		pr_err("gcn-xfb-observer: TFBL=%08x decodes outside XFB (%08x)\n",
		       tfbl, top);
		iounmap(vi);
		vi = NULL;
		return -ERANGE;
	}

	xfb = ioremap(GCN_XFB_PHYS, GCN_XFB_SIZE);
	if (!xfb) {
		iounmap(vi);
		vi = NULL;
		return -ENOMEM;
	}

	pr_info("gcn-xfb-observer: TFBL=%08x BFBL=%08x top=%08x\n",
		tfbl, bottom, top);
	for (i = 0; i < GCN_BAR_COUNT; i++) {
		offset = top - GCN_XFB_PHYS +
			 (GCN_BAR_Y0 + i * GCN_BAR_STEP) * GCN_XFB_PITCH +
			 (GCN_BAR_X / 2) * sizeof(u32);
		pr_info("gcn-xfb-observer: row=%u phys=%08x xfb=%08x\n",
			i, GCN_XFB_PHYS + offset, in_be32(xfb + offset));
	}

	return 0;
}

static void __exit gcn_xfb_observer_exit(void)
{
	if (xfb)
		iounmap(xfb);
	if (vi)
		iounmap(vi);
}

module_init(gcn_xfb_observer_init);
module_exit(gcn_xfb_observer_exit);

MODULE_AUTHOR("Bill Carson <anolisporcatus@gmail.com>");
MODULE_DESCRIPTION("Nintendo GameCube/Wii read-only XFB diagnostic observer");
MODULE_LICENSE("GPL");
