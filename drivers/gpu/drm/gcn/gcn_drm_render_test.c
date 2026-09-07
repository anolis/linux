// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>

#include <uapi/drm/gcn_drm.h>

#include "gcn_drm_render.h"

static void gcn_drm_render_uapi_layout(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, sizeof(struct drm_gcn_get_param), 16U);
	KUNIT_EXPECT_EQ(test, sizeof(struct drm_gcn_gem_create), 32U);
	KUNIT_EXPECT_EQ(test, sizeof(struct drm_gcn_gem_mmap), 16U);
	KUNIT_EXPECT_EQ(test, sizeof(struct drm_gcn_ctx_create), 8U);
	KUNIT_EXPECT_EQ(test, sizeof(struct drm_gcn_ctx_free), 8U);
	KUNIT_EXPECT_EQ(test, sizeof(struct drm_gcn_wait), 16U);
	KUNIT_EXPECT_EQ(test, sizeof(struct drm_gcn_submit), 32U);
	KUNIT_EXPECT_EQ(test, sizeof(struct drm_gcn_blit_scaled), 40U);
	KUNIT_EXPECT_EQ(test, sizeof(struct drm_gcn_color_vertex), 8U);
	KUNIT_EXPECT_EQ(test, sizeof(struct drm_gcn_color_triangle), 24U);
	KUNIT_EXPECT_EQ(test, sizeof(struct drm_gcn_draw_triangle), 48U);
	KUNIT_EXPECT_EQ(test, sizeof(struct drm_gcn_draw_triangles), 48U);
	KUNIT_EXPECT_EQ(test, sizeof(struct drm_gcn_draw_state), 24U);
	KUNIT_EXPECT_EQ(test, sizeof(struct drm_gcn_draw_triangles_state), 64U);
	KUNIT_EXPECT_EQ(test, sizeof(struct drm_gcn_color_depth_vertex), 12U);
	KUNIT_EXPECT_EQ(test, sizeof(struct drm_gcn_color_depth_triangle), 36U);
	KUNIT_EXPECT_EQ(test, sizeof(struct drm_gcn_depth_state), 16U);
	KUNIT_EXPECT_EQ(test, sizeof(struct drm_gcn_draw_triangles_depth), 80U);
	KUNIT_EXPECT_EQ(test, sizeof(struct drm_gcn_texture_vertex), 8U);
	KUNIT_EXPECT_EQ(test, sizeof(struct drm_gcn_texture_triangle), 24U);
	KUNIT_EXPECT_EQ(test, sizeof(struct drm_gcn_draw_textured_triangles), 64U);
	KUNIT_EXPECT_EQ(test, sizeof(struct drm_gcn_draw_indexed_triangles), 72U);
	KUNIT_EXPECT_EQ(test,
			sizeof(struct drm_gcn_draw_indexed_textured), 80U);
	KUNIT_EXPECT_EQ(test, sizeof(struct drm_gcn_texture_depth_vertex), 12U);
	KUNIT_EXPECT_EQ(test,
			sizeof(struct drm_gcn_draw_indexed_textured_depth), 96U);
	KUNIT_EXPECT_EQ(test, sizeof(struct drm_gcn_fixed_vertex), 16U);
	KUNIT_EXPECT_EQ(test, sizeof(struct drm_gcn_draw_indexed_fixed), 96U);
	KUNIT_EXPECT_EQ(test, DRM_GCN_NUM_IOCTLS, 17);
	KUNIT_EXPECT_EQ(test, DRM_GCN_PARAM_FEATURES, 9);
	KUNIT_EXPECT_EQ(test,
			DRM_GCN_FEATURE_BLIT_RECT_RGB565_UNEQUAL_DIMS,
			1ULL << 8);
	KUNIT_EXPECT_EQ(test,
			DRM_GCN_FEATURE_BLIT_RECT_RGB565_SAME_OBJECT,
			1ULL << 9);
	KUNIT_EXPECT_EQ(test, DRM_GCN_FEATURE_BLIT_SCALED_RGB565, 1ULL << 10);
	KUNIT_EXPECT_EQ(test, DRM_GCN_FEATURE_SYSTEM_GEM, 1ULL << 11);
	KUNIT_EXPECT_EQ(test, DRM_GCN_FEATURE_BLIT_SCALED_SYSTEM_RGB565,
			1ULL << 12);
	KUNIT_EXPECT_EQ(test, DRM_GCN_FEATURE_SYSTEM_GEM_LINEAR, 1ULL << 13);
	KUNIT_EXPECT_EQ(test,
			DRM_GCN_FEATURE_BLIT_SCALED_SYSTEM_XRGB8888_TO_RGB565,
			1ULL << 14);
	KUNIT_EXPECT_EQ(test, DRM_GCN_FEATURE_DRAW_TRIANGLE_RGB565,
			1ULL << 15);
	KUNIT_EXPECT_EQ(test, DRM_GCN_FEATURE_DRAW_TRIANGLES_RGB565,
			1ULL << 16);
	KUNIT_EXPECT_EQ(test, DRM_GCN_FEATURE_DRAW_TRIANGLES_STATE_RGB565,
			1ULL << 17);
	KUNIT_EXPECT_EQ(test, DRM_GCN_FEATURE_DRAW_TRIANGLES_DEPTH_RGB565,
			1ULL << 18);
	KUNIT_EXPECT_EQ(test, DRM_GCN_FEATURE_DRAW_TEXTURED_TRIANGLES_RGB565,
			1ULL << 19);
	KUNIT_EXPECT_EQ(test, DRM_GCN_FEATURE_DRAW_INDEXED_TRIANGLES_RGB565,
			1ULL << 20);
	KUNIT_EXPECT_EQ(test,
			DRM_GCN_FEATURE_DRAW_INDEXED_TEXTURED_TRIANGLES_RGB565,
			1ULL << 21);
	KUNIT_EXPECT_EQ(test,
			DRM_GCN_FEATURE_DRAW_INDEXED_TEXTURED_DEPTH_RGB565,
			1ULL << 22);
	KUNIT_EXPECT_EQ(test, DRM_GCN_FEATURE_DRAW_INDEXED_FIXED_RGB565,
			1ULL << 23);
	KUNIT_EXPECT_EQ(test, DRM_GCN_FEATURE_RASTER_CULL, 1ULL << 24);
	KUNIT_EXPECT_EQ(test, DRM_GCN_FEATURE_SYSTEM_RENDER_RGB565, 1ULL << 25);
	KUNIT_EXPECT_EQ(test, DRM_GCN_FEATURE_TEXTURE_RGBA8, 1ULL << 26);
	KUNIT_EXPECT_EQ(test, DRM_GCN_FEATURE_TEXTURE_LINEAR, 1ULL << 27);
}

