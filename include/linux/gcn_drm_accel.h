/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_GCN_DRM_ACCEL_H
#define _LINUX_GCN_DRM_ACCEL_H

#include <linux/types.h>

struct module;
struct vm_area_struct;

struct gcn_drm_mem1_info {
	u64 total_bytes;
	u64 free_bytes;
	u32 alignment;
	u32 max_width;
	u32 max_height;
	u64 formats;
	u64 layouts;
	u64 features;
};

struct gcn_drm_accel_ops {
	const char *name;
	struct module *owner;
	int (*blit_rgb565)(const void *src, u32 src_pitch, u32 xfb_phys,
			   u16 width, u16 height);
	int (*blit_xrgb8888)(const void *src, u32 src_pitch, u32 xfb_phys,
			     u16 width, u16 height);
	int (*mem1_info)(struct gcn_drm_mem1_info *info);
	int (*mem1_alloc)(size_t size, void **allocation);
	void (*mem1_free)(void *allocation);
	int (*mem1_mmap)(void *allocation, struct vm_area_struct *vma);
	int (*submit_rgb565)(void *src_allocation, void *dst_allocation,
			     u16 width, u16 height);
	int (*fill_rgb565)(void *dst_allocation, u16 width, u16 height,
			   u16 color);
	int (*fill_rect_rgb565)(void *dst_allocation, u16 width, u16 height,
				u16 x, u16 y, u16 rect_width,
				u16 rect_height, u16 color);
};

int gcn_drm_register_accel(const struct gcn_drm_accel_ops *ops);
void gcn_drm_unregister_accel(const struct gcn_drm_accel_ops *ops);

#endif /* _LINUX_GCN_DRM_ACCEL_H */
