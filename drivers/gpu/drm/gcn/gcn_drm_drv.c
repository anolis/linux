// SPDX-License-Identifier: GPL-2.0-only
/*
 * Nintendo GameCube/Wii Video Interface DRM/KMS driver
 * Copyright (C) 2026 Bill Carson
 */

#include <linux/delay.h>
#include <linux/gcn_drm_accel.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <asm/cacheflush.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_shmem.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>

#include "gcn_drm_trace.h"
#include "gcn_drm_internal.h"

#define GCN_DRM_NAME		"gcn-vi"
#define GCN_DRM_DESC		"Nintendo GameCube/Wii VI DRM"

#define GCN_DRM_WIDTH		640
#define GCN_DRM_HEIGHT		480
#define GCN_DRM_XFB_PITCH	(GCN_DRM_WIDTH * 2)
#define GCN_DRM_XFB_PAGE_SIZE	(GCN_DRM_XFB_PITCH * GCN_DRM_HEIGHT)

#define AVE_CHROMA_EXCHANGE_REG	0x62
#define AVE_CHROMA_EXCHANGE_OFF	0x00
#define AVE_CHROMA_EXCHANGE_ON	0x02

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
#define VI_DCR_RESET		BIT(1)
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

/* libogc TVNtsc480Prog with the standard centered 640x480 viewport. */
#define VI_NTSC_480P_VTR	0x1e0c
#define VI_NTSC_480P_HTR0	0x476901ad
#define VI_NTSC_480P_HTR1	0x02ea5140
#define VI_NTSC_480P_VTO	0x00060030
#define VI_NTSC_480P_VTE	0x00060030
#define VI_NTSC_480P_BBOI	0x81d881d8
#define VI_NTSC_480P_BBEI	0x81d881d8
#define VI_NTSC_480P_PCR	0x2828
#define VI_NTSC_480P_DI0	0x00000000
#define VI_NTSC_480P_DI1	0x020e0001

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
	struct i2c_client *ave;
	void __iomem *vi_base;
	void *xfb;
	u32 xfb_phys;
	u32 xfb_size;
	/* Protects page selection and pending scanout state. */
	spinlock_t scanout_lock;
	unsigned int visible_page;
	unsigned int pending_page;
	bool flip_pending;
	bool ave_restore_needed;
	bool ave_chroma_write_only;
	u64 gx_rgb565_frames;
	u64 gx_xrgb8888_frames;
};

static bool program_mode = true;
module_param(program_mode, bool, 0444);
MODULE_PARM_DESC(program_mode,
		 "program fixed 640x480 NTSC interlaced VI timing (default: true)");

static bool progressive;
module_param(progressive, bool, 0444);
MODULE_PARM_DESC(progressive,
		 "program 640x480 NTSC progressive timing (requires program_mode=1)");

static const u32 gcn_drm_vi_filter[] = {
	0x1ae771f0, 0x0db4a574, 0x00c1188e, 0xc4c0cbe2,
	0xfcecdecf, 0x13130f08, 0x00080c0f,
};

static atomic_t gcn_drm_vi_write_sequence = ATOMIC_INIT(0);
static DEFINE_MUTEX(gcn_drm_accel_lock);
static const struct gcn_drm_accel_ops *gcn_drm_accel;
static struct gcn_drm *gcn_drm_active;

static int gcn_drm_ave_write_verify(struct gcn_drm *gcn, u8 value);

static int gcn_drm_set_accel_ave_locked(bool accel_active)
{
	struct gcn_drm *gcn = gcn_drm_active;
	u8 value = accel_active ? AVE_CHROMA_EXCHANGE_OFF :
				 AVE_CHROMA_EXCHANGE_ON;
	int ret;

	if (!gcn || !gcn->ave)
		return 0;

	ret = gcn_drm_ave_write_verify(gcn, value);
	if (ret)
		return ret;

	drm_info(&gcn->drm, "set AVE chroma exchange for %s scanout: 62=%02x\n",
		 accel_active ? "GX" : "CPU", value);
	return 0;
}

