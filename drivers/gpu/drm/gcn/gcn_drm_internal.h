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

int gcn_drm_render_open(struct drm_device *drm, struct drm_file *file);
void gcn_drm_render_postclose(struct drm_device *drm, struct drm_file *file);

extern const struct drm_ioctl_desc
gcn_drm_render_ioctls[DRM_GCN_NUM_IOCTLS];

#endif /* __GCN_DRM_INTERNAL_H__ */
