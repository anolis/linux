/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __GCN_DRM_RENDER_H__
#define __GCN_DRM_RENDER_H__

#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/overflow.h>
#include <linux/types.h>

#include <drm/drm_fourcc.h>
#include <uapi/drm/gcn_drm.h>
#include <uapi/drm/drm_mode.h>

#define GCN_DRM_RENDER_MAX_WIDTH	640U
#define GCN_DRM_RENDER_MAX_HEIGHT	576U

struct gcn_drm_render_rect {
	u16 x;
	u16 y;
	u16 width;
	u16 height;
	u16 color;
};

struct gcn_drm_render_blit_rect {
	u16 src_x;
	u16 src_y;
	u16 dst_x;
	u16 dst_y;
	u16 width;
	u16 height;
};

static inline int
gcn_drm_render_validate_fb(u32 width, u32 height, u32 format, u32 layout,
			   const struct drm_mode_fb_cmd2 *mode_cmd)
{
	if (!mode_cmd || layout != DRM_GCN_GEM_LAYOUT_LINEAR ||
	    format != DRM_GCN_GEM_FORMAT_RGB565 ||
	    mode_cmd->pixel_format != DRM_FORMAT_RGB565 ||
	    mode_cmd->width != width || mode_cmd->height != height ||
	    mode_cmd->pitches[0] != width * sizeof(u16) ||
	    mode_cmd->offsets[0] || mode_cmd->modifier[0] != DRM_FORMAT_MOD_LINEAR)
		return -EINVAL;

	return 0;
}

static inline int
gcn_drm_render_validate_scaled(const struct drm_gcn_blit_scaled *args,
			       u16 src_width, u16 src_height,
			       u16 dst_width, u16 dst_height)
{
	if (!args || !args->ctx_id || !args->src_handle || !args->dst_handle ||
	    args->flags || args->pad || !args->src_width || !args->src_height ||
	    !args->dst_width || !args->dst_height || args->src_x >= src_width ||
	    args->src_y >= src_height || args->src_width > src_width - args->src_x ||
	    args->src_height > src_height - args->src_y ||
	    args->dst_x >= dst_width || args->dst_y >= dst_height ||
	    args->dst_width > dst_width - args->dst_x ||
	    args->dst_height > dst_height - args->dst_y)
		return -EINVAL;

	return 0;
}

static inline int
gcn_drm_render_validate_color_triangle_alpha(const struct drm_gcn_color_vertex vertices[3],
					     u16 dst_width, u16 dst_height,
					     bool require_opaque)
{
	s64 area;
	unsigned int i;

	if (!vertices)
		return -EINVAL;

	for (i = 0; i < 3; i++) {
		const struct drm_gcn_color_vertex *vertex = &vertices[i];

		if (vertex->x > dst_width || vertex->y > dst_height ||
		    (require_opaque && (vertex->rgba & 0xff) != 0xff))
			return -EINVAL;
	}

	area = (s64)(vertices[1].x - vertices[0].x) *
		(vertices[2].y - vertices[0].y) -
		(s64)(vertices[2].x - vertices[0].x) *
		(vertices[1].y - vertices[0].y);
	return area ? 0 : -EINVAL;
}

static inline int
gcn_drm_render_validate_color_triangle(const struct drm_gcn_color_vertex vertices[3],
				       u16 dst_width, u16 dst_height)
{
	return gcn_drm_render_validate_color_triangle_alpha(vertices, dst_width,
							    dst_height, true);
}

static inline int
gcn_drm_render_validate_triangle(const struct drm_gcn_draw_triangle *args,
				 u16 dst_width, u16 dst_height)
{
	if (!args || !args->ctx_id || !args->dst_handle || args->flags ||
	    args->pad[0] || args->pad[1])
		return -EINVAL;

	return gcn_drm_render_validate_color_triangle(args->vertices, dst_width,
						      dst_height);
}

static inline int
gcn_drm_render_validate_triangle_batch(const struct drm_gcn_draw_triangles *args)
{
	if (!args || !args->ctx_id || !args->dst_handle || args->flags ||
	    !args->triangle_count ||
	    args->triangle_count > DRM_GCN_MAX_TRIANGLES || args->pad0 ||
	    !args->triangles_ptr || args->pad[0] || args->pad[1])
		return -EINVAL;

	return 0;
}

static inline int
gcn_drm_render_validate_triangle_state_batch(const struct drm_gcn_draw_triangles_state *args)
{
	if (!args || !args->ctx_id || !args->dst_handle || args->flags ||
	    !args->triangle_count ||
	    args->triangle_count > DRM_GCN_MAX_TRIANGLES || args->pad0 ||
	    !args->triangles_ptr ||
	    args->state.cull_mode > DRM_GCN_CULL_ALL || args->pad1 ||
	    args->state.blend_mode > DRM_GCN_BLEND_SRC_ALPHA)
		return -EINVAL;

	return 0;
}