int gcn_drm_register_accel_v2(const struct gcn_drm_accel_ops *ops)
{
	int ret = 0;

	if (!ops || !ops->name || !ops->owner ||
	    (!ops->blit_rgb565 && !ops->blit_xrgb8888) ||
	    !ops->mem1_info || !ops->mem1_alloc || !ops->mem1_free ||
	    !ops->mem1_mmap)
		return -EINVAL;

	mutex_lock(&gcn_drm_accel_lock);
	if (gcn_drm_accel) {
		ret = -EBUSY;
	} else {
		ret = gcn_drm_set_accel_ave_locked(true);
		if (ret)
			goto out_unlock;
		gcn_drm_accel = ops;
	}
out_unlock:
	mutex_unlock(&gcn_drm_accel_lock);

	if (!ret)
		pr_info("gcn-drm: registered scanout accelerator %s\n",
			ops->name);
	return ret;
}
EXPORT_SYMBOL_GPL(gcn_drm_register_accel_v2);

void gcn_drm_unregister_accel_v2(const struct gcn_drm_accel_ops *ops)
{
	int ret = 0;

	mutex_lock(&gcn_drm_accel_lock);
	if (gcn_drm_accel == ops) {
		gcn_drm_accel = NULL;
		ret = gcn_drm_set_accel_ave_locked(false);
	}
	mutex_unlock(&gcn_drm_accel_lock);

	if (ret)
		pr_err("gcn-drm: failed to select CPU AVE chroma order: %d\n",
		       ret);
	pr_info("gcn-drm: unregistered scanout accelerator %s\n",
		ops ? ops->name : "unknown");
}
EXPORT_SYMBOL_GPL(gcn_drm_unregister_accel_v2);

int gcn_drm_provider_info(struct gcn_drm_mem1_info *info)
{
	int ret = -ENODEV;

	if (!info)
		return -EINVAL;

	mutex_lock(&gcn_drm_accel_lock);
	if (gcn_drm_accel && gcn_drm_accel->mem1_info)
		ret = gcn_drm_accel->mem1_info(info);
	mutex_unlock(&gcn_drm_accel_lock);
	return ret;
}

int gcn_drm_provider_alloc(size_t size,
			   const struct gcn_drm_accel_ops **provider,
			   void **allocation)
{
	const struct gcn_drm_accel_ops *ops;
	int ret = -ENODEV;

	if (!provider || !allocation)
		return -EINVAL;
	*provider = NULL;
	*allocation = NULL;

	mutex_lock(&gcn_drm_accel_lock);
	ops = gcn_drm_accel;
	if (!ops || !ops->mem1_alloc || !try_module_get(ops->owner))
		goto out_unlock;

	ret = ops->mem1_alloc(size, allocation);
	if (ret) {
		module_put(ops->owner);
		goto out_unlock;
	}
	*provider = ops;
out_unlock:
	mutex_unlock(&gcn_drm_accel_lock);
	return ret;
}

void gcn_drm_provider_free(const struct gcn_drm_accel_ops *provider,
			   void *allocation)
{
	if (!provider)
		return;

	provider->mem1_free(allocation);
	module_put(provider->owner);
}

int gcn_drm_provider_mmap(const struct gcn_drm_accel_ops *provider,
			  void *allocation, struct vm_area_struct *vma)
{
	if (!provider || !provider->mem1_mmap)
		return -ENODEV;

	return provider->mem1_mmap(allocation, vma);
}

int gcn_drm_provider_copy(const struct gcn_drm_accel_ops *provider,
			  void *src_allocation, void *dst_allocation,
			  u16 width, u16 height)
{
	int ret = -ENODEV;

	mutex_lock(&gcn_drm_accel_lock);
	if (gcn_drm_accel == provider && provider->submit_rgb565)
		ret = provider->submit_rgb565(src_allocation, dst_allocation,
					      width, height);
	mutex_unlock(&gcn_drm_accel_lock);

	return ret;
}

int gcn_drm_provider_fill(const struct gcn_drm_accel_ops *provider,
			  void *dst_allocation, u16 width, u16 height,
			  u16 color)
{
	int ret = -ENODEV;

	mutex_lock(&gcn_drm_accel_lock);
	if (gcn_drm_accel == provider && provider->fill_rgb565)
		ret = provider->fill_rgb565(dst_allocation, width, height, color);
	mutex_unlock(&gcn_drm_accel_lock);

	return ret;
}

