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

struct gcn_drm_render_rect {
	u16 x;
	u16 y;
	u16 width;
	u16 height;
	u16 color;
};

static inline void gcn_drm_render_decode_rect(u64 data,
					      struct gcn_drm_render_rect *rect)
{
	rect->color = (data >> DRM_GCN_RECT_COLOR_SHIFT) &
		      DRM_GCN_RECT_COLOR_MASK;
	rect->x = (data >> DRM_GCN_RECT_X_SHIFT) & DRM_GCN_RECT_FIELD_MASK;
	rect->y = (data >> DRM_GCN_RECT_Y_SHIFT) & DRM_GCN_RECT_FIELD_MASK;
	rect->width = ((data >> DRM_GCN_RECT_WIDTH_SHIFT) &
		       DRM_GCN_RECT_FIELD_MASK) + 1;
	rect->height = ((data >> DRM_GCN_RECT_HEIGHT_SHIFT) &
			DRM_GCN_RECT_FIELD_MASK) + 1;
}

static inline int gcn_drm_render_validate_rect(u64 data, u16 dst_width,
					       u16 dst_height)
{
	struct gcn_drm_render_rect rect;

	if (data & DRM_GCN_RECT_RESERVED_MASK)
		return -EINVAL;

	gcn_drm_render_decode_rect(data, &rect);
	if (rect.x >= dst_width || rect.y >= dst_height ||
	    rect.width > dst_width - rect.x ||
	    rect.height > dst_height - rect.y)
		return -EINVAL;

	return 0;
}

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
	if (!args || !args->ctx_id || !args->dst_handle || args->flags)
		return -EINVAL;

	switch (args->op) {
	case DRM_GCN_RENDER_OP_COPY_RGB565:
		if (!args->src_handle ||
		    args->src_handle == args->dst_handle || args->data)
			return -EINVAL;
		return 0;
	case DRM_GCN_RENDER_OP_FILL_RGB565:
		if (args->src_handle || args->data & ~0xffffULL)
			return -EINVAL;
		return 0;
	case DRM_GCN_RENDER_OP_FILL_RECT_RGB565:
		if (args->src_handle ||
		    args->data & DRM_GCN_RECT_RESERVED_MASK)
			return -EINVAL;
		return 0;
	default:
		return -EINVAL;
	}
}

#endif /* __GCN_DRM_RENDER_H__ */
