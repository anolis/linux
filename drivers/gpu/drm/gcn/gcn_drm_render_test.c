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
	KUNIT_EXPECT_EQ(test, DRM_GCN_NUM_IOCTLS, 8);
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
	KUNIT_CASE(gcn_drm_render_rejects_invalid_objects),
	KUNIT_CASE(gcn_drm_render_validates_linear_framebuffer),
	KUNIT_CASE(gcn_drm_render_converts_absolute_timeouts),
	KUNIT_CASE(gcn_drm_render_validates_copy_submit),
	KUNIT_CASE(gcn_drm_render_validates_fill_submit),
	KUNIT_CASE(gcn_drm_render_validates_rect_fill_submit),
	KUNIT_CASE(gcn_drm_render_validates_rect_blit_submit),
	KUNIT_CASE(gcn_drm_render_validates_scaled_blit),
	{}
};

static struct kunit_suite gcn_drm_render_test_suite = {
	.name = "gcn_drm_render",
	.test_cases = gcn_drm_render_test_cases,
};

kunit_test_suite(gcn_drm_render_test_suite);

MODULE_DESCRIPTION("Nintendo GCN DRM render UAPI validation tests");
MODULE_LICENSE("GPL");