int gcn_drm_provider_fill_rect(const struct gcn_drm_accel_ops *provider,
			       void *dst_allocation, u16 width, u16 height,
			       u16 x, u16 y, u16 rect_width,
			       u16 rect_height, u16 color)
{
	int ret = -ENODEV;

	mutex_lock(&gcn_drm_accel_lock);
	if (gcn_drm_accel == provider && provider->fill_rect_rgb565)
		ret = provider->fill_rect_rgb565(dst_allocation, width, height,
						 x, y, rect_width, rect_height,
						 color);
	mutex_unlock(&gcn_drm_accel_lock);

	return ret;
}

int gcn_drm_provider_draw_triangle(const struct gcn_drm_accel_ops *provider,
				   void *dst_allocation,
				   u16 width, u16 height,
				   const struct gcn_drm_color_vertex vertices[3])
{
	int ret = -ENODEV;

	mutex_lock(&gcn_drm_accel_lock);
	if (gcn_drm_accel == provider && provider->draw_triangle_rgb565)
		ret = provider->draw_triangle_rgb565(dst_allocation, width,
						      height, vertices);
	mutex_unlock(&gcn_drm_accel_lock);

	return ret;
}

int gcn_drm_provider_draw_triangles(const struct gcn_drm_accel_ops *provider,
				    void *dst_allocation,
				    u16 width, u16 height,
				    const struct gcn_drm_color_vertex *vertices,
				    u32 triangle_count)
{
	int ret = -ENODEV;

	mutex_lock(&gcn_drm_accel_lock);
	if (gcn_drm_accel == provider && provider->draw_triangles_rgb565)
		ret = provider->draw_triangles_rgb565(dst_allocation, width,
						       height, vertices,
						       triangle_count);
	mutex_unlock(&gcn_drm_accel_lock);

	return ret;
}

int
gcn_drm_provider_draw_triangles_state(const struct gcn_drm_accel_ops *provider,
				      void *dst_allocation, u16 width, u16 height,
				      const struct gcn_drm_color_vertex *vertices,
				      u32 triangle_count,
				      const struct gcn_drm_draw_state *state)
{
	int ret = -ENODEV;

	mutex_lock(&gcn_drm_accel_lock);
	if (gcn_drm_accel == provider && provider->draw_triangles_state_rgb565)
		ret = provider->draw_triangles_state_rgb565(dst_allocation, width,
				height, vertices, triangle_count, state);
	mutex_unlock(&gcn_drm_accel_lock);

	return ret;
}

int gcn_drm_provider_blit_rect(const struct gcn_drm_accel_ops *provider,
			       void *src_allocation, void *dst_allocation,
			       u16 src_width, u16 src_height, u16 dst_width,
			       u16 dst_height, u16 src_x, u16 src_y, u16 dst_x,
			       u16 dst_y, u16 rect_width, u16 rect_height)
{
	int ret = -ENODEV;

	mutex_lock(&gcn_drm_accel_lock);
	if (gcn_drm_accel == provider && provider->blit_rect_rgb565)
		ret = provider->blit_rect_rgb565(src_allocation, dst_allocation,
						 src_width, src_height,
						 dst_width, dst_height, src_x,
						 src_y, dst_x, dst_y, rect_width,
						 rect_height);
	mutex_unlock(&gcn_drm_accel_lock);

	return ret;
}

int gcn_drm_provider_blit_scaled(const struct gcn_drm_accel_ops *provider,
				 void *src_allocation, void *dst_allocation,
				 u16 src_width, u16 src_height, u16 dst_width,
				 u16 dst_height,
				 const struct drm_gcn_blit_scaled *args)
{
	int ret = -ENODEV;

	mutex_lock(&gcn_drm_accel_lock);
	if (gcn_drm_accel == provider && provider->blit_scaled_rgb565)
		ret = provider->blit_scaled_rgb565(src_allocation, dst_allocation,
						 src_width, src_height,
						 dst_width, dst_height,
						 args->src_x, args->src_y,
						 args->src_width,
						 args->src_height,
						 args->dst_x, args->dst_y,
						 args->dst_width,
						 args->dst_height);
	mutex_unlock(&gcn_drm_accel_lock);

	return ret;
}

