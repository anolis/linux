/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __UAPI_GCN_DRM_H__
#define __UAPI_GCN_DRM_H__

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define DRM_GCN_RENDER_ABI_VERSION	1

enum drm_gcn_param {
	DRM_GCN_PARAM_ABI_VERSION = 0,
	DRM_GCN_PARAM_PROVIDER_AVAILABLE = 1,
	DRM_GCN_PARAM_MEM1_TOTAL_BYTES = 2,
	DRM_GCN_PARAM_MEM1_FREE_BYTES = 3,
	DRM_GCN_PARAM_MEM1_ALIGNMENT = 4,
	DRM_GCN_PARAM_MAX_EFB_WIDTH = 5,
	DRM_GCN_PARAM_MAX_EFB_HEIGHT = 6,
	DRM_GCN_PARAM_FORMATS = 7,
	DRM_GCN_PARAM_LAYOUTS = 8,
	DRM_GCN_PARAM_FEATURES = 9,
};

#define DRM_GCN_FORMAT_RGB565		(1ULL << 0)
#define DRM_GCN_LAYOUT_TILED_4X4		(1ULL << 0)

#define DRM_GCN_FEATURE_MEM1_GEM		(1ULL << 0)
#define DRM_GCN_FEATURE_CONTEXTS		(1ULL << 1)
#define DRM_GCN_FEATURE_WAIT		(1ULL << 2)
#define DRM_GCN_FEATURE_SYNCOBJ		(1ULL << 3)
#define DRM_GCN_FEATURE_SUBMIT_RGB565	(1ULL << 4)
#define DRM_GCN_FEATURE_FILL_RGB565	(1ULL << 5)
#define DRM_GCN_FEATURE_FILL_RECT_RGB565	(1ULL << 6)
#define DRM_GCN_FEATURE_BLIT_RECT_RGB565	(1ULL << 7)
#define DRM_GCN_FEATURE_BLIT_RECT_RGB565_UNEQUAL_DIMS	(1ULL << 8)
#define DRM_GCN_FEATURE_BLIT_RECT_RGB565_SAME_OBJECT	(1ULL << 9)
#define DRM_GCN_FEATURE_BLIT_SCALED_RGB565	(1ULL << 10)
#define DRM_GCN_FEATURE_SYSTEM_GEM		(1ULL << 11)
#define DRM_GCN_FEATURE_BLIT_SCALED_SYSTEM_RGB565	(1ULL << 12)

struct drm_gcn_get_param {
	__u32 param;
	/* Must be zero. */
	__u32 pad;
	__u64 value;
};

enum drm_gcn_gem_format {
	DRM_GCN_GEM_FORMAT_RGB565 = 1,
};

enum drm_gcn_gem_layout {
	DRM_GCN_GEM_LAYOUT_TILED_4X4 = 1,
};

#define DRM_GCN_GEM_CREATE_SYSTEM	(1U << 0)

struct drm_gcn_gem_create {
	__u32 width;
	__u32 height;
	__u32 format;
	__u32 layout;
	/* DRM_GCN_GEM_CREATE_* flags. */
	__u32 flags;
	/* Must be zero on input; GEM handle on success. */
	__u32 handle;
	/* Must be zero on input; allocation size on success. */
	__u64 size;
};

struct drm_gcn_gem_mmap {
	__u32 handle;
	/* Must be zero. */
	__u32 pad;
	/* Must be zero on input; DRM mmap offset on success. */
	__u64 offset;
};

struct drm_gcn_ctx_create {
	/* Must be zero. */
	__u32 flags;
	/* Must be zero on input; per-file context ID on success. */
	__u32 id;
};

struct drm_gcn_ctx_free {
	__u32 id;
	/* Must be zero. */
	__u32 pad;
};

#define DRM_GCN_WAIT_WRITE		(1U << 0)

struct drm_gcn_wait {
	__u32 handle;
	__u32 flags;
	/*
	 * Absolute CLOCK_MONOTONIC deadline in nanoseconds. U64_MAX waits
	 * indefinitely.
	 */
	__u64 timeout_ns;
};

enum drm_gcn_render_op {
	DRM_GCN_RENDER_OP_COPY_RGB565 = 1,
	DRM_GCN_RENDER_OP_FILL_RGB565 = 2,
	DRM_GCN_RENDER_OP_FILL_RECT_RGB565 = 3,
	DRM_GCN_RENDER_OP_BLIT_RECT_RGB565 = 4,
};

#define DRM_GCN_RECT_COLOR_SHIFT	0
#define DRM_GCN_RECT_X_SHIFT		16
#define DRM_GCN_RECT_Y_SHIFT		26
#define DRM_GCN_RECT_WIDTH_SHIFT	36
#define DRM_GCN_RECT_HEIGHT_SHIFT	46
#define DRM_GCN_RECT_COLOR_MASK		0xffffULL
#define DRM_GCN_RECT_FIELD_MASK		0x3ffULL
#define DRM_GCN_RECT_RESERVED_MASK	(0xffULL << 56)

/* Width and height must be in the inclusive range 1..1024. */
#define DRM_GCN_RECT_DATA(color, x, y, width, height) \
	(((__u64)(color) & DRM_GCN_RECT_COLOR_MASK) | \
	 (((__u64)(x) & DRM_GCN_RECT_FIELD_MASK) << DRM_GCN_RECT_X_SHIFT) | \
	 (((__u64)(y) & DRM_GCN_RECT_FIELD_MASK) << DRM_GCN_RECT_Y_SHIFT) | \
	 (((__u64)((width) - 1) & DRM_GCN_RECT_FIELD_MASK) << \
	  DRM_GCN_RECT_WIDTH_SHIFT) | \
	 (((__u64)((height) - 1) & DRM_GCN_RECT_FIELD_MASK) << \
	  DRM_GCN_RECT_HEIGHT_SHIFT))

