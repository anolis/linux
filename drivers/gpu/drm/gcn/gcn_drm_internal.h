/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __GCN_DRM_INTERNAL_H__
#define __GCN_DRM_INTERNAL_H__

#include <linux/gcn_drm_accel.h>
#include <uapi/drm/gcn_drm.h>

struct drm_device;
struct drm_file;
struct drm_gem_object;
struct vm_area_struct;

int gcn_drm_provider_info(struct gcn_drm_mem1_info *info);
int gcn_drm_provider_alloc(size_t size,
			   const struct gcn_drm_accel_ops **provider,
			   void **allocation);
void gcn_drm_provider_free(const struct gcn_drm_accel_ops *provider,
			   void *allocation);
int gcn_drm_provider_mmap(const struct gcn_drm_accel_ops *provider,
			  void *allocation, struct vm_area_struct *vma);
int gcn_drm_provider_copy(const struct gcn_drm_accel_ops *provider,
			  void *src_allocation, void *dst_allocation,
			  u16 width, u16 height);
int gcn_drm_provider_fill(const struct gcn_drm_accel_ops *provider,
			  void *dst_allocation, u16 width, u16 height,
			  u16 color);
int gcn_drm_provider_fill_rect(const struct gcn_drm_accel_ops *provider,
			       void *dst_allocation, u16 width, u16 height,
			       u16 x, u16 y, u16 rect_width,
			       u16 rect_height, u16 color);
int gcn_drm_provider_blit_rect(const struct gcn_drm_accel_ops *provider,
			       void *src_allocation, void *dst_allocation,
			       u16 width, u16 height, u16 src_x, u16 src_y,
			       u16 dst_x, u16 dst_y, u16 rect_width,
			       u16 rect_height);

int gcn_drm_render_open(struct drm_device *drm, struct drm_file *file);
void gcn_drm_render_postclose(struct drm_device *drm, struct drm_file *file);

extern const struct drm_ioctl_desc
gcn_drm_render_ioctls[DRM_GCN_NUM_IOCTLS];

#endif /* __GCN_DRM_INTERNAL_H__ */