static inline int
gcn_drm_render_validate_triangle_depth_batch(const struct drm_gcn_draw_triangles_depth *args)
{
	if (!args || !args->ctx_id || !args->dst_handle || args->flags ||
	    !args->triangle_count ||
	    args->triangle_count > DRM_GCN_MAX_TRIANGLES || args->pad0 ||
	    !args->triangles_ptr ||
	    args->state.cull_mode > DRM_GCN_CULL_ALL || args->depth.pad ||
	    args->pad1 || args->state.blend_mode > DRM_GCN_BLEND_SRC_ALPHA ||
	    args->depth.test_enable > 1 || args->depth.write_enable > 1 ||
	    args->depth.compare > DRM_GCN_DEPTH_ALWAYS)
		return -EINVAL;

	return 0;
}

static inline int
gcn_drm_render_validate_textured_triangle_batch(const struct drm_gcn_draw_textured_triangles *args)
{
	if (!args || !args->ctx_id || !args->src_handle || !args->dst_handle ||
	    args->src_handle == args->dst_handle || args->flags ||
	    !args->triangle_count ||
	    args->triangle_count > DRM_GCN_MAX_TRIANGLES ||
	    !args->triangles_ptr ||
	    args->state.cull_mode > DRM_GCN_CULL_ALL || args->pad ||
	    args->state.blend_mode != DRM_GCN_BLEND_NONE)
		return -EINVAL;

	return 0;
}

static inline int
gcn_drm_validate_texture_triangle(const struct drm_gcn_texture_vertex vertices[3],
				  u16 src_width, u16 src_height,
				  u16 dst_width, u16 dst_height)
{
	s64 area;
	unsigned int i;

	if (!vertices)
		return -EINVAL;

	for (i = 0; i < 3; i++) {
		if (vertices[i].x > dst_width || vertices[i].y > dst_height ||
		    vertices[i].s > src_width || vertices[i].t > src_height)
			return -EINVAL;
	}

	area = (s64)(vertices[1].x - vertices[0].x) *
		(vertices[2].y - vertices[0].y) -
		(s64)(vertices[2].x - vertices[0].x) *
		(vertices[1].y - vertices[0].y);
	return area ? 0 : -EINVAL;
}

static inline int
gcn_drm_render_validate_indexed_triangle_batch(const struct drm_gcn_draw_indexed_triangles *args)
{
	if (!args || !args->ctx_id || !args->dst_handle || args->flags ||
	    args->vertex_count < 3 ||
	    args->vertex_count > DRM_GCN_MAX_VERTICES ||
	    !args->triangle_count ||
	    args->triangle_count > DRM_GCN_MAX_TRIANGLES ||
	    !args->vertices_ptr || !args->indices_ptr ||
	    args->state.cull_mode > DRM_GCN_CULL_ALL ||
	    args->pad || args->state.blend_mode > DRM_GCN_BLEND_SRC_ALPHA)
		return -EINVAL;

	return 0;
}

static inline int
gcn_drm_validate_indexed_triangles(const struct drm_gcn_color_vertex *vertices,
				   u32 vertex_count, const u16 *indices,
				   u32 triangle_count, u16 dst_width,
				   u16 dst_height, bool require_opaque)
{
	unsigned int index_count = triangle_count * 3;
	unsigned int i;

	if (!vertices || !indices)
		return -EINVAL;

	for (i = 0; i < vertex_count; i++) {
		if (vertices[i].x > dst_width || vertices[i].y > dst_height ||
		    (require_opaque && (vertices[i].rgba & 0xff) != 0xff))
			return -EINVAL;
	}
	for (i = 0; i < index_count; i++) {
		if (indices[i] >= vertex_count)
			return -EINVAL;
	}
	for (i = 0; i < index_count; i += 3) {
		const struct drm_gcn_color_vertex *a = &vertices[indices[i]];
		const struct drm_gcn_color_vertex *b = &vertices[indices[i + 1]];
		const struct drm_gcn_color_vertex *c = &vertices[indices[i + 2]];
		s64 area = (s64)(b->x - a->x) * (c->y - a->y) -
			   (s64)(c->x - a->x) * (b->y - a->y);

		if (!area)
			return -EINVAL;
	}

	return 0;
}

