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
#define DRM_GCN_FORMAT_XRGB8888		(1ULL << 1)
#define DRM_GCN_LAYOUT_TILED_4X4		(1ULL << 0)
#define DRM_GCN_LAYOUT_LINEAR		(1ULL << 1)

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
#define DRM_GCN_FEATURE_SYSTEM_GEM_LINEAR	(1ULL << 13)
#define DRM_GCN_FEATURE_BLIT_SCALED_SYSTEM_XRGB8888_TO_RGB565	(1ULL << 14)
#define DRM_GCN_FEATURE_DRAW_TRIANGLE_RGB565	(1ULL << 15)
#define DRM_GCN_FEATURE_DRAW_TRIANGLES_RGB565	(1ULL << 16)
#define DRM_GCN_FEATURE_DRAW_TRIANGLES_STATE_RGB565	(1ULL << 17)
#define DRM_GCN_FEATURE_DRAW_TRIANGLES_DEPTH_RGB565	(1ULL << 18)
#define DRM_GCN_FEATURE_DRAW_TEXTURED_TRIANGLES_RGB565	(1ULL << 19)
#define DRM_GCN_FEATURE_DRAW_INDEXED_TRIANGLES_RGB565	(1ULL << 20)
#define DRM_GCN_FEATURE_DRAW_INDEXED_TEXTURED_TRIANGLES_RGB565	(1ULL << 21)

struct drm_gcn_get_param {
	__u32 param;
	/* Must be zero. */
	__u32 pad;
	__u64 value;
};

enum drm_gcn_gem_format {
	DRM_GCN_GEM_FORMAT_RGB565 = 1,
	DRM_GCN_GEM_FORMAT_XRGB8888 = 2,
};

