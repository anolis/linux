/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_GCN_DRM_ACCEL_H
#define _LINUX_GCN_DRM_ACCEL_H

#include <linux/types.h>

struct gcn_drm_accel_ops {
	const char *name;
	int (*blit_rgb565)(const void *src, u32 src_pitch, u32 xfb_phys,
			   u16 width, u16 height);
};

int gcn_drm_register_accel(const struct gcn_drm_accel_ops *ops);
void gcn_drm_unregister_accel(const struct gcn_drm_accel_ops *ops);

#endif /* _LINUX_GCN_DRM_ACCEL_H */
