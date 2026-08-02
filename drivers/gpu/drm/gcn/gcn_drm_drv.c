// SPDX-License-Identifier: GPL-2.0-only
/*
 * Nintendo GameCube/Wii Video Interface DRM/KMS driver
 * Copyright (C) 2026 Bill Carson
 */

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <asm/cacheflush.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>

#define GCN_DRM_NAME		"gcn-vi"
#define GCN_DRM_DESC		"Nintendo GameCube/Wii VI DRM"

#define GCN_DRM_WIDTH		640
#define GCN_DRM_HEIGHT		480
#define GCN_DRM_XFB_PITCH	(GCN_DRM_WIDTH * 2)
#define GCN_DRM_XFB_PAGE_SIZE	(GCN_DRM_XFB_PITCH * GCN_DRM_HEIGHT)

#define AVE_CHROMA_SWAP_REG	0x62

#define VI_VTR			0x00
#define VI_DCR			0x02
#define VI_HTR0			0x04
#define VI_HTR1			0x08
#define VI_VTO			0x0c
#define VI_VTE			0x10
#define VI_BBOI			0x14
#define VI_BBEI			0x18
#define VI_TFBL			0x1c
#define VI_TFBR			0x20
#define VI_BFBL			0x24
#define VI_BFBR			0x28
#define VI_DI0			0x30
#define VI_DI1			0x34
#define VI_DI2			0x38
#define VI_DI3			0x3c
#define VI_PCR			0x48
#define VI_HSR			0x4a
#define VI_FCT0			0x4c
#define VI_FCT1			0x50
#define VI_FCT2			0x54
#define VI_FCT3			0x58
#define VI_FCT4			0x5c
#define VI_FCT5			0x60
#define VI_FCT6			0x64
#define VI_AA			0x68
#define VI_CLK			0x6c
#define VI_HSW			0x70
#define VI_HBE			0x72
#define VI_HBS			0x74
#define VI_UNK1			0x76
#define VI_UNK2			0x78
#define VI_UNK3			0x7c

#define VI_DCR_NIN		BIT(2)
#define VI_DCR_ENABLE		BIT(0)
#define VI_DI_IRQ		BIT(31)
#define VI_DI_ENABLE		BIT(28)
#define VI_FB_POB		BIT(28)
#define VI_FB_XOF_SHIFT		24

#define VI_NTSC_VTR		0x0f06
#define VI_NTSC_HTR0		0x476901ad
#define VI_NTSC_HTR1		0x02e850c0
#define VI_NTSC_VTO		0x00030018
#define VI_NTSC_VTE		0x00020019
#define VI_NTSC_BBOI		0x410c410c
#define VI_NTSC_BBEI		0x40ed40ed
#define VI_NTSC_PCR		0x2850
#define VI_NTSC_HSR		0x0100
#define VI_NTSC_DI0		0x00010001
#define VI_NTSC_DI1		0x00f101ae

#define RGB2YUV_SHIFT		16
#define RGB2YUV_LUMA_565		16
#define RGB2YUV_LUMA_888		32
#define RGB2YUV_CHROMA_565	16
#define RGB2YUV_CHROMA_888	32

#define RGB2YUV_YR		19595
#define RGB2YUV_YG		38469
#define RGB2YUV_YB		7471
#define RGB2YUV_UR		(-11076)
#define RGB2YUV_UG		(-21692)
#define RGB2YUV_UB		32768
#define RGB2YUV_VR		32768
#define RGB2YUV_VG		(-27460)
#define RGB2YUV_VB		(-5308)

struct gcn_drm {
	struct drm_device drm;
	struct drm_simple_display_pipe pipe;
	struct drm_connector connector;
	void __iomem *vi_base;
	void *xfb;
	u32 xfb_phys;
	u32 xfb_size;
	spinlock_t scanout_lock;
	unsigned int visible_page;
	unsigned int pending_page;
	bool flip_pending;
};

static bool program_mode = true;
module_param(program_mode, bool, 0444);
MODULE_PARM_DESC(program_mode,
		 "program fixed 640x480 NTSC interlaced VI timing (default: true)");

static const u32 gcn_drm_vi_filter[] = {
	0x1ae771f0, 0x0db4a574, 0x00c1188e, 0xc4c0cbe2,
	0xfcecdecf, 0x13130f08, 0x00080c0f,
};

static inline struct gcn_drm *to_gcn_drm(struct drm_device *drm)
{
	return container_of(drm, struct gcn_drm, drm);
}