static inline int
gcn_drm_itex_args(const struct drm_gcn_draw_indexed_textured *args)
{
	if (!args || !args->ctx_id || !args->src_handle || !args->dst_handle ||
	    args->src_handle == args->dst_handle || args->flags ||
	    args->vertex_count < 3 ||
	    args->vertex_count > DRM_GCN_MAX_VERTICES ||
	    !args->triangle_count ||
	    args->triangle_count > DRM_GCN_MAX_TRIANGLES || args->pad0 ||
	    !args->vertices_ptr || !args->indices_ptr ||
	    args->state.cull_mode > DRM_GCN_CULL_ALL ||
	    args->pad1 || args->state.blend_mode != DRM_GCN_BLEND_NONE)
		return -EINVAL;

	return 0;
}

static inline int
gcn_drm_itex_vertices(const struct drm_gcn_texture_vertex *vertices,
		      u32 vertex_count,
		const u16 *indices, u32 triangle_count, u16 src_width,
		u16 src_height, u16 dst_width, u16 dst_height)
{
	unsigned int index_count = triangle_count * 3;
	unsigned int i;

	if (!vertices || !indices)
		return -EINVAL;

	for (i = 0; i < vertex_count; i++) {
		if (vertices[i].x > dst_width || vertices[i].y > dst_height ||
		    vertices[i].s > src_width || vertices[i].t > src_height)
			return -EINVAL;
	}
	for (i = 0; i < index_count; i++) {
		if (indices[i] >= vertex_count)
			return -EINVAL;
	}
	for (i = 0; i < index_count; i += 3) {
		const struct drm_gcn_texture_vertex *a = &vertices[indices[i]];
		const struct drm_gcn_texture_vertex *b = &vertices[indices[i + 1]];
		const struct drm_gcn_texture_vertex *c = &vertices[indices[i + 2]];
		s64 area = (s64)(b->x - a->x) * (c->y - a->y) -
			   (s64)(c->x - a->x) * (b->y - a->y);

		if (!area)
			return -EINVAL;
	}

	return 0;
}

static inline int
gcn_drm_itex_z_args(const struct drm_gcn_draw_indexed_textured_depth *args)
{
	if (!args || !args->ctx_id || !args->src_handle || !args->dst_handle ||
	    args->src_handle == args->dst_handle || args->flags ||
	    args->vertex_count < 3 ||
	    args->vertex_count > DRM_GCN_MAX_VERTICES ||
	    !args->triangle_count ||
	    args->triangle_count > DRM_GCN_MAX_TRIANGLES || args->pad0 ||
	    !args->vertices_ptr || !args->indices_ptr ||
	    args->state.cull_mode > DRM_GCN_CULL_ALL ||
	    args->depth.pad || args->pad1 ||
	    args->state.blend_mode != DRM_GCN_BLEND_NONE ||
	    args->depth.test_enable > 1 || args->depth.write_enable > 1 ||
	    args->depth.compare > DRM_GCN_DEPTH_ALWAYS)
		return -EINVAL;

	return 0;
}

static inline int
gcn_drm_itex_z_vertices(const struct drm_gcn_texture_depth_vertex *vertices,
			u32 vertex_count, const u16 *indices,
			u32 triangle_count, u16 src_width, u16 src_height,
			u16 dst_width, u16 dst_height)
{
	unsigned int index_count = triangle_count * 3;
	unsigned int i;

	if (!vertices || !indices)
		return -EINVAL;

	for (i = 0; i < vertex_count; i++) {
		if (vertices[i].x > dst_width || vertices[i].y > dst_height ||
		    vertices[i].z > DRM_GCN_DEPTH_MAX ||
		    vertices[i].s > src_width || vertices[i].t > src_height)
			return -EINVAL;
	}
	for (i = 0; i < index_count; i++) {
		if (indices[i] >= vertex_count)
			return -EINVAL;
	}
	for (i = 0; i < index_count; i += 3) {
		const struct drm_gcn_texture_depth_vertex *a;
		const struct drm_gcn_texture_depth_vertex *b;
		const struct drm_gcn_texture_depth_vertex *c;
		s64 area;

		a = &vertices[indices[i]];
		b = &vertices[indices[i + 1]];
		c = &vertices[indices[i + 2]];
		area = (s64)(b->x - a->x) * (c->y - a->y) -
		       (s64)(c->x - a->x) * (b->y - a->y);
		if (!area)
			return -EINVAL;
	}

	return 0;
}