int gcn_drm_provider_blit_scaled_system(const void *src, void *dst,
					u16 src_width, u16 src_height,
					u16 dst_width, u16 dst_height,
					u32 src_format, u32 dst_format,
					u32 src_layout, u32 dst_layout,
					const struct drm_gcn_blit_scaled *args)
{
	const struct gcn_drm_accel_ops *provider;
	int ret = -ENODEV;

	mutex_lock(&gcn_drm_accel_lock);
	provider = gcn_drm_accel;
	if (provider && src_format == DRM_GCN_GEM_FORMAT_RGB565 &&
	    dst_format == DRM_GCN_GEM_FORMAT_RGB565 &&
	    provider->blit_scaled_system_rgb565 &&
	    try_module_get(provider->owner)) {
		ret = provider->blit_scaled_system_rgb565(src, dst,
				src_width, src_height, dst_width, dst_height,
				src_layout, dst_layout,
				args->src_x, args->src_y, args->src_width,
				args->src_height, args->dst_x, args->dst_y,
				args->dst_width, args->dst_height);
		module_put(provider->owner);
	} else if (provider &&
		   src_format == DRM_GCN_GEM_FORMAT_XRGB8888 &&
		   dst_format == DRM_GCN_GEM_FORMAT_RGB565 &&
		   src_layout == DRM_GCN_GEM_LAYOUT_LINEAR &&
		   provider->blit_scaled_system_xrgb8888 &&
		   try_module_get(provider->owner)) {
		ret = provider->blit_scaled_system_xrgb8888(src, dst,
				src_width, src_height, dst_width, dst_height,
				dst_layout, args->src_x, args->src_y,
				args->src_width, args->src_height, args->dst_x,
				args->dst_y, args->dst_width, args->dst_height);
		module_put(provider->owner);
	}
	mutex_unlock(&gcn_drm_accel_lock);

	return ret;
}

static int gcn_drm_accel_rgb565(const void *src, u32 src_pitch,
				u32 xfb_phys, u16 width, u16 height)
{
	int ret = -ENODEV;

	/* Unregister holds this mutex until every in-flight call returns. */
	mutex_lock(&gcn_drm_accel_lock);
	if (gcn_drm_accel && gcn_drm_accel->blit_rgb565)
		ret = gcn_drm_accel->blit_rgb565(src, src_pitch, xfb_phys,
						 width, height);
	mutex_unlock(&gcn_drm_accel_lock);

	return ret;
}

static int gcn_drm_accel_xrgb8888(const void *src, u32 src_pitch,
				  u32 xfb_phys, u16 width, u16 height)
{
	int ret = -ENODEV;

	/* Unregister holds this mutex until every in-flight call returns. */
	mutex_lock(&gcn_drm_accel_lock);
	if (gcn_drm_accel && gcn_drm_accel->blit_xrgb8888)
		ret = gcn_drm_accel->blit_xrgb8888(src, src_pitch, xfb_phys,
						   width, height);
	mutex_unlock(&gcn_drm_accel_lock);

	return ret;
}

static __always_inline void gcn_drm_vi_write16(struct gcn_drm *gcn,
					       u8 offset, u16 value)
{
	if (trace_gcn_vi_write_enabled()) {
		unsigned int sequence;

		sequence = atomic_inc_return(&gcn_drm_vi_write_sequence);
		trace_gcn_vi_write(sequence, 16, offset, value, _RET_IP_);
	}
	out_be16(gcn->vi_base + offset, value);
}

static __always_inline void gcn_drm_vi_write32(struct gcn_drm *gcn,
					       u8 offset, u32 value)
{
	if (trace_gcn_vi_write_enabled()) {
		unsigned int sequence;

		sequence = atomic_inc_return(&gcn_drm_vi_write_sequence);
		trace_gcn_vi_write(sequence, 32, offset, value, _RET_IP_);
	}
	out_be32(gcn->vi_base + offset, value);
}

static inline struct gcn_drm *to_gcn_drm(struct drm_device *drm)
{
	return container_of(drm, struct gcn_drm, drm);
}