static int gcn_drm_clear_ave_chroma_swap(struct device *dev)
{
	struct device_node *ave_node;
	struct i2c_client *ave;
	u8 command[] = { AVE_CHROMA_SWAP_REG, 0x00 };
	int ret;

	ave_node = of_parse_phandle(dev->of_node, "audio-video-encoder", 0);
	if (!ave_node) {
		if (of_device_is_compatible(dev->of_node,
					    "nintendo,hollywood-vi"))
			return dev_err_probe(dev, -ENODEV,
					     "missing audio-video-encoder\n");
		return 0;
	}

	ave = of_find_i2c_device_by_node(ave_node);
	of_node_put(ave_node);
	if (!ave)
		return dev_err_probe(dev, -EPROBE_DEFER,
				     "AVE I2C client is not ready\n");

	ret = i2c_master_send(ave, command, sizeof(command));
	put_device(&ave->dev);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "failed to clear AVE chroma swap\n");
	if (ret != sizeof(command))
		return dev_err_probe(dev, -EIO,
				     "short AVE chroma-swap write: %d\n", ret);

	dev_info(dev, "cleared AVE chroma-swap control\n");
	return 0;
}

static const struct drm_display_mode gcn_drm_mode = {
	.clock = 25175,
	.hdisplay = 640,
	.hsync_start = 656,
	.hsync_end = 752,
	.htotal = 800,
	.vdisplay = 480,
	.vsync_start = 490,
	.vsync_end = 492,
	.vtotal = 525,
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	.name = "640x480",
};

static u32 gcn_drm_pack_yuyv(unsigned int r0, unsigned int g0,
			     unsigned int b0, unsigned int r1,
			     unsigned int g1, unsigned int b1,
			     unsigned int luma_min,
			     unsigned int chroma_min)
{
	int cb;
	int cr;
	int r;
	int g;
	int b;
	int y0;
	int y1;

	y0 = clamp(((RGB2YUV_YR * r0 + RGB2YUV_YG * g0 +
		     RGB2YUV_YB * b0) >> RGB2YUV_SHIFT) + 16,
		   (int)luma_min, 235);
	y1 = clamp(((RGB2YUV_YR * r1 + RGB2YUV_YG * g1 +
		     RGB2YUV_YB * b1) >> RGB2YUV_SHIFT) + 16,
		   (int)luma_min, 235);
	r = (r0 + r1) / 2;
	g = (g0 + g1) / 2;
	b = (b0 + b1) / 2;
	cb = clamp(((RGB2YUV_UR * r + RGB2YUV_UG * g +
		      RGB2YUV_UB * b) >> RGB2YUV_SHIFT) + 128,
		    (int)chroma_min, 240);
	cr = clamp(((RGB2YUV_VR * r + RGB2YUV_VG * g +
		      RGB2YUV_VB * b) >> RGB2YUV_SHIFT) + 128,
		    (int)chroma_min, 240);

	return y0 << 24 | cr << 16 | y1 << 8 | cb;
}

static u32 gcn_drm_rgb565_pair(u16 pixel0, u16 pixel1)
{
	unsigned int r0;
	unsigned int g0;
	unsigned int b0;
	unsigned int r1;
	unsigned int g1;
	unsigned int b1;

	if (!(pixel0 | pixel1))
		return 0x00800080;

	r0 = (pixel0 >> 11) & 0x1f;
	g0 = (pixel0 >> 5) & 0x3f;
	b0 = pixel0 & 0x1f;
	r1 = (pixel1 >> 11) & 0x1f;
	g1 = (pixel1 >> 5) & 0x3f;
	b1 = pixel1 & 0x1f;

	r0 = (r0 << 3) | (r0 >> 2);
	g0 = (g0 << 2) | (g0 >> 4);
	b0 = (b0 << 3) | (b0 >> 2);
	r1 = (r1 << 3) | (r1 >> 2);
	g1 = (g1 << 2) | (g1 >> 4);
	b1 = (b1 << 3) | (b1 >> 2);

	return gcn_drm_pack_yuyv(r0, g0, b0, r1, g1, b1,
				 RGB2YUV_LUMA_565, RGB2YUV_CHROMA_565);
}

static u32 gcn_drm_xrgb8888_pair(u32 pixel0, u32 pixel1)
{
	if (!(pixel0 | pixel1))
		return 0x00800080;

	return gcn_drm_pack_yuyv((pixel0 >> 16) & 0xff,
				 (pixel0 >> 8) & 0xff, pixel0 & 0xff,
				 (pixel1 >> 16) & 0xff,
				 (pixel1 >> 8) & 0xff, pixel1 & 0xff,
				 RGB2YUV_LUMA_888, RGB2YUV_CHROMA_888);
}

