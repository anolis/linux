/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __DRM_GCN_GX_MEM1_H__
#define __DRM_GCN_GX_MEM1_H__

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>

#include <drm/drm_mm.h>

struct gcn_gx_mem1_allocator {
	struct drm_mm mm;
	u64 start;
	u64 size;
	bool initialized;
};

static inline int
gcn_gx_mem1_allocator_init(struct gcn_gx_mem1_allocator *allocator,
			   u64 start, u64 size, u64 alignment)
{
	if (!allocator || !size || !is_power_of_2(alignment) ||
	    !IS_ALIGNED(start, alignment) || !IS_ALIGNED(size, alignment) ||
	    start + size < start)
		return -EINVAL;

	memset(allocator, 0, sizeof(*allocator));
	drm_mm_init(&allocator->mm, start, size);
	allocator->start = start;
	allocator->size = size;
	allocator->initialized = true;
	return 0;
}

static inline int
gcn_gx_mem1_insert(struct gcn_gx_mem1_allocator *allocator,
		   struct drm_mm_node *node, u64 size, u64 alignment)
{
	if (!allocator || !allocator->initialized || !node || !size ||
	    !is_power_of_2(alignment))
		return -EINVAL;

	memset(node, 0, sizeof(*node));
	return drm_mm_insert_node_generic(&allocator->mm, node, size,
					  alignment, 0, DRM_MM_INSERT_LOW);
}

static inline void gcn_gx_mem1_remove(struct drm_mm_node *node)
{
	if (node && drm_mm_node_allocated(node))
		drm_mm_remove_node(node);
}

static inline bool
gcn_gx_mem1_contains(const struct gcn_gx_mem1_allocator *allocator,
		     const struct drm_mm_node *node)
{
	return allocator && allocator->initialized && node &&
	       drm_mm_node_allocated(node) &&
	       node->start >= allocator->start &&
	       allocator->start + allocator->size >= allocator->start &&
	       node->start + node->size >= node->start &&
	       node->start + node->size <= allocator->start + allocator->size;
}

static inline void
gcn_gx_mem1_allocator_fini(struct gcn_gx_mem1_allocator *allocator)
{
	if (!allocator || !allocator->initialized)
		return;

	WARN_ON(!drm_mm_clean(&allocator->mm));
	drm_mm_takedown(&allocator->mm);
	memset(allocator, 0, sizeof(*allocator));
}

#endif