static int gcn_drm_ave_read(struct gcn_drm *gcn, u8 reg, u8 *value)
{
	struct i2c_msg messages[2];
	int ret;

	messages[0].addr = gcn->ave->addr;
	messages[0].flags = gcn->ave->flags;
	messages[0].len = sizeof(reg);
	messages[0].buf = &reg;
	messages[1].addr = gcn->ave->addr;
	messages[1].flags = gcn->ave->flags | I2C_M_RD;
	messages[1].len = sizeof(*value);
	messages[1].buf = value;

	ret = i2c_transfer(gcn->ave->adapter, messages, ARRAY_SIZE(messages));
	if (ret < 0)
		return ret;
	if (ret != ARRAY_SIZE(messages))
		return -EIO;

	return 0;
}

static int gcn_drm_ave_write_verify(struct gcn_drm *gcn, u8 value)
{
	u8 command[] = { AVE_CHROMA_EXCHANGE_REG, value };
	u8 control;
	u8 readback;
	int ret;

	ret = i2c_master_send(gcn->ave, command, sizeof(command));
	if (ret < 0)
		return ret;
	if (ret != sizeof(command))
		return -EIO;

	ret = gcn_drm_ave_read(gcn, AVE_CHROMA_EXCHANGE_REG, &readback);
	if (ret)
		return ret;
	if (readback == value)
		return 0;
	if (readback != 0xff)
		return -EIO;

	/* Some AVE revisions return 0xff for command-style registers. */
	ret = gcn_drm_ave_read(gcn, 0x01, &control);
	if (ret)
		return ret;
	if (control == 0xff)
		return -EIO;
	if (!gcn->ave_chroma_write_only)
		drm_info(&gcn->drm,
			 "AVE chroma register has write-only readback; control=%02x\n",
			 control);
	gcn->ave_chroma_write_only = true;

	return 0;
}

static int gcn_drm_restore_ave(struct gcn_drm *gcn)
{
	int ret;

	if (!gcn->ave || !gcn->ave_restore_needed)
		return 0;

	ret = gcn_drm_ave_write_verify(gcn, AVE_CHROMA_EXCHANGE_OFF);
	if (ret) {
		drm_err(&gcn->drm,
			"failed to restore AVE chroma exchange: %d\n", ret);
		return ret;
	}

	gcn->ave_restore_needed = false;
	drm_info(&gcn->drm, "restored AVE chroma exchange: 62=00\n");
	return 0;
}

static void gcn_drm_release_ave(void *data)
{
	struct gcn_drm *gcn = data;

	mutex_lock(&gcn_drm_accel_lock);
	if (gcn_drm_active == gcn)
		gcn_drm_active = NULL;
	mutex_unlock(&gcn_drm_accel_lock);
	gcn_drm_restore_ave(gcn);
	put_device(&gcn->ave->dev);
	gcn->ave = NULL;
}

static int gcn_drm_acquire_ave(struct gcn_drm *gcn, struct device *dev)
{
	struct device_node *ave_node;
	int ret;

	ave_node = of_parse_phandle(dev->of_node, "audio-video-encoder", 0);
	if (!ave_node) {
		if (of_device_is_compatible(dev->of_node,
					    "nintendo,hollywood-vi"))
			return dev_err_probe(dev, -ENODEV,
					     "missing audio-video-encoder\n");
		return 0;
	}

	gcn->ave = of_find_i2c_device_by_node(ave_node);
	of_node_put(ave_node);
	if (!gcn->ave)
		return dev_err_probe(dev, -EPROBE_DEFER,
				     "AVE I2C client is not ready\n");

	ret = devm_add_action_or_reset(dev, gcn_drm_release_ave, gcn);
	if (ret)
		return ret;

	return 0;
}