enum drm_gcn_gem_layout {
	DRM_GCN_GEM_LAYOUT_TILED_4X4 = 1,
	DRM_GCN_GEM_LAYOUT_LINEAR = 2,
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
 * sampling. Matching RGB565 objects may name the same object. A linear
 * system-memory XRGB8888 source may target a distinct RGB565 system object.
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

/* Colors are encoded as 0xRRGGBBAA. Alpha must currently be 0xff. */
struct drm_gcn_color_vertex {
	__u16 x;
	__u16 y;
	__u32 rgba;
};

struct drm_gcn_color_triangle {
	struct drm_gcn_color_vertex vertices[3];
};

#define DRM_GCN_RGBA8(r, g, b, a) \
	(((__u32)(r) << 24) | ((__u32)(g) << 16) | \
	 ((__u32)(b) << 8) | (__u32)(a))

/*
 * Draw one untextured, Gouraud-shaded triangle into a tiled RGB565 object.
 * Coordinates are in destination pixel space and may lie on its right or
 * bottom edge. Raw commands, registers, and physical addresses are never
 * accepted.
 */
struct drm_gcn_draw_triangle {
	__u32 ctx_id;
	__u32 dst_handle;
	/* Optional binary syncobj replaced with the completion fence. */
	__u32 out_syncobj;
	/* Must be zero. */
	__u32 flags;
	struct drm_gcn_color_vertex vertices[3];
	/* Must be zero. */
	__u32 pad[2];
};

#define DRM_GCN_MAX_TRIANGLES	64U
#define DRM_GCN_MAX_VERTICES	(DRM_GCN_MAX_TRIANGLES * 3U)

/*
 * Draw a bounded array of untextured, Gouraud-shaded triangles into one tiled
 * RGB565 object. The complete array is copied and validated before execution.
 * It is emitted as one primitive stream and completed with one destination
 * reservation fence.
 */
struct drm_gcn_draw_triangles {
	__u32 ctx_id;
	__u32 dst_handle;
	/* Optional binary syncobj replaced with the completion fence. */
	__u32 out_syncobj;
	/* Must be zero. */
	__u32 flags;
	/* Inclusive range 1..DRM_GCN_MAX_TRIANGLES. */
	__u32 triangle_count;
	/* Must be zero. */
	__u32 pad0;
	/* Userspace pointer to drm_gcn_color_triangle[triangle_count]. */
	__u64 triangles_ptr;
	/* Must be zero. */
	__u64 pad[2];
};

enum drm_gcn_blend_mode {
	DRM_GCN_BLEND_NONE = 0,
	DRM_GCN_BLEND_SRC_ALPHA = 1,
};

struct drm_gcn_draw_state {
	/* Destination-space viewport. Width and height must be nonzero. */
	__u16 viewport_x;
	__u16 viewport_y;
	__u16 viewport_width;
	__u16 viewport_height;
	/* Destination-space scissor. Width and height must be nonzero. */
	__u16 scissor_x;
	__u16 scissor_y;
	__u16 scissor_width;
	__u16 scissor_height;
	/* One of drm_gcn_blend_mode. */
	__u32 blend_mode;
	/* Must be zero. */
	__u32 pad;
};

/*
 * Draw a bounded triangle array with semantic raster state. Coordinates remain
 * in destination pixel space; the viewport maps that coordinate space into
 * its destination rectangle. Source-alpha blending permits non-opaque vertex
 * alpha. No raw GX register values are accepted.
 */
struct drm_gcn_draw_triangles_state {
	__u32 ctx_id;
	__u32 dst_handle;
	/* Optional binary syncobj replaced with the completion fence. */
	__u32 out_syncobj;
	/* Must be zero. */
	__u32 flags;
	/* Inclusive range 1..DRM_GCN_MAX_TRIANGLES. */
	__u32 triangle_count;
	/* Must be zero. */
	__u32 pad0;
	/* Userspace pointer to drm_gcn_color_triangle[triangle_count]. */
	__u64 triangles_ptr;
	struct drm_gcn_draw_state state;
	/* Must be zero. */
	__u64 pad1;
};

#define DRM_GCN_DEPTH_MAX	0x00ffffffU

struct drm_gcn_color_depth_vertex {
	__u16 x;
	__u16 y;
	/* Screen-space depth: zero is near and DRM_GCN_DEPTH_MAX is far. */
	__u32 z;
	__u32 rgba;
};

struct drm_gcn_color_depth_triangle {
	struct drm_gcn_color_depth_vertex vertices[3];
};

enum drm_gcn_depth_compare {
	DRM_GCN_DEPTH_NEVER = 0,
	DRM_GCN_DEPTH_LESS = 1,
	DRM_GCN_DEPTH_EQUAL = 2,
	DRM_GCN_DEPTH_LEQUAL = 3,
	DRM_GCN_DEPTH_GREATER = 4,
	DRM_GCN_DEPTH_NEQUAL = 5,
	DRM_GCN_DEPTH_GEQUAL = 6,
	DRM_GCN_DEPTH_ALWAYS = 7,
};

struct drm_gcn_depth_state {
	/* Boolean: enable depth comparison. */
	__u32 test_enable;
	/* One of drm_gcn_depth_compare. */
	__u32 compare;
	/* Boolean: update depth for fragments that pass. */
	__u32 write_enable;
	/* Must be zero. */
	__u32 pad;
};

/*
 * Draw a bounded triangle array with semantic raster and depth state. The EFB
 * depth buffer is initialized to DRM_GCN_DEPTH_MAX for every request. Existing
 * colour-only draw ioctls remain independent and byte-for-byte unchanged.
 */
struct drm_gcn_draw_triangles_depth {
	__u32 ctx_id;
	__u32 dst_handle;
	/* Optional binary syncobj replaced with the completion fence. */
	__u32 out_syncobj;
	/* Must be zero. */
	__u32 flags;
	/* Inclusive range 1..DRM_GCN_MAX_TRIANGLES. */
	__u32 triangle_count;
	/* Must be zero. */
	__u32 pad0;
	/* Userspace pointer to drm_gcn_color_depth_triangle[triangle_count]. */
	__u64 triangles_ptr;
	struct drm_gcn_draw_state state;
	struct drm_gcn_depth_state depth;
	/* Must be zero. */
	__u64 pad1;
};

/*
 * Texture coordinates use texel-edge space. Valid coordinates range from
 * zero through the corresponding source extent, inclusive. GX applies the
 * fixed nearest-sampling phase internally; userspace does not encode it.
 */
struct drm_gcn_texture_vertex {
	__u16 x;
	__u16 y;
	__u16 s;
	__u16 t;
};

struct drm_gcn_texture_triangle {
	struct drm_gcn_texture_vertex vertices[3];
};

/*
 * Draw bounded, unlit RGB565 textured triangles into a distinct tiled RGB565
 * destination. Sampling is nearest with clamp wrapping and texture colour
 * replaces raster colour. Only viewport and scissor state are accepted;
 * blend_mode must be DRM_GCN_BLEND_NONE.
 */
struct drm_gcn_draw_textured_triangles {
	__u32 ctx_id;
	__u32 src_handle;
	__u32 dst_handle;
	/* Optional binary syncobj replaced with the completion fence. */
	__u32 out_syncobj;
	/* Must be zero. */
	__u32 flags;
	/* Inclusive range 1..DRM_GCN_MAX_TRIANGLES. */
	__u32 triangle_count;
	/* Userspace pointer to drm_gcn_texture_triangle[triangle_count]. */
	__u64 triangles_ptr;
	struct drm_gcn_draw_state state;
	/* Must be zero. */
	__u64 pad;
};

/*
 * Draw bounded indexed, Gouraud-shaded triangles into one tiled RGB565
 * destination. Indices are unsigned 16-bit vertex-array elements; every
 * referenced triangle must be non-degenerate after index resolution.
 */
struct drm_gcn_draw_indexed_triangles {
	__u32 ctx_id;
	__u32 dst_handle;
	/* Optional binary syncobj replaced with the completion fence. */
	__u32 out_syncobj;
	/* Must be zero. */
	__u32 flags;
	/* Inclusive range 3..DRM_GCN_MAX_VERTICES. */
	__u32 vertex_count;
	/* Inclusive range 1..DRM_GCN_MAX_TRIANGLES. */
	__u32 triangle_count;
	/* Userspace pointer to drm_gcn_color_vertex[vertex_count]. */
	__u64 vertices_ptr;
	/* Userspace pointer to __u16[triangle_count * 3]. */
	__u64 indices_ptr;
	struct drm_gcn_draw_state state;
	/* Must be zero. */
	__u64 pad;
};

/*
 * Draw bounded indexed, unlit RGB565 textured triangles into a distinct tiled
 * RGB565 destination. Vertices are shared XY/ST elements and indices are
 * unsigned 16-bit elements. Sampling and state match the non-indexed textured
 * operation.
 */
struct drm_gcn_draw_indexed_textured {
	__u32 ctx_id;
	__u32 src_handle;
	__u32 dst_handle;
	/* Optional binary syncobj replaced with the completion fence. */
	__u32 out_syncobj;
	/* Must be zero. */
	__u32 flags;
	/* Inclusive range 3..DRM_GCN_MAX_VERTICES. */
	__u32 vertex_count;
	/* Inclusive range 1..DRM_GCN_MAX_TRIANGLES. */
	__u32 triangle_count;
	/* Must be zero. */
	__u32 pad0;
	/* Userspace pointer to drm_gcn_texture_vertex[vertex_count]. */
	__u64 vertices_ptr;
	/* Userspace pointer to __u16[triangle_count * 3]. */
	__u64 indices_ptr;
	struct drm_gcn_draw_state state;
	/* Must be zero. */
	__u64 pad1;
};

#define DRM_GCN_GET_PARAM	0x00
#define DRM_GCN_GEM_CREATE	0x01
#define DRM_GCN_GEM_MMAP	0x02
#define DRM_GCN_CTX_CREATE	0x03
#define DRM_GCN_CTX_FREE	0x04
#define DRM_GCN_WAIT		0x05
#define DRM_GCN_SUBMIT		0x06
#define DRM_GCN_BLIT_SCALED	0x07
#define DRM_GCN_DRAW_TRIANGLE	0x08
#define DRM_GCN_DRAW_TRIANGLES	0x09
#define DRM_GCN_DRAW_TRIANGLES_STATE	0x0a
#define DRM_GCN_DRAW_TRIANGLES_DEPTH	0x0b
#define DRM_GCN_DRAW_TEXTURED_TRIANGLES	0x0c
#define DRM_GCN_DRAW_INDEXED_TRIANGLES	0x0d
#define DRM_GCN_DRAW_INDEXED_TEXTURED_TRIANGLES	0x0e
#define DRM_GCN_NUM_IOCTLS	0x0f

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
#define DRM_IOCTL_GCN_DRAW_TRIANGLE \
	DRM_IOW(DRM_COMMAND_BASE + DRM_GCN_DRAW_TRIANGLE, \
		 struct drm_gcn_draw_triangle)
#define DRM_IOCTL_GCN_DRAW_TRIANGLES \
	DRM_IOW(DRM_COMMAND_BASE + DRM_GCN_DRAW_TRIANGLES, \
		 struct drm_gcn_draw_triangles)
#define DRM_IOCTL_GCN_DRAW_TRIANGLES_STATE \
	DRM_IOW(DRM_COMMAND_BASE + DRM_GCN_DRAW_TRIANGLES_STATE, \
		 struct drm_gcn_draw_triangles_state)
#define DRM_IOCTL_GCN_DRAW_TRIANGLES_DEPTH \
	DRM_IOW(DRM_COMMAND_BASE + DRM_GCN_DRAW_TRIANGLES_DEPTH, \
		 struct drm_gcn_draw_triangles_depth)
#define DRM_IOCTL_GCN_DRAW_TEXTURED_TRIANGLES \
	DRM_IOW(DRM_COMMAND_BASE + DRM_GCN_DRAW_TEXTURED_TRIANGLES, \
		 struct drm_gcn_draw_textured_triangles)
#define DRM_IOCTL_GCN_DRAW_INDEXED_TRIANGLES \
	DRM_IOW(DRM_COMMAND_BASE + DRM_GCN_DRAW_INDEXED_TRIANGLES, \
		 struct drm_gcn_draw_indexed_triangles)
#define DRM_IOCTL_GCN_DRAW_INDEXED_TEXTURED_TRIANGLES \
	DRM_IOW(DRM_COMMAND_BASE + DRM_GCN_DRAW_INDEXED_TEXTURED_TRIANGLES, \
		 struct drm_gcn_draw_indexed_textured)

#if defined(__cplusplus)
}
#endif

#endif /* __UAPI_GCN_DRM_H__ */