static int gcn_drm_convert(struct gcn_drm *gcn,
			   struct drm_plane_state *state,
			   unsigned int page)
{
	struct drm_shadow_plane_state *shadow =
		to_drm_shadow_plane_state(state);
	struct drm_framebuffer *fb = state->fb;
	u32 *dst = gcn->xfb + page * GCN_DRM_XFB_PAGE_SIZE;
	unsigned int x;
	unsigned int y;
	int ret;

	if (!fb || !shadow->data[0].vaddr)
		return -EINVAL;

	ret = drm_gem_fb_begin_cpu_access(fb, DMA_FROM_DEVICE);
	if (ret)
		return ret;

	for (y = 0; y < GCN_DRM_HEIGHT; y++) {
		const u8 *src = shadow->data[0].vaddr + y * fb->pitches[0];

		switch (fb->format->format) {
		case DRM_FORMAT_RGB565: {
			const u16 *pixels = (const u16 *)src;

			for (x = 0; x < GCN_DRM_WIDTH; x += 2)
				*dst++ = gcn_drm_rgb565_pair(pixels[x],
							     pixels[x + 1]);
			break;
		}
		case DRM_FORMAT_XRGB8888: {
			const u32 *pixels = (const u32 *)src;

			for (x = 0; x < GCN_DRM_WIDTH; x += 2)
				*dst++ = gcn_drm_xrgb8888_pair(pixels[x],
							       pixels[x + 1]);
			break;
		}
		default:
			ret = -EINVAL;
			goto out_end_access;
		}
	}

	flush_dcache_range((unsigned long)gcn->xfb +
			   page * GCN_DRM_XFB_PAGE_SIZE,
			   (unsigned long)gcn->xfb +
			   (page + 1) * GCN_DRM_XFB_PAGE_SIZE);
out_end_access:
	drm_gem_fb_end_cpu_access(fb, DMA_FROM_DEVICE);
	return ret;
}

static void gcn_drm_set_scanout(struct gcn_drm *gcn, unsigned int page)
{
	u32 bottom;
	u32 top = gcn->xfb_phys + page * GCN_DRM_XFB_PAGE_SIZE;
	u32 xof = (top / 2) & 0xf;

	bottom = top;
	if (!(in_be16(gcn->vi_base + VI_DCR) & VI_DCR_NIN))
		bottom += GCN_DRM_XFB_PITCH;

	out_be32(gcn->vi_base + VI_TFBL,
		 VI_FB_POB | xof << VI_FB_XOF_SHIFT | top >> 5);
	out_be32(gcn->vi_base + VI_BFBL, VI_FB_POB | bottom >> 5);
}

static void gcn_drm_quiesce_irqs(struct gcn_drm *gcn)
{
	static const u8 di_regs[] = { VI_DI0, VI_DI1, VI_DI2, VI_DI3 };
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(di_regs); i++) {
		u32 value = in_be32(gcn->vi_base + di_regs[i]);

		out_be32(gcn->vi_base + di_regs[i],
			 value & ~(VI_DI_IRQ | VI_DI_ENABLE));
	}
}