#define DRM_GCN_BLIT_SRC_X_SHIFT	0
#define DRM_GCN_BLIT_SRC_Y_SHIFT	10
#define DRM_GCN_BLIT_DST_X_SHIFT	20
#define DRM_GCN_BLIT_DST_Y_SHIFT	30
#define DRM_GCN_BLIT_WIDTH_SHIFT	40
#define DRM_GCN_BLIT_HEIGHT_SHIFT	50
#define DRM_GCN_BLIT_FIELD_MASK		0x3ffULL
#define DRM_GCN_BLIT_RESERVED_MASK	(0xfULL << 60)

/* Width and height must be in the inclusive range 1..1024. */
#define DRM_GCN_BLIT_RECT_DATA(src_x, src_y, dst_x, dst_y, width, height) \
	(((__u64)(src_x) & DRM_GCN_BLIT_FIELD_MASK) | \
	 (((__u64)(src_y) & DRM_GCN_BLIT_FIELD_MASK) << \
	  DRM_GCN_BLIT_SRC_Y_SHIFT) | \
	 (((__u64)(dst_x) & DRM_GCN_BLIT_FIELD_MASK) << \
	  DRM_GCN_BLIT_DST_X_SHIFT) | \
	 (((__u64)(dst_y) & DRM_GCN_BLIT_FIELD_MASK) << \
	  DRM_GCN_BLIT_DST_Y_SHIFT) | \
	 (((__u64)((width) - 1) & DRM_GCN_BLIT_FIELD_MASK) << \
	  DRM_GCN_BLIT_WIDTH_SHIFT) | \
	 (((__u64)((height) - 1) & DRM_GCN_BLIT_FIELD_MASK) << \
	  DRM_GCN_BLIT_HEIGHT_SHIFT))

/*
 * Submit one validated operation between driver-owned MEM1 GEM objects.
 * No command bytes, register values, or physical addresses are accepted.
 */
struct drm_gcn_submit {
	__u32 ctx_id;
	__u32 op;
	__u32 src_handle;
	__u32 dst_handle;
	/* Optional binary syncobj replaced with the completion fence. */
	__u32 out_syncobj;
	/* Must be zero. */
	__u32 flags;
	/* Operation-specific data; see the rectangle packing macros above. */
	union {
		__u64 data;
		/* Legacy name retained for source compatibility. */
		__u64 pad;
	};
};

/*
 * Scale one source rectangle into one destination rectangle using nearest
 * sampling. Source and destination may name the same object.
 */
struct drm_gcn_blit_scaled {
	__u32 ctx_id;
	__u32 src_handle;
	__u32 dst_handle;
	/* Optional binary syncobj replaced with the completion fence. */
	__u32 out_syncobj;
	/* Must be zero. */
	__u32 flags;
	__u16 src_x;
	__u16 src_y;
	__u16 src_width;
	__u16 src_height;
	__u16 dst_x;
	__u16 dst_y;
	__u16 dst_width;
	__u16 dst_height;
	/* Must be zero. */
	__u32 pad;
};

#define DRM_GCN_GET_PARAM	0x00
#define DRM_GCN_GEM_CREATE	0x01
#define DRM_GCN_GEM_MMAP	0x02
#define DRM_GCN_CTX_CREATE	0x03
#define DRM_GCN_CTX_FREE	0x04
#define DRM_GCN_WAIT		0x05
#define DRM_GCN_SUBMIT		0x06
#define DRM_GCN_BLIT_SCALED	0x07
#define DRM_GCN_NUM_IOCTLS	0x08

#define DRM_IOCTL_GCN_GET_PARAM \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_GCN_GET_PARAM, \
		 struct drm_gcn_get_param)
#define DRM_IOCTL_GCN_GEM_CREATE \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_GCN_GEM_CREATE, \
		 struct drm_gcn_gem_create)
#define DRM_IOCTL_GCN_GEM_MMAP \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_GCN_GEM_MMAP, \
		 struct drm_gcn_gem_mmap)
#define DRM_IOCTL_GCN_CTX_CREATE \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_GCN_CTX_CREATE, \
		 struct drm_gcn_ctx_create)
#define DRM_IOCTL_GCN_CTX_FREE \
	DRM_IOW(DRM_COMMAND_BASE + DRM_GCN_CTX_FREE, \
		struct drm_gcn_ctx_free)
#define DRM_IOCTL_GCN_WAIT \
	DRM_IOW(DRM_COMMAND_BASE + DRM_GCN_WAIT, struct drm_gcn_wait)
#define DRM_IOCTL_GCN_SUBMIT \
	DRM_IOW(DRM_COMMAND_BASE + DRM_GCN_SUBMIT, struct drm_gcn_submit)
#define DRM_IOCTL_GCN_BLIT_SCALED \
	DRM_IOW(DRM_COMMAND_BASE + DRM_GCN_BLIT_SCALED, \
		struct drm_gcn_blit_scaled)

#if defined(__cplusplus)
}
#endif

#endif /* __UAPI_GCN_DRM_H__ */
