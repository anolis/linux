// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>

#include <drm/gcn_gx_mem1.h>

#define TEST_START		0x01600000ULL
#define TEST_SIZE		0x00080000ULL
#define TEST_ALIGNMENT		32ULL
#define TEST_OBJECT_SIZE	(128 * 1024ULL)

static void gcn_gx_mem1_rejects_invalid_ranges(struct kunit *test)
{
	struct gcn_gx_mem1_allocator allocator = {};

	KUNIT_EXPECT_EQ(test, -EINVAL,
		gcn_gx_mem1_allocator_init(&allocator, TEST_START + 1,
					   TEST_SIZE, TEST_ALIGNMENT));
	KUNIT_EXPECT_EQ(test, -EINVAL,
		gcn_gx_mem1_allocator_init(&allocator, TEST_START,
					   TEST_SIZE - 1, TEST_ALIGNMENT));
	KUNIT_EXPECT_EQ(test, -EINVAL,
		gcn_gx_mem1_allocator_init(&allocator, TEST_START,
					   TEST_SIZE, 24));
	KUNIT_EXPECT_EQ(test, -EINVAL,
		gcn_gx_mem1_allocator_init(&allocator, U64_MAX - 15,
					   32, TEST_ALIGNMENT));
	KUNIT_EXPECT_FALSE(test, allocator.initialized);
}

static void gcn_gx_mem1_places_and_reuses_render_objects(struct kunit *test)
{
	struct gcn_gx_mem1_allocator allocator = {};
	struct drm_mm_node first = {};
	struct drm_mm_node second = {};
	struct drm_mm_node replacement = {};

	KUNIT_ASSERT_EQ(test, 0,
		gcn_gx_mem1_allocator_init(&allocator, TEST_START,
					   TEST_SIZE, TEST_ALIGNMENT));
	KUNIT_ASSERT_EQ(test, 0,
		gcn_gx_mem1_insert(&allocator, &first, TEST_OBJECT_SIZE,
				   TEST_ALIGNMENT));
	KUNIT_ASSERT_EQ(test, 0,
		gcn_gx_mem1_insert(&allocator, &second, TEST_OBJECT_SIZE,
				   TEST_ALIGNMENT));
	KUNIT_EXPECT_EQ(test, TEST_START, first.start);
	KUNIT_EXPECT_EQ(test, TEST_START + TEST_OBJECT_SIZE, second.start);
	KUNIT_EXPECT_TRUE(test, gcn_gx_mem1_contains(&allocator, &first));
	KUNIT_EXPECT_TRUE(test, gcn_gx_mem1_contains(&allocator, &second));

	gcn_gx_mem1_remove(&first);
	KUNIT_ASSERT_EQ(test, 0,
		gcn_gx_mem1_insert(&allocator, &replacement,
				   TEST_OBJECT_SIZE, TEST_ALIGNMENT));
	KUNIT_EXPECT_EQ(test, TEST_START, replacement.start);

	gcn_gx_mem1_remove(&replacement);
	gcn_gx_mem1_remove(&second);
	KUNIT_EXPECT_TRUE(test, drm_mm_clean(&allocator.mm));
	gcn_gx_mem1_allocator_fini(&allocator);
	KUNIT_EXPECT_FALSE(test, allocator.initialized);
}

static void gcn_gx_mem1_enforces_capacity(struct kunit *test)
{
	struct gcn_gx_mem1_allocator allocator = {};
	struct drm_mm_node objects[4] = {};
	struct drm_mm_node overflow = {};
	int i;

	KUNIT_ASSERT_EQ(test, 0,
		gcn_gx_mem1_allocator_init(&allocator, TEST_START,
					   TEST_SIZE, TEST_ALIGNMENT));
	for (i = 0; i < ARRAY_SIZE(objects); i++) {
		KUNIT_ASSERT_EQ(test, 0,
				gcn_gx_mem1_insert(&allocator, &objects[i],
						   TEST_OBJECT_SIZE,
						   TEST_ALIGNMENT));
		KUNIT_EXPECT_EQ(test, TEST_START + i * TEST_OBJECT_SIZE,
				objects[i].start);
	}
	KUNIT_EXPECT_EQ(test, -ENOSPC,
		gcn_gx_mem1_insert(&allocator, &overflow, TEST_ALIGNMENT,
				   TEST_ALIGNMENT));

	for (i = ARRAY_SIZE(objects) - 1; i >= 0; i--)
		gcn_gx_mem1_remove(&objects[i]);
	gcn_gx_mem1_allocator_fini(&allocator);
}

static struct kunit_case gcn_gx_mem1_test_cases[] = {
	KUNIT_CASE(gcn_gx_mem1_rejects_invalid_ranges),
	KUNIT_CASE(gcn_gx_mem1_places_and_reuses_render_objects),
	KUNIT_CASE(gcn_gx_mem1_enforces_capacity),
	{}
};

static struct kunit_suite gcn_gx_mem1_test_suite = {
	.name = "gcn_gx_mem1",
	.test_cases = gcn_gx_mem1_test_cases,
};

kunit_test_suite(gcn_gx_mem1_test_suite);

MODULE_DESCRIPTION("Nintendo GX MEM1 allocator tests");
MODULE_LICENSE("GPL");