static inline int
gcn_drm_fixed_args(const struct drm_gcn_draw_indexed_fixed *args)
{
	if (!args || !args->ctx_id || !args->dst_handle || args->flags ||
	    args->vertex_count < 3 ||
	    args->vertex_count > DRM_GCN_MAX_VERTICES ||
	    !args->triangle_count ||
	    args->triangle_count > DRM_GCN_MAX_TRIANGLES ||
	    !args->vertices_ptr || !args->indices_ptr ||
	    args->state.cull_mode > DRM_GCN_CULL_ALL ||
	    args->depth.pad || args->pad ||
	    args->tev_mode > DRM_GCN_TEV_MODULATE ||
	    args->state.blend_mode > DRM_GCN_BLEND_SRC_ALPHA ||
	    args->depth.test_enable > 1 || args->depth.write_enable > 1 ||
	    args->depth.compare > DRM_GCN_DEPTH_ALWAYS)
		return -EINVAL;

	if (args->tev_mode == DRM_GCN_TEV_PASS_COLOR) {
		if (args->src_handle)
			return -EINVAL;
	} else if (!args->src_handle || args->src_handle == args->dst_handle) {
		return -EINVAL;
	}
	if (args->tev_mode == DRM_GCN_TEV_REPLACE_TEXTURE &&
	    args->state.blend_mode != DRM_GCN_BLEND_NONE)
		return -EINVAL;

	return 0;
}

static inline int
gcn_drm_fixed_vertices(const struct drm_gcn_fixed_vertex *vertices,
		       u32 vertex_count, const u16 *indices,
		       u32 triangle_count, u32 tev_mode,
		       u16 src_width, u16 src_height,
		       u16 dst_width, u16 dst_height,
		       bool require_opaque)
{
	unsigned int index_count = triangle_count * 3;
	unsigned int i;

	if (!vertices || !indices)
		return -EINVAL;

	for (i = 0; i < vertex_count; i++) {
		if (vertices[i].x > dst_width || vertices[i].y > dst_height ||
		    vertices[i].z > DRM_GCN_DEPTH_MAX)
			return -EINVAL;
		if (tev_mode == DRM_GCN_TEV_PASS_COLOR) {
			if (vertices[i].s || vertices[i].t)
				return -EINVAL;
		} else if (vertices[i].s > src_width ||
			   vertices[i].t > src_height) {
			return -EINVAL;
		}
		if (tev_mode != DRM_GCN_TEV_REPLACE_TEXTURE && require_opaque &&
		    (vertices[i].rgba & 0xff) != 0xff)
			return -EINVAL;
	}
	for (i = 0; i < index_count; i++) {
		if (indices[i] >= vertex_count)
			return -EINVAL;
	}
	for (i = 0; i < index_count; i += 3) {
		const struct drm_gcn_fixed_vertex *a = &vertices[indices[i]];
		const struct drm_gcn_fixed_vertex *b = &vertices[indices[i + 1]];
		const struct drm_gcn_fixed_vertex *c = &vertices[indices[i + 2]];
		s64 area = (s64)(b->x - a->x) * (c->y - a->y) -
			   (s64)(c->x - a->x) * (b->y - a->y);

		if (!area)
			return -EINVAL;
	}

	return 0;
}

static inline int gcn_drm_validate_z(const struct drm_gcn_color_depth_vertex vertices[3],
				     u16 dst_width, u16 dst_height,
				     bool require_opaque)
{
	s64 area;
	unsigned int i;

	if (!vertices)
		return -EINVAL;

	for (i = 0; i < 3; i++) {
		if (vertices[i].x > dst_width || vertices[i].y > dst_height ||
		    vertices[i].z > DRM_GCN_DEPTH_MAX ||
		    (require_opaque && (vertices[i].rgba & 0xff) != 0xff))
			return -EINVAL;
	}

	area = (s64)(vertices[1].x - vertices[0].x) *
		(vertices[2].y - vertices[0].y) -
		(s64)(vertices[2].x - vertices[0].x) *
		(vertices[1].y - vertices[0].y);
	return area ? 0 : -EINVAL;
}

static inline int
gcn_drm_render_validate_draw_state(const struct drm_gcn_draw_state *state,
				   u16 dst_width, u16 dst_height)
{
	if (!state || !state->viewport_width || !state->viewport_height ||
	    state->viewport_x >= dst_width || state->viewport_y >= dst_height ||
	    state->viewport_width > dst_width - state->viewport_x ||
	    state->viewport_height > dst_height - state->viewport_y ||
	    !state->scissor_width || !state->scissor_height ||
	    state->scissor_x >= dst_width || state->scissor_y >= dst_height ||
	    state->scissor_width > dst_width - state->scissor_x ||
	    state->scissor_height > dst_height - state->scissor_y)
		return -EINVAL;

	return 0;
}

