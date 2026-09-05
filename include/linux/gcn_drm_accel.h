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

struct gcn_drm_color_vertex {
	u16 x;
	u16 y;
	u8 r;
	u8 g;
	u8 b;
	u8 a;
};

struct gcn_drm_draw_state {
	u16 viewport_x;
	u16 viewport_y;
	u16 viewport_width;
	u16 viewport_height;
	u16 scissor_x;
	u16 scissor_y;
	u16 scissor_width;
	u16 scissor_height;
	u32 blend_mode;
	u32 cull_mode;
};

struct gcn_drm_color_depth_vertex {
	u16 x;
	u16 y;
	u32 z;
	u8 r;
	u8 g;
	u8 b;
	u8 a;
};

struct gcn_drm_depth_state {
	bool test_enable;
	u32 compare;
	bool write_enable;
};

struct gcn_drm_texture_vertex {
	u16 x;
	u16 y;
	u16 s;
	u16 t;
};

struct gcn_drm_itex_depth_vertex {
	u16 x;
	u16 y;
	u32 z;
	u16 s;
	u16 t;
};

struct gcn_drm_fixed_vertex {
	u16 x;
	u16 y;
	u32 z;
	u8 r;
	u8 g;
	u8 b;
	u8 a;
	u16 s;
	u16 t;
};

struct gcn_drm_accel_ops {
	/*
	 * This layout is a private core/provider ABI. Add new callbacks only at
	 * the end and bump the registration symbol suffix for any layout change.
	 */
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
	int (*draw_triangle_rgb565)(void *dst_allocation, u16 width, u16 height,
				    const struct gcn_drm_color_vertex vertices[3]);
	int (*draw_triangles_rgb565)(void *dst_allocation, u16 width, u16 height,
				     const struct gcn_drm_color_vertex *vertices,
				     u32 triangle_count);
	int (*draw_triangles_state_rgb565)(void *dst_allocation, u16 width, u16 height,
					   const struct gcn_drm_color_vertex *vertices,
					   u32 triangle_count,
					   const struct gcn_drm_draw_state *state);
	int (*blit_rect_rgb565)(void *src_allocation, void *dst_allocation,
				u16 src_width, u16 src_height, u16 dst_width,
				u16 dst_height, u16 src_x, u16 src_y, u16 dst_x,
				u16 dst_y, u16 rect_width, u16 rect_height);
	int (*blit_scaled_rgb565)(void *src_allocation, void *dst_allocation,
				  u16 src_width, u16 src_height,
				  u16 dst_width, u16 dst_height, u16 src_x,
				  u16 src_y, u16 src_rect_width,
				  u16 src_rect_height, u16 dst_x, u16 dst_y,
				  u16 dst_rect_width, u16 dst_rect_height);
	int (*blit_scaled_system_rgb565)(const void *src, void *dst,
					 u16 src_width, u16 src_height,
					 u16 dst_width, u16 dst_height,
					 u32 src_layout, u32 dst_layout,
					 u16 src_x, u16 src_y,
					 u16 src_rect_width,
					 u16 src_rect_height, u16 dst_x,
					 u16 dst_y, u16 dst_rect_width,
					 u16 dst_rect_height);
	int (*blit_scaled_system_xrgb8888)(const void *src, void *dst,
					   u16 src_width, u16 src_height,
					   u16 dst_width, u16 dst_height,
					   u32 dst_layout, u16 src_x, u16 src_y,
					   u16 src_rect_width,
					   u16 src_rect_height, u16 dst_x,
					   u16 dst_y, u16 dst_rect_width,
					   u16 dst_rect_height);
	int (*draw_triangles_depth_rgb565)(void *dst_allocation, u16 width,
					   u16 height,
					   const struct gcn_drm_color_depth_vertex *vertices,
					   u32 triangle_count,
					   const struct gcn_drm_draw_state *state,
					   const struct gcn_drm_depth_state *depth);
	int (*draw_textured_triangles_rgb565)(void *src_allocation,
					      void *dst_allocation,
					      u16 src_width, u16 src_height,
					      u16 dst_width, u16 dst_height,
					      const struct gcn_drm_texture_vertex *vertices,
					      u32 triangle_count,
					      const struct gcn_drm_draw_state *state);
	int (*draw_indexed_triangles_rgb565)(void *dst_allocation,
					     u16 width, u16 height,
					     const struct gcn_drm_color_vertex *vertices,
					     u32 vertex_count, const u8 *indices,
					     u32 triangle_count,
					     const struct gcn_drm_draw_state *state);
	int (*draw_indexed_textured_triangles_rgb565)(void *src_allocation,
						      void *dst_allocation,
					     u16 src_width, u16 src_height,
					     u16 dst_width, u16 dst_height,
					     const struct gcn_drm_texture_vertex *vertices,
					     u32 vertex_count, const u8 *indices,
					     u32 triangle_count,
					     const struct gcn_drm_draw_state *state);
	int (*draw_itex_depth_rgb565)(void *src_allocation,
				      void *dst_allocation,
				      u16 src_width, u16 src_height,
				      u16 dst_width, u16 dst_height,
				      const struct gcn_drm_itex_depth_vertex *vertices,
				      u32 vertex_count, const u8 *indices,
				      u32 triangle_count,
				      const struct gcn_drm_draw_state *state,
				      const struct gcn_drm_depth_state *depth);
	int (*draw_fixed_rgb565)(void *src_allocation, void *dst_allocation,
				 u16 src_width, u16 src_height,
				 u16 dst_width, u16 dst_height,
				 const struct gcn_drm_fixed_vertex *vertices,
				 u32 vertex_count, const u8 *indices,
				 u32 triangle_count, u32 tev_mode,
				 const struct gcn_drm_draw_state *state,
				 const struct gcn_drm_depth_state *depth);
	int (*draw_fixed_system_rgb565)(void *src_allocation, void *dst,
					u16 src_width, u16 src_height,
					u16 dst_width, u16 dst_height,
					u32 dst_layout,
					const struct gcn_drm_fixed_vertex *vertices,
					u32 vertex_count, const u8 *indices,
					u32 triangle_count, u32 tev_mode,
					const struct gcn_drm_draw_state *state,
					const struct gcn_drm_depth_state *depth);
	int (*fill_system_rgb565)(void *dst, u16 width, u16 height,
				  u32 dst_layout, u16 x, u16 y,
				  u16 rect_width, u16 rect_height, u16 color);
};

int gcn_drm_register_accel_v10(const struct gcn_drm_accel_ops *ops);
void gcn_drm_unregister_accel_v10(const struct gcn_drm_accel_ops *ops);

#endif /* _LINUX_GCN_DRM_ACCEL_H */