static void gcn_drm_render_validates_color_triangle(struct kunit *test)
{
	struct drm_gcn_draw_triangle args = {
		.ctx_id = 1,
		.dst_handle = 2,
		.vertices = {
			{ 0, 0, DRM_GCN_RGBA8(0xff, 0, 0, 0xff) },
			{ 640, 0, DRM_GCN_RGBA8(0, 0xff, 0, 0xff) },
			{ 320, 480, DRM_GCN_RGBA8(0, 0, 0xff, 0xff) },
		},
	};

	KUNIT_EXPECT_EQ(test,
			gcn_drm_render_validate_triangle(&args, 640, 480), 0);
	args.vertices[1].x = 641;
	KUNIT_EXPECT_EQ(test,
			gcn_drm_render_validate_triangle(&args, 640, 480),
			-EINVAL);
	args.vertices[1].x = 640;
	args.vertices[2].rgba &= ~0xffU;
	KUNIT_EXPECT_EQ(test,
			gcn_drm_render_validate_triangle(&args, 640, 480),
			-EINVAL);
	args.vertices[2].rgba |= 0xff;
	args.vertices[2].x = 0;
	args.vertices[2].y = 0;
	KUNIT_EXPECT_EQ(test,
			gcn_drm_render_validate_triangle(&args, 640, 480),
			-EINVAL);
	args.vertices[2].x = 320;
	args.vertices[2].y = 480;
	args.flags = 1;
	KUNIT_EXPECT_EQ(test,
			gcn_drm_render_validate_triangle(&args, 640, 480),
			-EINVAL);
	args.flags = 0;
	args.pad[1] = 1;
	KUNIT_EXPECT_EQ(test,
			gcn_drm_render_validate_triangle(&args, 640, 480),
			-EINVAL);
}