static int gcn_drm_enable_ave(struct gcn_drm *gcn)
{
	int ret;

	if (!gcn->ave)
		return 0;

	/* Cleanup must restore zero even if this transfer fails partway. */
	gcn->ave_restore_needed = true;
	ret = gcn_drm_ave_write_verify(gcn, AVE_CHROMA_EXCHANGE_ON);
	if (ret)
		return ret;

	drm_info(&gcn->drm, "enabled AVE chroma exchange: 62=02\n");
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

	switch (fb->format->format) {
	case DRM_FORMAT_RGB565:
		ret = gcn_drm_accel_rgb565(shadow->data[0].vaddr,
					   fb->pitches[0],
					   gcn->xfb_phys +
					   page * GCN_DRM_XFB_PAGE_SIZE,
					   GCN_DRM_WIDTH, GCN_DRM_HEIGHT);
		if (!ret) {
			if (!gcn->gx_rgb565_frames++)
				drm_info(&gcn->drm,
					 "GX RGB565 scanout accelerator active\n");
			goto out_end_access;
		}
		break;
	case DRM_FORMAT_XRGB8888:
		ret = gcn_drm_accel_xrgb8888(shadow->data[0].vaddr,
					     fb->pitches[0],
					     gcn->xfb_phys +
					     page * GCN_DRM_XFB_PAGE_SIZE,
					     GCN_DRM_WIDTH, GCN_DRM_HEIGHT);
		if (!ret) {
			if (!gcn->gx_xrgb8888_frames++)
				drm_info(&gcn->drm,
					 "GX XRGB8888 scanout accelerator active\n");
			goto out_end_access;
		}
		break;
	default:
		ret = -EINVAL;
		goto out_end_access;
	}
	if (ret != -ENODEV)
		drm_err_ratelimited(&gcn->drm,
				    "GX scanout failed (%d); using CPU conversion\n",
				    ret);

	ret = 0;

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

	gcn_drm_vi_write32(gcn, VI_TFBL,
			   VI_FB_POB | xof << VI_FB_XOF_SHIFT | top >> 5);
	gcn_drm_vi_write32(gcn, VI_BFBL, VI_FB_POB | bottom >> 5);
}

static void gcn_drm_quiesce_irqs(struct gcn_drm *gcn)
{
	static const u8 di_regs[] = { VI_DI0, VI_DI1, VI_DI2, VI_DI3 };
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(di_regs); i++) {
		u32 value = in_be32(gcn->vi_base + di_regs[i]);

		gcn_drm_vi_write32(gcn, di_regs[i],
				   value & ~(VI_DI_IRQ | VI_DI_ENABLE));
	}
}