static inline int
gcn_drm_valid_system_formats(u32 src_format, u32 src_layout,
			     u32 dst_format, u32 dst_layout,
			     bool same_object)
{
	if (dst_format != DRM_GCN_GEM_FORMAT_RGB565 ||
	    (dst_layout != DRM_GCN_GEM_LAYOUT_TILED_4X4 &&
	     dst_layout != DRM_GCN_GEM_LAYOUT_LINEAR))
		return -EINVAL;

	if (src_format == DRM_GCN_GEM_FORMAT_RGB565 &&
	    (src_layout == DRM_GCN_GEM_LAYOUT_TILED_4X4 ||
	     src_layout == DRM_GCN_GEM_LAYOUT_LINEAR))
		return 0;

	if (src_format == DRM_GCN_GEM_FORMAT_XRGB8888 &&
	    src_layout == DRM_GCN_GEM_LAYOUT_LINEAR && !same_object)
		return 0;

	return -EINVAL;
}

static inline void
gcn_drm_render_decode_blit_rect(u64 data,
				struct gcn_drm_render_blit_rect *rect)
{
	rect->src_x = data & DRM_GCN_BLIT_FIELD_MASK;
	rect->src_y = (data >> DRM_GCN_BLIT_SRC_Y_SHIFT) &
		      DRM_GCN_BLIT_FIELD_MASK;
	rect->dst_x = (data >> DRM_GCN_BLIT_DST_X_SHIFT) &
		      DRM_GCN_BLIT_FIELD_MASK;
	rect->dst_y = (data >> DRM_GCN_BLIT_DST_Y_SHIFT) &
		      DRM_GCN_BLIT_FIELD_MASK;
	rect->width = ((data >> DRM_GCN_BLIT_WIDTH_SHIFT) &
		       DRM_GCN_BLIT_FIELD_MASK) + 1;
	rect->height = ((data >> DRM_GCN_BLIT_HEIGHT_SHIFT) &
			DRM_GCN_BLIT_FIELD_MASK) + 1;
}

static inline int
gcn_drm_render_validate_blit_rect(u64 data, u16 src_width, u16 src_height,
				  u16 dst_width, u16 dst_height)
{
	struct gcn_drm_render_blit_rect rect;

	if (data & DRM_GCN_BLIT_RESERVED_MASK)
		return -EINVAL;

	gcn_drm_render_decode_blit_rect(data, &rect);
	if (rect.src_x >= src_width || rect.src_y >= src_height ||
	    rect.width > src_width - rect.src_x ||
	    rect.height > src_height - rect.src_y ||
	    rect.dst_x >= dst_width || rect.dst_y >= dst_height ||
	    rect.width > dst_width - rect.dst_x ||
	    rect.height > dst_height - rect.dst_y)
		return -EINVAL;

	return 0;
}

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
	u64 bytes_per_pixel;
	u64 bytes;

	if (!args || !size ||
	    (args->flags & ~DRM_GCN_GEM_CREATE_SYSTEM) ||
	    !args->width || !args->height ||
	    args->width > GCN_DRM_RENDER_MAX_WIDTH ||
	    args->height > GCN_DRM_RENDER_MAX_HEIGHT ||
	    (args->width & 3) || (args->height & 3) ||
	    (args->layout != DRM_GCN_GEM_LAYOUT_TILED_4X4 &&
	     args->layout != DRM_GCN_GEM_LAYOUT_LINEAR))
		return -EINVAL;

	if (args->format == DRM_GCN_GEM_FORMAT_RGB565) {
		if (args->layout == DRM_GCN_GEM_LAYOUT_LINEAR &&
		    !(args->flags & DRM_GCN_GEM_CREATE_SYSTEM))
			return -EINVAL;
		bytes_per_pixel = sizeof(u16);
	} else if (args->format == DRM_GCN_GEM_FORMAT_XRGB8888) {
		if (!(args->flags & DRM_GCN_GEM_CREATE_SYSTEM) ||
		    args->layout != DRM_GCN_GEM_LAYOUT_LINEAR)
			return -EINVAL;
		bytes_per_pixel = sizeof(u32);
	} else {
		return -EINVAL;
	}

	if (check_mul_overflow((u64)args->width, (u64)args->height, &bytes) ||
	    check_mul_overflow(bytes, bytes_per_pixel, &bytes))
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
	case DRM_GCN_RENDER_OP_BLIT_RECT_RGB565:
		if (!args->src_handle ||
		    args->data & DRM_GCN_BLIT_RESERVED_MASK)
			return -EINVAL;
		return 0;
	default:
		return -EINVAL;
	}
}

#endif /* __GCN_DRM_RENDER_H__ */