static void gcn_drm_render_validates_triangle_batch(struct kunit *test)
{
	struct drm_gcn_draw_triangles args = {
		.ctx_id = 1,
		.dst_handle = 2,
		.triangle_count = 2,
		.triangles_ptr = 0x1000,
	};
	struct drm_gcn_color_triangle triangle = {
		.vertices = {
			{ 0, 0, DRM_GCN_RGBA8(0xff, 0, 0, 0xff) },
			{ 640, 0, DRM_GCN_RGBA8(0, 0xff, 0, 0xff) },
			{ 320, 480, DRM_GCN_RGBA8(0, 0, 0xff, 0xff) },
		},
	};
	int ret;

	KUNIT_EXPECT_EQ(test, gcn_drm_render_validate_triangle_batch(&args), 0);
	ret = gcn_drm_render_validate_color_triangle(triangle.vertices, 640, 480);
	KUNIT_EXPECT_EQ(test, ret, 0);
	args.triangle_count = 0;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_validate_triangle_batch(&args),
			-EINVAL);
	args.triangle_count = DRM_GCN_MAX_TRIANGLES + 1;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_validate_triangle_batch(&args),
			-EINVAL);
	args.triangle_count = 2;
	args.triangles_ptr = 0;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_validate_triangle_batch(&args),
			-EINVAL);
	args.triangles_ptr = 0x1000;
	args.flags = 1;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_validate_triangle_batch(&args),
			-EINVAL);
	args.flags = 0;
	args.pad[1] = 1;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_validate_triangle_batch(&args),
			-EINVAL);
	triangle.vertices[2] = triangle.vertices[0];
	ret = gcn_drm_render_validate_color_triangle(triangle.vertices, 640, 480);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void gcn_drm_render_validates_triangle_state_batch(struct kunit *test)
{
	struct drm_gcn_draw_triangles_state args = {
		.ctx_id = 1,
		.dst_handle = 2,
		.triangle_count = 1,
		.triangles_ptr = 0x1000,
		.state = {
			.viewport_x = 13,
			.viewport_y = 17,
			.viewport_width = 319,
			.viewport_height = 239,
			.scissor_x = 19,
			.scissor_y = 23,
			.scissor_width = 101,
			.scissor_height = 79,
			.blend_mode = DRM_GCN_BLEND_SRC_ALPHA,
		},
	};
	struct drm_gcn_color_triangle triangle = {
		.vertices = {
			{ 0, 0, DRM_GCN_RGBA8(0xff, 0, 0, 0x80) },
			{ 640, 0, DRM_GCN_RGBA8(0, 0xff, 0, 0x80) },
			{ 320, 480, DRM_GCN_RGBA8(0, 0, 0xff, 0x80) },
		},
	};
	int ret;

	ret = gcn_drm_render_validate_triangle_state_batch(&args);
	KUNIT_EXPECT_EQ(test, ret, 0);
	ret = gcn_drm_render_validate_draw_state(&args.state, 640, 480);
	KUNIT_EXPECT_EQ(test, ret, 0);
	ret = gcn_drm_render_validate_color_triangle_alpha(triangle.vertices, 640,
							   480, false);
	KUNIT_EXPECT_EQ(test, ret, 0);
	ret = gcn_drm_render_validate_color_triangle_alpha(triangle.vertices, 640,
							   480, true);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	args.state.viewport_width = 0;
	ret = gcn_drm_render_validate_draw_state(&args.state, 640, 480);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.state.viewport_width = 628;
	ret = gcn_drm_render_validate_draw_state(&args.state, 640, 480);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.state.viewport_x = 12;
	ret = gcn_drm_render_validate_draw_state(&args.state, 640, 480);
	KUNIT_EXPECT_EQ(test, ret, 0);
	args.state.scissor_height = 458;
	ret = gcn_drm_render_validate_draw_state(&args.state, 640, 480);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.state.scissor_height = 79;

	args.state.blend_mode = DRM_GCN_BLEND_SRC_ALPHA + 1;
	ret = gcn_drm_render_validate_triangle_state_batch(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.state.blend_mode = DRM_GCN_BLEND_NONE;
	args.state.cull_mode = DRM_GCN_CULL_ALL;
	ret = gcn_drm_render_validate_triangle_state_batch(&args);
	KUNIT_EXPECT_EQ(test, ret, 0);
	args.state.cull_mode = DRM_GCN_CULL_ALL + 1;
	ret = gcn_drm_render_validate_triangle_state_batch(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.state.cull_mode = DRM_GCN_CULL_NONE;
	args.pad1 = 1;
	ret = gcn_drm_render_validate_triangle_state_batch(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void gcn_drm_render_validates_triangle_depth_batch(struct kunit *test)
{
	struct drm_gcn_draw_triangles_depth args = {
		.ctx_id = 1,
		.dst_handle = 2,
		.triangle_count = 1,
		.triangles_ptr = 0x1000,
		.state = {
			.viewport_width = 640,
			.viewport_height = 480,
			.scissor_width = 640,
			.scissor_height = 480,
			.blend_mode = DRM_GCN_BLEND_NONE,
		},
		.depth = {
			.test_enable = 1,
			.compare = DRM_GCN_DEPTH_LESS,
			.write_enable = 1,
		},
	};
	struct drm_gcn_color_depth_triangle triangle = {
		.vertices = {
			{ 32, 32, 0, DRM_GCN_RGBA8(0xff, 0, 0, 0xff) },
			{ 608, 32, DRM_GCN_DEPTH_MAX / 2,
			  DRM_GCN_RGBA8(0, 0xff, 0, 0xff) },
			{ 320, 448, DRM_GCN_DEPTH_MAX,
			  DRM_GCN_RGBA8(0, 0, 0xff, 0xff) },
		},
	};
	int ret;

	ret = gcn_drm_render_validate_triangle_depth_batch(&args);
	KUNIT_EXPECT_EQ(test, ret, 0);
	ret = gcn_drm_render_validate_draw_state(&args.state, 640, 480);
	KUNIT_EXPECT_EQ(test, ret, 0);
	ret = gcn_drm_validate_z(triangle.vertices, 640, 480, true);
	KUNIT_EXPECT_EQ(test, ret, 0);

	triangle.vertices[0].z = DRM_GCN_DEPTH_MAX + 1;
	ret = gcn_drm_validate_z(triangle.vertices, 640, 480, true);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	triangle.vertices[0].z = 0;
	triangle.vertices[0].rgba &= ~0xffU;
	ret = gcn_drm_validate_z(triangle.vertices, 640, 480, true);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	ret = gcn_drm_validate_z(triangle.vertices, 640, 480, false);
	KUNIT_EXPECT_EQ(test, ret, 0);
	triangle.vertices[0].rgba |= 0xff;
	triangle.vertices[2] = triangle.vertices[0];
	ret = gcn_drm_validate_z(triangle.vertices, 640, 480, true);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	args.depth.test_enable = 2;
	ret = gcn_drm_render_validate_triangle_depth_batch(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.depth.test_enable = 1;
	args.depth.write_enable = 2;
	ret = gcn_drm_render_validate_triangle_depth_batch(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.depth.write_enable = 1;
	args.depth.compare = DRM_GCN_DEPTH_ALWAYS + 1;
	ret = gcn_drm_render_validate_triangle_depth_batch(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.depth.compare = DRM_GCN_DEPTH_LESS;
	args.depth.pad = 1;
	ret = gcn_drm_render_validate_triangle_depth_batch(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.depth.pad = 0;
	args.pad1 = 1;
	ret = gcn_drm_render_validate_triangle_depth_batch(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void gcn_drm_render_validates_textured_triangle_batch(struct kunit *test)
{
	struct drm_gcn_draw_textured_triangles args = {
		.ctx_id = 1,
		.src_handle = 2,
		.dst_handle = 3,
		.triangle_count = 1,
		.triangles_ptr = 0x1000,
		.state = {
			.viewport_width = 640,
			.viewport_height = 480,
			.scissor_width = 640,
			.scissor_height = 480,
			.blend_mode = DRM_GCN_BLEND_NONE,
		},
	};
	struct drm_gcn_texture_triangle triangle = {
		.vertices = {
			{ 0, 0, 0, 0 },
			{ 640, 0, 320, 0 },
			{ 0, 480, 0, 192 },
		},
	};
	int ret;

	ret = gcn_drm_render_validate_textured_triangle_batch(&args);
	KUNIT_EXPECT_EQ(test, ret, 0);
	ret = gcn_drm_render_validate_draw_state(&args.state, 640, 480);
	KUNIT_EXPECT_EQ(test, ret, 0);
	ret = gcn_drm_validate_texture_triangle(triangle.vertices, 320, 192,
						640, 480);
	KUNIT_EXPECT_EQ(test, ret, 0);

	triangle.vertices[1].s = 321;
	ret = gcn_drm_validate_texture_triangle(triangle.vertices, 320, 192,
						640, 480);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	triangle.vertices[1].s = 320;
	triangle.vertices[2].y = 481;
	ret = gcn_drm_validate_texture_triangle(triangle.vertices, 320, 192,
						640, 480);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	triangle.vertices[2].y = 480;
	triangle.vertices[2] = triangle.vertices[0];
	ret = gcn_drm_validate_texture_triangle(triangle.vertices, 320, 192,
						640, 480);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	args.src_handle = args.dst_handle;
	ret = gcn_drm_render_validate_textured_triangle_batch(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.src_handle = 2;
	args.state.blend_mode = DRM_GCN_BLEND_SRC_ALPHA;
	ret = gcn_drm_render_validate_textured_triangle_batch(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.state.blend_mode = DRM_GCN_BLEND_NONE;
	args.state.cull_mode = DRM_GCN_CULL_ALL;
	ret = gcn_drm_render_validate_textured_triangle_batch(&args);
	KUNIT_EXPECT_EQ(test, ret, 0);
	args.state.cull_mode = DRM_GCN_CULL_ALL + 1;
	ret = gcn_drm_render_validate_textured_triangle_batch(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.state.cull_mode = DRM_GCN_CULL_NONE;
	args.pad = 1;
	ret = gcn_drm_render_validate_textured_triangle_batch(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void gcn_drm_render_validates_indexed_triangle_batch(struct kunit *test)
{
	struct drm_gcn_draw_indexed_triangles args = {
		.ctx_id = 1,
		.dst_handle = 2,
		.vertex_count = 5,
		.triangle_count = 2,
		.vertices_ptr = 0x1000,
		.indices_ptr = 0x2000,
		.state = {
			.viewport_width = 640,
			.viewport_height = 480,
			.scissor_width = 640,
			.scissor_height = 480,
			.blend_mode = DRM_GCN_BLEND_NONE,
		},
	};
	struct drm_gcn_color_vertex vertices[5] = {
		{ 0, 0, DRM_GCN_RGBA8(0, 0, 255, 255) },
		{ 32, 40, DRM_GCN_RGBA8(255, 0, 0, 255) },
		{ 224, 40, DRM_GCN_RGBA8(255, 0, 0, 255) },
		{ 224, 216, DRM_GCN_RGBA8(255, 0, 0, 255) },
		{ 32, 216, DRM_GCN_RGBA8(255, 0, 0, 255) },
	};
	u16 indices[6] = { 1, 2, 3, 1, 3, 4 };
	int ret;

	ret = gcn_drm_render_validate_indexed_triangle_batch(&args);
	KUNIT_EXPECT_EQ(test, ret, 0);
	ret = gcn_drm_validate_indexed_triangles(vertices, 5, indices, 2,
						 640, 480, true);
	KUNIT_EXPECT_EQ(test, ret, 0);

	indices[5] = 5;
	ret = gcn_drm_validate_indexed_triangles(vertices, 5, indices, 2,
						 640, 480, true);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	indices[5] = 4;
	indices[1] = 1;
	ret = gcn_drm_validate_indexed_triangles(vertices, 5, indices, 2,
						 640, 480, true);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	indices[1] = 2;
	vertices[4].rgba &= ~0xffU;
	ret = gcn_drm_validate_indexed_triangles(vertices, 5, indices, 2,
						 640, 480, true);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	ret = gcn_drm_validate_indexed_triangles(vertices, 5, indices, 2,
						 640, 480, false);
	KUNIT_EXPECT_EQ(test, ret, 0);

	args.vertex_count = 2;
	ret = gcn_drm_render_validate_indexed_triangle_batch(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.vertex_count = 5;
	args.indices_ptr = 0;
	ret = gcn_drm_render_validate_indexed_triangle_batch(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.indices_ptr = 0x2000;
	args.state.blend_mode = DRM_GCN_BLEND_SRC_ALPHA + 1;
	ret = gcn_drm_render_validate_indexed_triangle_batch(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.state.blend_mode = DRM_GCN_BLEND_NONE;
	args.pad = 1;
	ret = gcn_drm_render_validate_indexed_triangle_batch(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void gcn_drm_render_validates_indexed_textured(struct kunit *test)
{
	struct drm_gcn_draw_indexed_textured args = {
		.ctx_id = 1,
		.src_handle = 2,
		.dst_handle = 3,
		.vertex_count = 5,
		.triangle_count = 2,
		.vertices_ptr = 0x1000,
		.indices_ptr = 0x2000,
		.state = {
			.viewport_width = 640,
			.viewport_height = 480,
			.scissor_width = 640,
			.scissor_height = 480,
			.blend_mode = DRM_GCN_BLEND_NONE,
		},
	};
	struct drm_gcn_texture_vertex vertices[5] = {
		{ 0, 0, 320, 192 },
		{ 32, 40, 0, 0 },
		{ 224, 40, 320, 0 },
		{ 224, 216, 320, 192 },
		{ 32, 216, 0, 192 },
	};
	u16 indices[6] = { 1, 2, 3, 1, 3, 4 };
	int ret;

	ret = gcn_drm_itex_args(&args);
	KUNIT_EXPECT_EQ(test, ret, 0);
	ret = gcn_drm_itex_vertices(vertices, 5, indices, 2,
				    320, 192, 640, 480);
	KUNIT_EXPECT_EQ(test, ret, 0);

	indices[5] = 5;
	ret = gcn_drm_itex_vertices(vertices, 5, indices, 2,
				    320, 192, 640, 480);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	indices[5] = 4;
	indices[1] = 1;
	ret = gcn_drm_itex_vertices(vertices, 5, indices, 2,
				    320, 192, 640, 480);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	indices[1] = 2;
	vertices[4].t = 193;
	ret = gcn_drm_itex_vertices(vertices, 5, indices, 2,
				    320, 192, 640, 480);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	vertices[4].t = 192;

	args.src_handle = args.dst_handle;
	ret = gcn_drm_itex_args(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.src_handle = 2;
	args.state.blend_mode = DRM_GCN_BLEND_SRC_ALPHA;
	ret = gcn_drm_itex_args(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.state.blend_mode = DRM_GCN_BLEND_NONE;
	args.pad0 = 1;
	ret = gcn_drm_itex_args(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.pad0 = 0;
	args.pad1 = 1;
	ret = gcn_drm_itex_args(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void gcn_drm_render_validates_itex_depth(struct kunit *test)
{
	struct drm_gcn_draw_indexed_textured_depth args = {
		.ctx_id = 1,
		.src_handle = 2,
		.dst_handle = 3,
		.vertex_count = 5,
		.triangle_count = 2,
		.vertices_ptr = 0x1000,
		.indices_ptr = 0x2000,
		.state = {
			.viewport_width = 640,
			.viewport_height = 480,
			.scissor_width = 640,
			.scissor_height = 480,
			.blend_mode = DRM_GCN_BLEND_NONE,
		},
		.depth = {
			.test_enable = 1,
			.compare = DRM_GCN_DEPTH_LESS,
			.write_enable = 1,
		},
	};
	struct drm_gcn_texture_depth_vertex vertices[5] = {
		{ 0, 0, DRM_GCN_DEPTH_MAX, 320, 192 },
		{ 32, 40, 0, 0, 0 },
		{ 224, 40, DRM_GCN_DEPTH_MAX / 2, 320, 0 },
		{ 224, 216, DRM_GCN_DEPTH_MAX, 320, 192 },
		{ 32, 216, 1, 0, 192 },
	};
	u16 indices[6] = { 1, 2, 3, 1, 3, 4 };
	int ret;

	ret = gcn_drm_itex_z_args(&args);
	KUNIT_EXPECT_EQ(test, ret, 0);
	ret = gcn_drm_itex_z_vertices(vertices, 5, indices, 2,
				      320, 192, 640, 480);
	KUNIT_EXPECT_EQ(test, ret, 0);

	indices[5] = 5;
	ret = gcn_drm_itex_z_vertices(vertices, 5, indices, 2,
				      320, 192, 640, 480);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	indices[5] = 4;
	indices[1] = 1;
	ret = gcn_drm_itex_z_vertices(vertices, 5, indices, 2,
				      320, 192, 640, 480);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	indices[1] = 2;
	vertices[4].z = DRM_GCN_DEPTH_MAX + 1;
	ret = gcn_drm_itex_z_vertices(vertices, 5, indices, 2,
				      320, 192, 640, 480);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	vertices[4].z = 1;
	vertices[4].t = 193;
	ret = gcn_drm_itex_z_vertices(vertices, 5, indices, 2,
				      320, 192, 640, 480);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	vertices[4].t = 192;

	args.src_handle = args.dst_handle;
	ret = gcn_drm_itex_z_args(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.src_handle = 2;
	args.state.blend_mode = DRM_GCN_BLEND_SRC_ALPHA;
	ret = gcn_drm_itex_z_args(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.state.blend_mode = DRM_GCN_BLEND_NONE;
	args.depth.test_enable = 2;
	ret = gcn_drm_itex_z_args(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.depth.test_enable = 1;
	args.depth.compare = DRM_GCN_DEPTH_ALWAYS + 1;
	ret = gcn_drm_itex_z_args(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.depth.compare = DRM_GCN_DEPTH_LESS;
	args.depth.write_enable = 2;
	ret = gcn_drm_itex_z_args(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.depth.write_enable = 1;
	args.depth.pad = 1;
	ret = gcn_drm_itex_z_args(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.depth.pad = 0;
	args.pad0 = 1;
	ret = gcn_drm_itex_z_args(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.pad0 = 0;
	args.pad1 = 1;
	ret = gcn_drm_itex_z_args(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void gcn_drm_render_validates_fixed_draw(struct kunit *test)
{
	struct drm_gcn_draw_indexed_fixed args = {
		.ctx_id = 1,
		.src_handle = 2,
		.dst_handle = 3,
		.vertex_count = 5,
		.triangle_count = 2,
		.tev_mode = DRM_GCN_TEV_MODULATE,
		.vertices_ptr = 0x1000,
		.indices_ptr = 0x2000,
		.state = {
			.viewport_width = 640,
			.viewport_height = 480,
			.scissor_width = 640,
			.scissor_height = 480,
			.blend_mode = DRM_GCN_BLEND_NONE,
		},
		.depth = {
			.test_enable = 1,
			.compare = DRM_GCN_DEPTH_LESS,
			.write_enable = 1,
		},
	};
	struct drm_gcn_fixed_vertex vertices[5] = {
		{ 0, 0, DRM_GCN_DEPTH_MAX,
		  DRM_GCN_RGBA8(0, 0, 0xff, 0xff), 320, 192 },
		{ 32, 40, 0, DRM_GCN_RGBA8(0, 0xff, 0xff, 0xff), 0, 0 },
		{ 224, 40, DRM_GCN_DEPTH_MAX / 2,
		  DRM_GCN_RGBA8(0xff, 0, 0xff, 0xff), 320, 0 },
		{ 224, 216, DRM_GCN_DEPTH_MAX,
		  DRM_GCN_RGBA8(0xff, 0, 0xff, 0xff), 320, 192 },
		{ 32, 216, 1, DRM_GCN_RGBA8(0, 0xff, 0xff, 0xff), 0, 192 },
	};
	u16 indices[6] = { 1, 2, 3, 1, 3, 4 };
	int ret;

	ret = gcn_drm_fixed_args(&args);
	KUNIT_EXPECT_EQ(test, ret, 0);
	args.texture_filter = DRM_GCN_TEXTURE_FILTER_LINEAR;
	ret = gcn_drm_fixed_args(&args);
	KUNIT_EXPECT_EQ(test, ret, 0);
	args.texture_filter = DRM_GCN_TEXTURE_FILTER_LINEAR + 1;
	ret = gcn_drm_fixed_args(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.texture_filter = DRM_GCN_TEXTURE_FILTER_NEAREST;
	ret = gcn_drm_fixed_vertices(vertices, 5, indices, 2,
				     DRM_GCN_TEV_MODULATE,
				     320, 192, 640, 480, true);
	KUNIT_EXPECT_EQ(test, ret, 0);

	args.tev_mode = DRM_GCN_TEV_REPLACE_TEXTURE;
	ret = gcn_drm_fixed_args(&args);
	KUNIT_EXPECT_EQ(test, ret, 0);
	vertices[4].rgba &= ~0xffU;
	ret = gcn_drm_fixed_vertices(vertices, 5, indices, 2,
				     DRM_GCN_TEV_REPLACE_TEXTURE,
				     320, 192, 640, 480, true);
	KUNIT_EXPECT_EQ(test, ret, 0);
	vertices[4].rgba |= 0xff;

	args.tev_mode = DRM_GCN_TEV_PASS_COLOR;
	args.src_handle = 0;
	args.texture_filter = DRM_GCN_TEXTURE_FILTER_LINEAR;
	ret = gcn_drm_fixed_args(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.texture_filter = DRM_GCN_TEXTURE_FILTER_NEAREST;
	vertices[0].s = 0;
	vertices[0].t = 0;
	vertices[2].s = 0;
	vertices[3].s = 0;
	vertices[3].t = 0;
	vertices[4].t = 0;
	ret = gcn_drm_fixed_args(&args);
	KUNIT_EXPECT_EQ(test, ret, 0);
	ret = gcn_drm_fixed_vertices(vertices, 5, indices, 2,
				     DRM_GCN_TEV_PASS_COLOR,
				     0, 0, 640, 480, true);
	KUNIT_EXPECT_EQ(test, ret, 0);

	args.src_handle = 2;
	ret = gcn_drm_fixed_args(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.src_handle = 0;
	vertices[4].s = 1;
	ret = gcn_drm_fixed_vertices(vertices, 5, indices, 2,
				     DRM_GCN_TEV_PASS_COLOR,
				     0, 0, 640, 480, true);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	vertices[4].s = 0;

	args.tev_mode = DRM_GCN_TEV_MODULATE;
	ret = gcn_drm_fixed_args(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.src_handle = 2;
	vertices[4].rgba &= ~0xffU;
	ret = gcn_drm_fixed_vertices(vertices, 5, indices, 2,
				     DRM_GCN_TEV_MODULATE,
				     320, 192, 640, 480, true);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	ret = gcn_drm_fixed_vertices(vertices, 5, indices, 2,
				     DRM_GCN_TEV_MODULATE,
				     320, 192, 640, 480, false);
	KUNIT_EXPECT_EQ(test, ret, 0);
	vertices[4].rgba |= 0xff;

	args.tev_mode = DRM_GCN_TEV_REPLACE_TEXTURE;
	args.state.blend_mode = DRM_GCN_BLEND_SRC_ALPHA;
	ret = gcn_drm_fixed_args(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.state.blend_mode = DRM_GCN_BLEND_NONE;
	indices[5] = 5;
	ret = gcn_drm_fixed_vertices(vertices, 5, indices, 2,
				     DRM_GCN_TEV_REPLACE_TEXTURE,
				     320, 192, 640, 480, true);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	indices[5] = 4;
	indices[1] = 1;
	ret = gcn_drm_fixed_vertices(vertices, 5, indices, 2,
				     DRM_GCN_TEV_REPLACE_TEXTURE,
				     320, 192, 640, 480, true);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	indices[1] = 2;
	args.depth.write_enable = 2;
	ret = gcn_drm_fixed_args(&args);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void gcn_drm_render_validates_scaled_blit(struct kunit *test)
{
	struct drm_gcn_blit_scaled args = {
		.ctx_id = 1,
		.src_handle = 2,
		.dst_handle = 3,
		.src_x = 17,
		.src_y = 19,
		.src_width = 73,
		.src_height = 61,
		.dst_x = 29,
		.dst_y = 31,
		.dst_width = 113,
		.dst_height = 97,
	};
	int ret;

	ret = gcn_drm_render_validate_scaled(&args, 320, 192, 256, 256);
	KUNIT_EXPECT_EQ(test, ret, 0);
	args.src_width = 304;
	ret = gcn_drm_render_validate_scaled(&args, 320, 192, 256, 256);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.src_width = 73;
	args.dst_height = 226;
	ret = gcn_drm_render_validate_scaled(&args, 320, 192, 256, 256);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.dst_height = 97;
	args.src_handle = args.dst_handle;
	ret = gcn_drm_render_validate_scaled(&args, 256, 256, 256, 256);
	KUNIT_EXPECT_EQ(test, ret, 0);
	args.flags = 1;
	ret = gcn_drm_render_validate_scaled(&args, 256, 256, 256, 256);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.flags = 0;
	args.pad = 1;
	ret = gcn_drm_render_validate_scaled(&args, 256, 256, 256, 256);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.pad = 0;
	args.src_width = 0;
	ret = gcn_drm_render_validate_scaled(&args, 256, 256, 256, 256);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void gcn_drm_render_validates_copy_submit(struct kunit *test)
{
	struct drm_gcn_submit args = {
		.ctx_id = 1,
		.op = DRM_GCN_RENDER_OP_COPY_RGB565,
		.src_handle = 1,
		.dst_handle = 2,
	};

	KUNIT_EXPECT_EQ(test, gcn_drm_render_validate_submit(&args), 0);
	args.flags = 1;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_validate_submit(&args), -EINVAL);
	args.flags = 0;
	args.dst_handle = args.src_handle;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_validate_submit(&args), -EINVAL);
	args.dst_handle = 2;
	args.data = 1;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_validate_submit(&args), -EINVAL);
	args.data = 0;
	args.op = 0;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_validate_submit(&args), -EINVAL);
}

static void gcn_drm_render_validates_fill_submit(struct kunit *test)
{
	struct drm_gcn_submit args = {
		.ctx_id = 1,
		.op = DRM_GCN_RENDER_OP_FILL_RGB565,
		.dst_handle = 2,
		.data = 0x5aa5,
	};

	KUNIT_EXPECT_EQ(test, gcn_drm_render_validate_submit(&args), 0);
	args.data = 0;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_validate_submit(&args), 0);
	args.data = 1ULL << 16;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_validate_submit(&args), -EINVAL);
	args.data = 0x5aa5;
	args.src_handle = 1;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_validate_submit(&args), -EINVAL);
	args.src_handle = 0;
	args.dst_handle = 0;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_validate_submit(&args), -EINVAL);
}

static void gcn_drm_render_validates_rect_fill_submit(struct kunit *test)
{
	struct gcn_drm_render_rect rect;
	struct drm_gcn_submit args = {
		.ctx_id = 1,
		.op = DRM_GCN_RENDER_OP_FILL_RECT_RGB565,
		.dst_handle = 2,
		.data = DRM_GCN_RECT_DATA(0x5aa5, 13, 17, 73, 61),
	};

	KUNIT_EXPECT_EQ(test, gcn_drm_render_validate_submit(&args), 0);
	KUNIT_EXPECT_EQ(test,
			gcn_drm_render_validate_rect(args.data, 256, 256), 0);
	gcn_drm_render_decode_rect(args.data, &rect);
	KUNIT_EXPECT_EQ(test, rect.color, (u16)0x5aa5);
	KUNIT_EXPECT_EQ(test, rect.x, (u16)13);
	KUNIT_EXPECT_EQ(test, rect.y, (u16)17);
	KUNIT_EXPECT_EQ(test, rect.width, (u16)73);
	KUNIT_EXPECT_EQ(test, rect.height, (u16)61);

	args.data |= 1ULL << 63;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_validate_submit(&args), -EINVAL);
	args.data = DRM_GCN_RECT_DATA(0x5aa5, 255, 255, 2, 1);
	KUNIT_EXPECT_EQ(test,
			gcn_drm_render_validate_rect(args.data, 256, 256),
			-EINVAL);
	args.data = DRM_GCN_RECT_DATA(0x5aa5, 255, 255, 1, 1);
	KUNIT_EXPECT_EQ(test,
			gcn_drm_render_validate_rect(args.data, 256, 256), 0);
	args.src_handle = 1;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_validate_submit(&args), -EINVAL);
}

static void gcn_drm_render_validates_rect_blit_submit(struct kunit *test)
{
	struct gcn_drm_render_blit_rect rect;
	int ret;
	struct drm_gcn_submit args = {
		.ctx_id = 1,
		.op = DRM_GCN_RENDER_OP_BLIT_RECT_RGB565,
		.src_handle = 1,
		.dst_handle = 2,
		.data = DRM_GCN_BLIT_RECT_DATA(101, 29, 11, 97, 67, 53),
	};

	KUNIT_EXPECT_EQ(test, gcn_drm_render_validate_submit(&args), 0);
	ret = gcn_drm_render_validate_blit_rect(args.data, 256, 256, 256, 256);
	KUNIT_EXPECT_EQ(test, ret, 0);
	gcn_drm_render_decode_blit_rect(args.data, &rect);
	KUNIT_EXPECT_EQ(test, rect.src_x, (u16)101);
	KUNIT_EXPECT_EQ(test, rect.src_y, (u16)29);
	KUNIT_EXPECT_EQ(test, rect.dst_x, (u16)11);
	KUNIT_EXPECT_EQ(test, rect.dst_y, (u16)97);
	KUNIT_EXPECT_EQ(test, rect.width, (u16)67);
	KUNIT_EXPECT_EQ(test, rect.height, (u16)53);

	args.data |= 1ULL << 63;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_validate_submit(&args), -EINVAL);
	args.data = DRM_GCN_BLIT_RECT_DATA(255, 255, 0, 0, 2, 1);
	ret = gcn_drm_render_validate_blit_rect(args.data, 256, 256, 256, 256);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.data = DRM_GCN_BLIT_RECT_DATA(0, 0, 255, 255, 1, 2);
	ret = gcn_drm_render_validate_blit_rect(args.data, 256, 256, 256, 256);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.data = DRM_GCN_BLIT_RECT_DATA(255, 255, 255, 255, 1, 1);
	ret = gcn_drm_render_validate_blit_rect(args.data, 256, 256, 256, 256);
	KUNIT_EXPECT_EQ(test, ret, 0);
	args.data = DRM_GCN_BLIT_RECT_DATA(241, 103, 11, 97, 67, 53);
	ret = gcn_drm_render_validate_blit_rect(args.data, 320, 192, 256, 256);
	KUNIT_EXPECT_EQ(test, ret, 0);
	args.data = DRM_GCN_BLIT_RECT_DATA(319, 191, 255, 255, 1, 1);
	ret = gcn_drm_render_validate_blit_rect(args.data, 320, 192, 256, 256);
	KUNIT_EXPECT_EQ(test, ret, 0);
	args.data = DRM_GCN_BLIT_RECT_DATA(319, 191, 255, 255, 2, 1);
	ret = gcn_drm_render_validate_blit_rect(args.data, 320, 192, 256, 256);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	args.data = DRM_GCN_BLIT_RECT_DATA(17, 31, 43, 31, 113, 79);
	args.dst_handle = args.src_handle;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_validate_submit(&args), 0);
}

static void gcn_drm_render_accepts_tiled_rgb565(struct kunit *test)
{
	struct drm_gcn_gem_create args = {
		.width = 640,
		.height = 480,
		.format = DRM_GCN_GEM_FORMAT_RGB565,
		.layout = DRM_GCN_GEM_LAYOUT_TILED_4X4,
	};
	u64 size = 0;

	KUNIT_EXPECT_EQ(test, gcn_drm_render_bo_size(&args, &size), 0);
	KUNIT_EXPECT_EQ(test, size, 614400ULL);
	args.flags = DRM_GCN_GEM_CREATE_SYSTEM;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_bo_size(&args, &size), 0);
	KUNIT_EXPECT_EQ(test, size, 614400ULL);

	args.flags = 0;
	args.width = 64;
	args.height = 64;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_bo_size(&args, &size), 0);
	KUNIT_EXPECT_EQ(test, size, 8192ULL);

	args.flags = DRM_GCN_GEM_CREATE_SYSTEM;
	args.layout = DRM_GCN_GEM_LAYOUT_LINEAR;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_bo_size(&args, &size), 0);
	KUNIT_EXPECT_EQ(test, size, 8192ULL);
}

static void gcn_drm_render_rejects_invalid_objects(struct kunit *test)
{
	struct drm_gcn_gem_create args = {
		.width = 640,
		.height = 480,
		.format = DRM_GCN_GEM_FORMAT_RGB565,
		.layout = DRM_GCN_GEM_LAYOUT_TILED_4X4,
	};
	u64 size;

	args.flags = 2;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_bo_size(&args, &size), -EINVAL);
	args.flags = 0;
	args.width = 639;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_bo_size(&args, &size), -EINVAL);
	args.width = 644;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_bo_size(&args, &size), -EINVAL);
	args.width = 640;
	args.height = 577;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_bo_size(&args, &size), -EINVAL);
	args.height = 480;
	args.format = 0;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_bo_size(&args, &size), -EINVAL);
	args.format = DRM_GCN_GEM_FORMAT_RGB565;
	args.layout = 0;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_bo_size(&args, &size), -EINVAL);
	args.layout = DRM_GCN_GEM_LAYOUT_LINEAR;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_bo_size(&args, &size), -EINVAL);
}

static void gcn_drm_render_accepts_tiled_rgba8_texture(struct kunit *test)
{
	struct drm_gcn_gem_create args = {
		.width = 64,
		.height = 64,
		.format = DRM_GCN_GEM_FORMAT_RGBA8,
		.layout = DRM_GCN_GEM_LAYOUT_TILED_4X4,
	};
	u64 size = 0;

	KUNIT_EXPECT_EQ(test, gcn_drm_render_bo_size(&args, &size), 0);
	KUNIT_EXPECT_EQ(test, size, 16384ULL);

	args.layout = DRM_GCN_GEM_LAYOUT_LINEAR;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_bo_size(&args, &size), -EINVAL);
	args.layout = DRM_GCN_GEM_LAYOUT_TILED_4X4;
	args.flags = DRM_GCN_GEM_CREATE_SYSTEM;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_bo_size(&args, &size), -EINVAL);
}

static void gcn_drm_render_accepts_linear_system_xrgb8888(struct kunit *test)
{
	struct drm_gcn_gem_create args = {
		.width = 320,
		.height = 240,
		.format = DRM_GCN_GEM_FORMAT_XRGB8888,
		.layout = DRM_GCN_GEM_LAYOUT_LINEAR,
		.flags = DRM_GCN_GEM_CREATE_SYSTEM,
	};
	u64 size = 0;

	KUNIT_EXPECT_EQ(test, gcn_drm_render_bo_size(&args, &size), 0);
	KUNIT_EXPECT_EQ(test, size, 307200ULL);

	args.flags = 0;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_bo_size(&args, &size), -EINVAL);
	args.flags = DRM_GCN_GEM_CREATE_SYSTEM;
	args.layout = DRM_GCN_GEM_LAYOUT_TILED_4X4;
	KUNIT_EXPECT_EQ(test, gcn_drm_render_bo_size(&args, &size), -EINVAL);
}

static void gcn_drm_render_validates_system_format_pairs(struct kunit *test)
{
	int ret;

	ret = gcn_drm_valid_system_formats(DRM_GCN_GEM_FORMAT_RGB565,
					   DRM_GCN_GEM_LAYOUT_LINEAR,
					   DRM_GCN_GEM_FORMAT_RGB565,
					   DRM_GCN_GEM_LAYOUT_LINEAR, true);
	KUNIT_EXPECT_EQ(test, ret, 0);
	ret = gcn_drm_valid_system_formats(DRM_GCN_GEM_FORMAT_XRGB8888,
					   DRM_GCN_GEM_LAYOUT_LINEAR,
					   DRM_GCN_GEM_FORMAT_RGB565,
					   DRM_GCN_GEM_LAYOUT_LINEAR, false);
	KUNIT_EXPECT_EQ(test, ret, 0);
	ret = gcn_drm_valid_system_formats(DRM_GCN_GEM_FORMAT_XRGB8888,
					   DRM_GCN_GEM_LAYOUT_TILED_4X4,
					   DRM_GCN_GEM_FORMAT_RGB565,
					   DRM_GCN_GEM_LAYOUT_LINEAR, false);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	ret = gcn_drm_valid_system_formats(DRM_GCN_GEM_FORMAT_XRGB8888,
					   DRM_GCN_GEM_LAYOUT_LINEAR,
					   DRM_GCN_GEM_FORMAT_RGB565,
					   DRM_GCN_GEM_LAYOUT_LINEAR, true);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	ret = gcn_drm_valid_system_formats(DRM_GCN_GEM_FORMAT_XRGB8888,
					   DRM_GCN_GEM_LAYOUT_LINEAR,
					   DRM_GCN_GEM_FORMAT_XRGB8888,
					   DRM_GCN_GEM_LAYOUT_LINEAR, false);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void gcn_drm_render_validates_linear_framebuffer(struct kunit *test)
{
	struct drm_mode_fb_cmd2 mode_cmd = {
		.width = 640,
		.height = 480,
		.pixel_format = DRM_FORMAT_RGB565,
		.pitches[0] = 640 * sizeof(u16),
		.modifier[0] = DRM_FORMAT_MOD_LINEAR,
	};
	int ret;

	ret = gcn_drm_render_validate_fb(640, 480,
					 DRM_GCN_GEM_FORMAT_RGB565,
					 DRM_GCN_GEM_LAYOUT_LINEAR, &mode_cmd);
	KUNIT_EXPECT_EQ(test, ret, 0);
	ret = gcn_drm_render_validate_fb(640, 480,
					 DRM_GCN_GEM_FORMAT_RGB565,
					 DRM_GCN_GEM_LAYOUT_TILED_4X4,
					 &mode_cmd);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	mode_cmd.pitches[0] += sizeof(u16);
	ret = gcn_drm_render_validate_fb(640, 480,
					 DRM_GCN_GEM_FORMAT_RGB565,
					 DRM_GCN_GEM_LAYOUT_LINEAR, &mode_cmd);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	mode_cmd.pitches[0] = 640 * sizeof(u16);
	mode_cmd.width = 320;
	ret = gcn_drm_render_validate_fb(640, 480,
					 DRM_GCN_GEM_FORMAT_RGB565,
					 DRM_GCN_GEM_LAYOUT_LINEAR, &mode_cmd);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void gcn_drm_render_converts_absolute_timeouts(struct kunit *test)
{
	u64 now = 10ULL * NSEC_PER_SEC;
	unsigned long timeout;

	KUNIT_EXPECT_EQ(test, gcn_drm_render_timeout_jiffies(now, now), 0UL);
	KUNIT_EXPECT_EQ(test, gcn_drm_render_timeout_jiffies(now - 1, now),
			0UL);
	KUNIT_EXPECT_EQ(test,
			gcn_drm_render_timeout_jiffies(U64_MAX, now),
			MAX_SCHEDULE_TIMEOUT);
	timeout = gcn_drm_render_timeout_jiffies(now + NSEC_PER_SEC, now);
	KUNIT_EXPECT_GE(test, timeout, 1UL);
	KUNIT_EXPECT_LE(test, timeout, HZ + 1UL);
}

static struct kunit_case gcn_drm_render_test_cases[] = {
	KUNIT_CASE(gcn_drm_render_uapi_layout),
	KUNIT_CASE(gcn_drm_render_accepts_tiled_rgb565),
	KUNIT_CASE(gcn_drm_render_accepts_tiled_rgba8_texture),
	KUNIT_CASE(gcn_drm_render_accepts_linear_system_xrgb8888),
	KUNIT_CASE(gcn_drm_render_rejects_invalid_objects),
	KUNIT_CASE(gcn_drm_render_validates_system_format_pairs),
	KUNIT_CASE(gcn_drm_render_validates_linear_framebuffer),
	KUNIT_CASE(gcn_drm_render_converts_absolute_timeouts),
	KUNIT_CASE(gcn_drm_render_validates_copy_submit),
	KUNIT_CASE(gcn_drm_render_validates_fill_submit),
	KUNIT_CASE(gcn_drm_render_validates_rect_fill_submit),
	KUNIT_CASE(gcn_drm_render_validates_rect_blit_submit),
	KUNIT_CASE(gcn_drm_render_validates_scaled_blit),
	KUNIT_CASE(gcn_drm_render_validates_color_triangle),
	KUNIT_CASE(gcn_drm_render_validates_triangle_batch),
	KUNIT_CASE(gcn_drm_render_validates_triangle_state_batch),
	KUNIT_CASE(gcn_drm_render_validates_triangle_depth_batch),
	KUNIT_CASE(gcn_drm_render_validates_textured_triangle_batch),
	KUNIT_CASE(gcn_drm_render_validates_indexed_triangle_batch),
	KUNIT_CASE(gcn_drm_render_validates_indexed_textured),
	KUNIT_CASE(gcn_drm_render_validates_itex_depth),
	KUNIT_CASE(gcn_drm_render_validates_fixed_draw),
	{}
};

static struct kunit_suite gcn_drm_render_test_suite = {
	.name = "gcn_drm_render",
	.test_cases = gcn_drm_render_test_cases,
};

kunit_test_suite(gcn_drm_render_test_suite);

MODULE_DESCRIPTION("Nintendo GCN DRM render UAPI validation tests");
MODULE_LICENSE("GPL");
