/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __GCN_DRM_RENDER_H__
#define __GCN_DRM_RENDER_H__

#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/overflow.h>
#include <linux/types.h>

#include <uapi/drm/gcn_drm.h>

#define GCN_DRM_RENDER_MAX_WIDTH	640U
#define GCN_DRM_RENDER_MAX_HEIGHT	576U

static inline int
gcn_drm_render_bo_size(const struct drm_gcn_gem_create *args, u64 *size)
{
	u64 bytes;

	if (!args || !size || args->flags || !args->width || !args->height ||
	    args->width > GCN_DRM_RENDER_MAX_WIDTH ||
	    args->height > GCN_DRM_RENDER_MAX_HEIGHT ||
	    (args->width & 3) || (args->height & 3) ||
	    args->format != DRM_GCN_GEM_FORMAT_RGB565 ||
	    args->layout != DRM_GCN_GEM_LAYOUT_TILED_4X4)
		return -EINVAL;

	if (check_mul_overflow((u64)args->width, (u64)args->height, &bytes) ||
	    check_mul_overflow(bytes, 2ULL, &bytes))
		return -EOVERFLOW;

	*size = PAGE_ALIGN(bytes);
	return 0;
}

static inline unsigned long gcn_drm_render_timeout_jiffies(u64 timeout_ns,
							   u64 now_ns)
{
	u64 timeout_jiffies;
	u64 remaining;

	if (timeout_ns == U64_MAX)
		return MAX_SCHEDULE_TIMEOUT;
	if (timeout_ns <= now_ns)
		return 0;

	remaining = timeout_ns - now_ns;
	timeout_jiffies = nsecs_to_jiffies64(remaining);
	if (timeout_jiffies >= MAX_SCHEDULE_TIMEOUT)
		return MAX_SCHEDULE_TIMEOUT - 1;

	return max_t(unsigned long, 1, timeout_jiffies);
}

static inline int
gcn_drm_render_validate_submit(const struct drm_gcn_submit *args)
{
	if (!args || !args->ctx_id || !args->src_handle || !args->dst_handle ||
	    args->src_handle == args->dst_handle || args->flags || args->pad ||
	    args->op != DRM_GCN_RENDER_OP_COPY_RGB565)
		return -EINVAL;

	return 0;
}

#endif /* __GCN_DRM_RENDER_H__ */