static void gcn_drm_program_ntsc(struct gcn_drm *gcn, bool use_progressive)
{
	u16 dcr = use_progressive ? VI_DCR_NIN : 0;
	u16 vtr = use_progressive ? VI_NTSC_480P_VTR : VI_NTSC_VTR;
	u32 htr0 = use_progressive ? VI_NTSC_480P_HTR0 : VI_NTSC_HTR0;
	u32 htr1 = use_progressive ? VI_NTSC_480P_HTR1 : VI_NTSC_HTR1;
	u32 vto = use_progressive ? VI_NTSC_480P_VTO : VI_NTSC_VTO;
	u32 vte = use_progressive ? VI_NTSC_480P_VTE : VI_NTSC_VTE;
	u32 bboi = use_progressive ? VI_NTSC_480P_BBOI : VI_NTSC_BBOI;
	u32 bbei = use_progressive ? VI_NTSC_480P_BBEI : VI_NTSC_BBEI;
	u16 pcr = use_progressive ? VI_NTSC_480P_PCR : VI_NTSC_PCR;
	u32 di0 = use_progressive ? VI_NTSC_480P_DI0 : VI_NTSC_DI0;
	u32 di1 = use_progressive ? VI_NTSC_480P_DI1 : VI_NTSC_DI1;

	gcn_drm_quiesce_irqs(gcn);
	gcn_drm_vi_write16(gcn, VI_DCR, VI_DCR_RESET);
	udelay(2);
	gcn_drm_vi_write16(gcn, VI_DCR, dcr);
	drm_info(&gcn->drm, "pulsed VI DCR reset\n");

	gcn_drm_vi_write16(gcn, VI_VTR, vtr);
	gcn_drm_vi_write32(gcn, VI_HTR0, htr0);
	gcn_drm_vi_write32(gcn, VI_HTR1, htr1);
	gcn_drm_vi_write32(gcn, VI_VTO, vto);
	gcn_drm_vi_write32(gcn, VI_VTE, vte);
	gcn_drm_vi_write32(gcn, VI_BBOI, bboi);
	gcn_drm_vi_write32(gcn, VI_BBEI, bbei);
	gcn_drm_vi_write32(gcn, VI_TFBR, 0);
	gcn_drm_vi_write32(gcn, VI_BFBR, 0);
	gcn_drm_vi_write16(gcn, VI_PCR, pcr);
	gcn_drm_vi_write16(gcn, VI_HSR, VI_NTSC_HSR);
	gcn_drm_vi_write32(gcn, VI_FCT0, gcn_drm_vi_filter[0]);
	gcn_drm_vi_write32(gcn, VI_FCT1, gcn_drm_vi_filter[1]);
	gcn_drm_vi_write32(gcn, VI_FCT2, gcn_drm_vi_filter[2]);
	gcn_drm_vi_write32(gcn, VI_FCT3, gcn_drm_vi_filter[3]);
	gcn_drm_vi_write32(gcn, VI_FCT4, gcn_drm_vi_filter[4]);
	gcn_drm_vi_write32(gcn, VI_FCT5, gcn_drm_vi_filter[5]);
	gcn_drm_vi_write32(gcn, VI_FCT6, gcn_drm_vi_filter[6]);
	gcn_drm_vi_write32(gcn, VI_AA, 0x00ff0000);
	gcn_drm_vi_write16(gcn, VI_CLK, use_progressive ? 1 : 0);
	gcn_drm_vi_write16(gcn, VI_HSW, GCN_DRM_WIDTH);
	gcn_drm_vi_write16(gcn, VI_HBE, 0);
	gcn_drm_vi_write16(gcn, VI_HBS, 0);
	gcn_drm_vi_write16(gcn, VI_UNK1, 0x00ff);
	gcn_drm_vi_write32(gcn, VI_UNK2, 0x00ff00ff);
	gcn_drm_vi_write32(gcn, VI_UNK3, 0x00ff00ff);

	memset32(gcn->xfb, 0x10801080,
		 2 * GCN_DRM_XFB_PAGE_SIZE / sizeof(u32));
	flush_dcache_range((unsigned long)gcn->xfb,
			   (unsigned long)gcn->xfb +
			   2 * GCN_DRM_XFB_PAGE_SIZE);
	gcn_drm_set_scanout(gcn, 0);
	gcn_drm_vi_write32(gcn, VI_DI0, di0);
	gcn_drm_vi_write32(gcn, VI_DI1, di1);
	gcn_drm_vi_write32(gcn, VI_DI2, 0);
	gcn_drm_vi_write32(gcn, VI_DI3, 0);
	gcn_drm_vi_write16(gcn, VI_DCR, dcr | VI_DCR_ENABLE);

	drm_info(&gcn->drm,
		 "programmed NTSC %s: DCR=%04x VTR=%04x HTR0=%08x HTR1=%08x PCR=%04x CLK=%04x\n",
		 use_progressive ? "480p" : "480i",
		 in_be16(gcn->vi_base + VI_DCR),
		 in_be16(gcn->vi_base + VI_VTR),
		 in_be32(gcn->vi_base + VI_HTR0),
		 in_be32(gcn->vi_base + VI_HTR1),
		 in_be16(gcn->vi_base + VI_PCR),
		 in_be16(gcn->vi_base + VI_CLK));
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

	gcn_drm_vi_write32(gcn, VI_DI1,
			   (value & ~VI_DI_IRQ) | VI_DI_ENABLE);
	return 0;
}