static void gcn_drm_program_ntsc_480i(struct gcn_drm *gcn)
{
	void __iomem *vi = gcn->vi_base;

	gcn_drm_quiesce_irqs(gcn);
	out_be16(vi + VI_DCR, 0);

	out_be16(vi + VI_VTR, VI_NTSC_VTR);
	out_be32(vi + VI_HTR0, VI_NTSC_HTR0);
	out_be32(vi + VI_HTR1, VI_NTSC_HTR1);
	out_be32(vi + VI_VTO, VI_NTSC_VTO);
	out_be32(vi + VI_VTE, VI_NTSC_VTE);
	out_be32(vi + VI_BBOI, VI_NTSC_BBOI);
	out_be32(vi + VI_BBEI, VI_NTSC_BBEI);
	out_be32(vi + VI_TFBR, 0);
	out_be32(vi + VI_BFBR, 0);
	out_be16(vi + VI_PCR, VI_NTSC_PCR);
	out_be16(vi + VI_HSR, VI_NTSC_HSR);
	out_be32(vi + VI_FCT0, gcn_drm_vi_filter[0]);
	out_be32(vi + VI_FCT1, gcn_drm_vi_filter[1]);
	out_be32(vi + VI_FCT2, gcn_drm_vi_filter[2]);
	out_be32(vi + VI_FCT3, gcn_drm_vi_filter[3]);
	out_be32(vi + VI_FCT4, gcn_drm_vi_filter[4]);
	out_be32(vi + VI_FCT5, gcn_drm_vi_filter[5]);
	out_be32(vi + VI_FCT6, gcn_drm_vi_filter[6]);
	out_be32(vi + VI_AA, 0x00ff0000);
	out_be16(vi + VI_CLK, 0);
	out_be16(vi + VI_HSW, GCN_DRM_WIDTH);
	out_be16(vi + VI_HBE, 0);
	out_be16(vi + VI_HBS, 0);
	out_be16(vi + VI_UNK1, 0x00ff);
	out_be32(vi + VI_UNK2, 0x00ff00ff);
	out_be32(vi + VI_UNK3, 0x00ff00ff);

	memset32(gcn->xfb, 0x10801080,
		 2 * GCN_DRM_XFB_PAGE_SIZE / sizeof(u32));
	flush_dcache_range((unsigned long)gcn->xfb,
			   (unsigned long)gcn->xfb +
			   2 * GCN_DRM_XFB_PAGE_SIZE);
	gcn_drm_set_scanout(gcn, 0);
	out_be32(vi + VI_DI0, VI_NTSC_DI0);
	out_be32(vi + VI_DI1, VI_NTSC_DI1);
	out_be32(vi + VI_DI2, 0);
	out_be32(vi + VI_DI3, 0);
	out_be16(vi + VI_DCR, VI_DCR_ENABLE);

	drm_info(&gcn->drm,
		 "programmed NTSC 480i: DCR=%04x VTR=%04x HTR0=%08x HTR1=%08x PCR=%04x\n",
		 in_be16(vi + VI_DCR), in_be16(vi + VI_VTR),
		 in_be32(vi + VI_HTR0), in_be32(vi + VI_HTR1),
		 in_be16(vi + VI_PCR));
}

static void gcn_drm_arm_event(struct gcn_drm *gcn)
{
	struct drm_crtc *crtc = &gcn->pipe.crtc;
	struct drm_pending_vblank_event *event;
	unsigned long flags;

	spin_lock_irqsave(&crtc->dev->event_lock, flags);
	event = crtc->state->event;
	if (event) {
		crtc->state->event = NULL;
		if (!drm_crtc_vblank_get(crtc))
			drm_crtc_arm_vblank_event(crtc, event);
		else
			drm_crtc_send_vblank_event(crtc, event);
	}
	spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
}

static void gcn_drm_submit(struct gcn_drm *gcn,
			   struct drm_plane_state *state,
			   bool immediate)
{
	unsigned long flags;
	unsigned int page;
	int ret;

	spin_lock_irqsave(&gcn->scanout_lock, flags);
	page = gcn->visible_page ^ 1;
	spin_unlock_irqrestore(&gcn->scanout_lock, flags);

	ret = gcn_drm_convert(gcn, state, page);
	if (ret) {
		drm_err(&gcn->drm, "framebuffer conversion failed: %d\n", ret);
		return;
	}

	spin_lock_irqsave(&gcn->scanout_lock, flags);
	if (immediate) {
		gcn_drm_set_scanout(gcn, page);
		gcn->visible_page = page;
		gcn->flip_pending = false;
	} else {
		gcn->pending_page = page;
		gcn->flip_pending = true;
	}
	spin_unlock_irqrestore(&gcn->scanout_lock, flags);
}

static void gcn_drm_pipe_enable(struct drm_simple_display_pipe *pipe,
				struct drm_crtc_state *crtc_state,
				struct drm_plane_state *plane_state)
{
	struct gcn_drm *gcn = to_gcn_drm(pipe->crtc.dev);

	gcn_drm_submit(gcn, plane_state, true);
	drm_crtc_vblank_on(&pipe->crtc);
}

static void gcn_drm_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	struct gcn_drm *gcn = to_gcn_drm(pipe->crtc.dev);
	unsigned long flags;

	drm_crtc_vblank_off(&pipe->crtc);
	spin_lock_irqsave(&gcn->scanout_lock, flags);
	gcn->flip_pending = false;
	spin_unlock_irqrestore(&gcn->scanout_lock, flags);
}

static void gcn_drm_pipe_update(struct drm_simple_display_pipe *pipe,
				struct drm_plane_state *old_state)
{
	struct gcn_drm *gcn = to_gcn_drm(pipe->crtc.dev);

