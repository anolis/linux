/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __GCN_DRM_INTERNAL_H__
#define __GCN_DRM_INTERNAL_H__

#include <linux/gcn_drm_accel.h>
#include <uapi/drm/gcn_drm.h>

struct drm_device;
struct drm_file;
struct drm_gem_object;
struct drm_mode_fb_cmd2;
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
int gcn_drm_provider_fill_system(void *dst, u16 width, u16 height,
				 u32 dst_layout, u16 x, u16 y,
				 u16 rect_width, u16 rect_height, u16 color);
int gcn_drm_provider_draw_triangle(const struct gcn_drm_accel_ops *provider,
				   void *dst_allocation,
				   u16 width, u16 height,
				   const struct gcn_drm_color_vertex vertices[3]);
int gcn_drm_provider_draw_triangles(const struct gcn_drm_accel_ops *provider,
				    void *dst_allocation,
				    u16 width, u16 height,
				    const struct gcn_drm_color_vertex *vertices,
				    u32 triangle_count);
int
gcn_drm_provider_draw_triangles_state(const struct gcn_drm_accel_ops *provider,
				      void *dst_allocation, u16 width, u16 height,
				      const struct gcn_drm_color_vertex *vertices,
				      u32 triangle_count,
				      const struct gcn_drm_draw_state *state);
int gcn_drm_provider_draw_depth(const struct gcn_drm_accel_ops *provider,
				void *dst_allocation, u16 width, u16 height,
				const struct gcn_drm_color_depth_vertex *vertices,
				u32 triangle_count,
				const struct gcn_drm_draw_state *state,
				const struct gcn_drm_depth_state *depth);
int gcn_drm_provider_draw_textured(const struct gcn_drm_accel_ops *provider,
				   void *src_allocation, void *dst_allocation,
				   u16 src_width, u16 src_height,
				   u16 dst_width, u16 dst_height,
				   const struct gcn_drm_texture_vertex *vertices,
				   u32 triangle_count,
				   const struct gcn_drm_draw_state *state);
int gcn_drm_provider_draw_indexed(const struct gcn_drm_accel_ops *provider,
				  void *dst_allocation, u16 width, u16 height,
				  const struct gcn_drm_color_vertex *vertices,
				  u32 vertex_count, const u8 *indices,
				  u32 triangle_count,
				  const struct gcn_drm_draw_state *state);
int gcn_drm_provider_draw_itex(const struct gcn_drm_accel_ops *provider,
			       void *src_allocation, void *dst_allocation,
				  u16 src_width, u16 src_height,
				  u16 dst_width, u16 dst_height,
				  const struct gcn_drm_texture_vertex *vertices,
				  u32 vertex_count, const u8 *indices,
				  u32 triangle_count,
				  const struct gcn_drm_draw_state *state);
int
gcn_drm_provider_draw_itex_depth(const struct gcn_drm_accel_ops *provider,
				 void *src_allocation, void *dst_allocation,
				 u16 src_width, u16 src_height,
				 u16 dst_width, u16 dst_height,
				 const struct gcn_drm_itex_depth_vertex *vertices,
				 u32 vertex_count, const u8 *indices,
				 u32 triangle_count,
				 const struct gcn_drm_draw_state *state,
				 const struct gcn_drm_depth_state *depth);
int gcn_drm_provider_draw_fixed(const struct gcn_drm_accel_ops *provider,
				void *src_allocation, void *dst_allocation,
				u32 src_format,
				u16 src_width, u16 src_height,
				u16 dst_width, u16 dst_height,
				const struct gcn_drm_fixed_vertex *vertices,
				u32 vertex_count, const u8 *indices,
				u32 triangle_count, u32 tev_mode,
				const struct gcn_drm_draw_state *state,
				const struct gcn_drm_depth_state *depth);
int
gcn_drm_provider_draw_fixed_system(const struct gcn_drm_accel_ops *provider,
				   void *src_allocation, void *dst,
				   u32 src_format,
				   u16 src_width, u16 src_height,
				   u16 dst_width, u16 dst_height,
				   u32 dst_layout,
				   const struct gcn_drm_fixed_vertex *vertices,
				   u32 vertex_count, const u8 *indices,
				   u32 triangle_count, u32 tev_mode,
				   const struct gcn_drm_draw_state *state,
				   const struct gcn_drm_depth_state *depth);
int gcn_drm_provider_blit_rect(const struct gcn_drm_accel_ops *provider,
			       void *src_allocation, void *dst_allocation,
			       u16 src_width, u16 src_height, u16 dst_width,
			       u16 dst_height, u16 src_x, u16 src_y, u16 dst_x,
			       u16 dst_y, u16 rect_width, u16 rect_height);
int gcn_drm_provider_blit_scaled(const struct gcn_drm_accel_ops *provider,
				 void *src_allocation, void *dst_allocation,
				 u16 src_width, u16 src_height, u16 dst_width,
				 u16 dst_height,
				 const struct drm_gcn_blit_scaled *args);
int gcn_drm_provider_blit_scaled_system(const void *src, void *dst,
					u16 src_width, u16 src_height,
					u16 dst_width, u16 dst_height,
					u32 src_format, u32 dst_format,
					u32 src_layout, u32 dst_layout,
					const struct drm_gcn_blit_scaled *args);

int gcn_drm_render_open(struct drm_device *drm, struct drm_file *file);
void gcn_drm_render_postclose(struct drm_device *drm, struct drm_file *file);
struct drm_gem_object *
gcn_drm_render_create_object(struct drm_device *drm, size_t size);
int gcn_drm_render_validate_framebuffer(struct drm_file *file,
					const struct drm_mode_fb_cmd2 *mode_cmd);

extern const struct drm_ioctl_desc
gcn_drm_render_ioctls[DRM_GCN_NUM_IOCTLS];

#endif /* __GCN_DRM_INTERNAL_H__ */