static void gcn_drm_disable_vblank(struct drm_simple_display_pipe *pipe)
{
	struct gcn_drm *gcn = to_gcn_drm(pipe->crtc.dev);
	u32 value = in_be32(gcn->vi_base + VI_DI1);

	gcn_drm_vi_write32(gcn, VI_DI1,
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

static struct drm_framebuffer *
gcn_drm_fb_create(struct drm_device *drm, struct drm_file *file,
		  const struct drm_format_info *info,
		  const struct drm_mode_fb_cmd2 *mode_cmd)
{
	int ret;

	ret = gcn_drm_render_validate_framebuffer(file, mode_cmd);
	if (ret)
		return ERR_PTR(ret);

	return drm_gem_fb_create_with_dirty(drm, file, info, mode_cmd);
}

static const struct drm_mode_config_funcs gcn_drm_mode_config_funcs = {
	.fb_create = gcn_drm_fb_create,
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
		gcn_drm_vi_write32(gcn, VI_DI0, value & ~VI_DI_IRQ);
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
		gcn_drm_vi_write32(gcn, VI_DI1, value & ~VI_DI_IRQ);
		drm_crtc_handle_vblank(&gcn->pipe.crtc);
		handled = true;
	}

	return IRQ_RETVAL(handled);
}

DEFINE_DRM_GEM_FOPS(gcn_drm_fops);

static int gcn_drm_prime_handle_to_fd(struct drm_device *drm,
				      struct drm_file *file, u32 handle,
				      u32 flags, int *prime_fd)
{
	(void)drm;
	(void)file;
	(void)handle;
	(void)flags;
	(void)prime_fd;
	return -EOPNOTSUPP;
}

static int gcn_drm_prime_fd_to_handle(struct drm_device *drm,
				      struct drm_file *file, int prime_fd,
				      u32 *handle)
{
	(void)drm;
	(void)file;
	(void)prime_fd;
	(void)handle;
	return -EOPNOTSUPP;
}

static const struct drm_driver gcn_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC |
			   DRIVER_RENDER | DRIVER_SYNCOBJ,
	.open = gcn_drm_render_open,
	.postclose = gcn_drm_render_postclose,
	.ioctls = gcn_drm_render_ioctls,
	.num_ioctls = DRM_GCN_NUM_IOCTLS,
	.prime_handle_to_fd = gcn_drm_prime_handle_to_fd,
	.prime_fd_to_handle = gcn_drm_prime_fd_to_handle,
	.fops = &gcn_drm_fops,
	.gem_create_object = gcn_drm_render_create_object,
	DRM_GEM_SHMEM_DRIVER_OPS,
	DRM_FBDEV_SHMEM_DRIVER_OPS,
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
	if (progressive && !program_mode)
		return dev_err_probe(dev, -EINVAL,
				     "progressive mode requires program_mode=1\n");

	ret = gcn_drm_acquire_ave(gcn, dev);
	if (ret)
		return ret;

	if (program_mode)
		gcn_drm_program_ntsc(gcn, progressive);

	if (!(in_be16(gcn->vi_base + VI_DCR) & VI_DCR_ENABLE))
		return dev_err_probe(dev, -ENODEV,
				     "VI has no active handoff mode\n");

	ret = gcn_drm_enable_ave(gcn);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to enable AVE chroma exchange\n");

	mutex_lock(&gcn_drm_accel_lock);
	gcn_drm_active = gcn;
	ret = gcn_drm_set_accel_ave_locked(gcn_drm_accel);
	if (ret)
		gcn_drm_active = NULL;
	mutex_unlock(&gcn_drm_accel_lock);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to select scanout chroma order\n");

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
	drm_client_setup(&gcn->drm, drm_format_info(DRM_FORMAT_RGB565));

	drm_info(&gcn->drm,
		 "bound fixed 640x480 %s mode, XFB %08x+%08x\n",
		 program_mode ? (progressive ? "programmed NTSC 480p" :
					     "programmed NTSC 480i") : "handoff",
		 xfb_phys, xfb_size);
	return 0;
}

static void gcn_drm_remove(struct platform_device *pdev)
{
	struct gcn_drm *gcn = platform_get_drvdata(pdev);

	mutex_lock(&gcn_drm_accel_lock);
	if (gcn_drm_active == gcn)
		gcn_drm_active = NULL;
	mutex_unlock(&gcn_drm_accel_lock);
	drm_dev_unplug(&gcn->drm);
	drm_atomic_helper_shutdown(&gcn->drm);
	gcn_drm_vi_write32(gcn, VI_DI0, 0);
	gcn_drm_vi_write32(gcn, VI_DI1, 0);
	gcn_drm_vi_write32(gcn, VI_DI2, 0);
	gcn_drm_vi_write32(gcn, VI_DI3, 0);
	gcn_drm_restore_ave(gcn);
}

static void gcn_drm_shutdown(struct platform_device *pdev)
{
	struct gcn_drm *gcn = platform_get_drvdata(pdev);

	drm_atomic_helper_shutdown(&gcn->drm);
	gcn_drm_restore_ave(gcn);
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