	if (pipe->plane.state->fb)
		gcn_drm_submit(gcn, pipe->plane.state, false);
	gcn_drm_arm_event(gcn);
}

static int gcn_drm_enable_vblank(struct drm_simple_display_pipe *pipe)
{
	struct gcn_drm *gcn = to_gcn_drm(pipe->crtc.dev);
	u32 value = in_be32(gcn->vi_base + VI_DI1);

	out_be32(gcn->vi_base + VI_DI1,
		 (value & ~VI_DI_IRQ) | VI_DI_ENABLE);
	return 0;
}

static void gcn_drm_disable_vblank(struct drm_simple_display_pipe *pipe)
{
	struct gcn_drm *gcn = to_gcn_drm(pipe->crtc.dev);
	u32 value = in_be32(gcn->vi_base + VI_DI1);

	out_be32(gcn->vi_base + VI_DI1,
		 value & ~(VI_DI_IRQ | VI_DI_ENABLE));
}

static const struct drm_simple_display_pipe_funcs gcn_drm_pipe_funcs = {
	.enable = gcn_drm_pipe_enable,
	.disable = gcn_drm_pipe_disable,
	.update = gcn_drm_pipe_update,
	.enable_vblank = gcn_drm_enable_vblank,
	.disable_vblank = gcn_drm_disable_vblank,
	DRM_GEM_SIMPLE_DISPLAY_PIPE_SHADOW_PLANE_FUNCS,
};

static const u32 gcn_drm_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGB565,
};

static int gcn_drm_connector_get_modes(struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &gcn_drm_mode);
	if (!mode)
		return 0;
	drm_mode_probed_add(connector, mode);
	return 1;
}

static const struct drm_connector_helper_funcs gcn_drm_connector_helper_funcs = {
	.get_modes = gcn_drm_connector_get_modes,
};

static enum drm_connector_status
gcn_drm_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static const struct drm_connector_funcs gcn_drm_connector_funcs = {
	.detect = gcn_drm_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_mode_config_funcs gcn_drm_mode_config_funcs = {
	.fb_create = drm_gem_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static irqreturn_t gcn_drm_irq(int irq, void *data)
{
	struct gcn_drm *gcn = data;
	unsigned long flags;
	u32 value;
	bool handled = false;

	value = in_be32(gcn->vi_base + VI_DI0);
	if (value & VI_DI_IRQ) {
		out_be32(gcn->vi_base + VI_DI0, value & ~VI_DI_IRQ);
		handled = true;
	}

	value = in_be32(gcn->vi_base + VI_DI1);
	if (value & VI_DI_IRQ) {
		spin_lock_irqsave(&gcn->scanout_lock, flags);
		if (gcn->flip_pending) {
			gcn_drm_set_scanout(gcn, gcn->pending_page);
			gcn->visible_page = gcn->pending_page;
			gcn->flip_pending = false;
		}
		spin_unlock_irqrestore(&gcn->scanout_lock, flags);
		out_be32(gcn->vi_base + VI_DI1, value & ~VI_DI_IRQ);
		drm_crtc_handle_vblank(&gcn->pipe.crtc);
		handled = true;
	}

	return IRQ_RETVAL(handled);
}

DEFINE_DRM_GEM_FOPS(gcn_drm_fops);

static const struct drm_driver gcn_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
	.fops = &gcn_drm_fops,
	DRM_GEM_SHMEM_DRIVER_OPS,
	.name = GCN_DRM_NAME,
	.desc = GCN_DRM_DESC,
	.major = 1,
	.minor = 0,
};

static int gcn_drm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gcn_drm *gcn;
	u32 xfb_phys;
	u32 xfb_size;
	int irq;
	int ret;

	gcn = devm_drm_dev_alloc(dev, &gcn_drm_driver,
				 struct gcn_drm, drm);
	if (IS_ERR(gcn))
		return PTR_ERR(gcn);
	dev_info(dev, "probe: DRM device allocated\n");

	gcn->vi_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(gcn->vi_base))
		return PTR_ERR(gcn->vi_base);

	ret = of_property_read_u32(dev->of_node, "xfb-start", &xfb_phys);
	if (ret)
		return dev_err_probe(dev, ret, "missing xfb-start\n");
	ret = of_property_read_u32(dev->of_node, "xfb-size", &xfb_size);
	if (ret)
		return dev_err_probe(dev, ret, "missing xfb-size\n");
	if (xfb_size < 2 * GCN_DRM_XFB_PAGE_SIZE)
		return dev_err_probe(dev, -ENOMEM,
				     "XFB reservation is too small\n");

	gcn->xfb = devm_memremap(dev, xfb_phys, xfb_size, MEMREMAP_WB);
	if (IS_ERR(gcn->xfb))
		return PTR_ERR(gcn->xfb);
	gcn->xfb_phys = xfb_phys;
	gcn->xfb_size = xfb_size;
	spin_lock_init(&gcn->scanout_lock);
	dev_info(dev, "probe: VI and XFB mapped\n");

	ret = gcn_drm_clear_ave_chroma_swap(dev);
	if (ret)
		return ret;

	if (program_mode)
		gcn_drm_program_ntsc_480i(gcn);

	if (!(in_be16(gcn->vi_base + VI_DCR) & VI_DCR_ENABLE))
		return dev_err_probe(dev, -ENODEV,
				     "VI has no active handoff mode\n");

	ret = drmm_mode_config_init(&gcn->drm);
	if (ret)
		return ret;
	gcn->drm.mode_config.min_width = GCN_DRM_WIDTH;
	gcn->drm.mode_config.max_width = GCN_DRM_WIDTH;
	gcn->drm.mode_config.min_height = GCN_DRM_HEIGHT;
	gcn->drm.mode_config.max_height = GCN_DRM_HEIGHT;
	gcn->drm.mode_config.funcs = &gcn_drm_mode_config_funcs;

	drm_connector_helper_add(&gcn->connector,
				 &gcn_drm_connector_helper_funcs);
	ret = drm_connector_init(&gcn->drm, &gcn->connector,
				 &gcn_drm_connector_funcs,
				 DRM_MODE_CONNECTOR_Composite);
	if (ret)
		return ret;

	ret = drm_simple_display_pipe_init(&gcn->drm, &gcn->pipe,
					   &gcn_drm_pipe_funcs,
					   gcn_drm_formats,
					   ARRAY_SIZE(gcn_drm_formats),
					   NULL, &gcn->connector);
	if (ret)
		return ret;
	dev_info(dev, "probe: mode objects initialized\n");

	ret = drm_vblank_init(&gcn->drm, 1);
	if (ret)
		return ret;
	dev_info(dev, "probe: vblank state initialized\n");

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;
	gcn_drm_quiesce_irqs(gcn);
	dev_info(dev, "probe: VI interrupt sources quiesced\n");
	ret = devm_request_irq(dev, irq, gcn_drm_irq, 0, GCN_DRM_NAME, gcn);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request VI IRQ\n");
	dev_info(dev, "probe: VI IRQ installed\n");

	drm_mode_config_reset(&gcn->drm);
	platform_set_drvdata(pdev, gcn);

	ret = drm_dev_register(&gcn->drm, 0);
	if (ret)
		return ret;

	drm_info(&gcn->drm,
		 "bound fixed 640x480 %s mode, XFB %08x+%08x\n",
		 program_mode ? "programmed NTSC 480i" : "handoff",
		 xfb_phys, xfb_size);
	return 0;
}

static void gcn_drm_remove(struct platform_device *pdev)
{
	struct gcn_drm *gcn = platform_get_drvdata(pdev);

	drm_dev_unplug(&gcn->drm);
	drm_atomic_helper_shutdown(&gcn->drm);
	out_be32(gcn->vi_base + VI_DI0, 0);
	out_be32(gcn->vi_base + VI_DI1, 0);
	out_be32(gcn->vi_base + VI_DI2, 0);
	out_be32(gcn->vi_base + VI_DI3, 0);
}

static void gcn_drm_shutdown(struct platform_device *pdev)
{
	struct gcn_drm *gcn = platform_get_drvdata(pdev);

	drm_atomic_helper_shutdown(&gcn->drm);
}

static const struct of_device_id gcn_drm_of_match[] = {
	{ .compatible = "nintendo,hollywood-vi" },
	{ .compatible = "nintendo,flipper-vi" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcn_drm_of_match);

static struct platform_driver gcn_drm_platform_driver = {
	.probe = gcn_drm_probe,
	.remove = gcn_drm_remove,
	.shutdown = gcn_drm_shutdown,
	.driver = {
		.name = GCN_DRM_NAME,
		.of_match_table = gcn_drm_of_match,
	},
};
module_platform_driver(gcn_drm_platform_driver);

MODULE_AUTHOR("Bill Carson <anolisporcatus@gmail.com>");
MODULE_DESCRIPTION(GCN_DRM_DESC);
MODULE_LICENSE("GPL");
