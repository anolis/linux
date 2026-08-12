// SPDX-License-Identifier: GPL-2.0-only

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <drm/gcn_drm.h>

#define TEST_WIDTH 256U
#define TEST_HEIGHT 256U
#define MAX_OBJECTS 1024U

static int failures;

static void fail(const char *what)
{
	fprintf(stderr, "FAIL: %s: %s\n", what, strerror(errno));
	failures++;
}

static void fail_value(const char *what, uint64_t got, uint64_t expected)
{
	fprintf(stderr, "FAIL: %s: got=%llu expected=%llu\n", what,
		(unsigned long long)got, (unsigned long long)expected);
	failures++;
}

static int get_param(int fd, uint32_t param, uint64_t *value)
{
	struct drm_gcn_get_param args = {
		.param = param,
	};

	if (ioctl(fd, DRM_IOCTL_GCN_GET_PARAM, &args))
		return -1;
	*value = args.value;
	return 0;
}

static int create_bo(int fd, struct drm_gcn_gem_create *args)
{
	memset(args, 0, sizeof(*args));
	args->width = TEST_WIDTH;
	args->height = TEST_HEIGHT;
	args->format = DRM_GCN_GEM_FORMAT_RGB565;
	args->layout = DRM_GCN_GEM_LAYOUT_TILED_4X4;
	return ioctl(fd, DRM_IOCTL_GCN_GEM_CREATE, args);
}

static int close_bo(int fd, uint32_t handle)
{
	struct drm_gem_close args = {
		.handle = handle,
	};

	return ioctl(fd, DRM_IOCTL_GEM_CLOSE, &args);
}

static uint64_t monotonic_ns(void)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now)) {
		fail("clock_gettime");
		return 0;
	}
	return (uint64_t)now.tv_sec * 1000000000ULL + now.tv_nsec;
}

static void test_provider_absent(int fd)
{
	struct drm_gcn_gem_create bo;
	struct drm_gcn_ctx_create ctx = {};
	uint64_t value;

	errno = 0;
	if (!get_param(fd, DRM_GCN_PARAM_MEM1_TOTAL_BYTES, &value) ||
	    errno != ENODEV)
		fail("provider-specific GET_PARAM should return ENODEV");
	errno = 0;
	if (!create_bo(fd, &bo) || errno != ENODEV)
		fail("GEM_CREATE without provider should return ENODEV");
	errno = 0;
	if (!ioctl(fd, DRM_IOCTL_GCN_CTX_CREATE, &ctx) || errno != ENODEV)
		fail("CTX_CREATE without provider should return ENODEV");
}

static void test_contexts(int fd, int other_fd)
{
	struct drm_gcn_ctx_create first = {};
	struct drm_gcn_ctx_create second = {};
	struct drm_gcn_ctx_free free_args;

	if (ioctl(fd, DRM_IOCTL_GCN_CTX_CREATE, &first)) {
		fail("create first context");
		return;
	}
	if (ioctl(fd, DRM_IOCTL_GCN_CTX_CREATE, &second)) {
		fail("create second context");
		goto free_first;
	}
	if (!first.id || !second.id || first.id == second.id)
		fail_value("context IDs must be nonzero and unique", second.id,
			   first.id + 1);

	free_args.id = first.id;
	free_args.pad = 0;
	errno = 0;
	if (!ioctl(other_fd, DRM_IOCTL_GCN_CTX_FREE, &free_args) ||
	    errno != ENOENT)
		fail("context must be isolated to its DRM file");

	free_args.id = second.id;
	if (ioctl(fd, DRM_IOCTL_GCN_CTX_FREE, &free_args))
		fail("free second context");
free_first:
	free_args.id = first.id;
	free_args.pad = 0;
	if (ioctl(fd, DRM_IOCTL_GCN_CTX_FREE, &free_args))
		fail("free first context");
}

static void test_mapping(int fd, const struct drm_gcn_gem_create *bo,
			 void **mapping_out)
{
	struct drm_gcn_gem_mmap mmap_args = {
		.handle = bo->handle,
	};
	uint8_t *mapping;
	void *oversized;
	size_t i;

	if (ioctl(fd, DRM_IOCTL_GCN_GEM_MMAP, &mmap_args)) {
		fail("GEM_MMAP offset query");
		return;
	}
	mapping = mmap(NULL, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
		       mmap_args.offset);
	if (mapping == MAP_FAILED) {
		fail("mmap MEM1 object");
		return;
	}
	for (i = 0; i < bo->size; i++) {
		if (mapping[i]) {
			fprintf(stderr, "FAIL: new MEM1 object not zero at %zu\n", i);
			failures++;
			break;
		}
	}
	for (i = 0; i < bo->size; i++)
		mapping[i] = (uint8_t)(i * 37U + 11U);
	for (i = 0; i < bo->size; i++) {
		if (mapping[i] != (uint8_t)(i * 37U + 11U)) {
			fprintf(stderr, "FAIL: MEM1 readback mismatch at %zu\n", i);
			failures++;
			break;
		}
	}

	errno = 0;
	oversized = mmap(NULL, bo->size + 4096, PROT_READ | PROT_WRITE,
			 MAP_SHARED, fd, mmap_args.offset);
	if (oversized != MAP_FAILED) {
		munmap(oversized, bo->size + 4096);
		errno = 0;
		fail("oversized MEM1 mmap unexpectedly succeeded");
	} else if (errno != EINVAL) {
		fail("oversized MEM1 mmap should return EINVAL");
	}
	*mapping_out = mapping;
}

static void test_wait_and_prime(int fd, uint32_t handle)
{
	struct drm_gcn_wait wait_args = {
		.handle = handle,
		.flags = DRM_GCN_WAIT_WRITE,
		.timeout_ns = monotonic_ns() + 1000000000ULL,
	};
	struct drm_prime_handle prime = {
		.handle = handle,
		.flags = DRM_CLOEXEC | DRM_RDWR,
	};

	if (ioctl(fd, DRM_IOCTL_GCN_WAIT, &wait_args))
		fail("wait on idle MEM1 object");
	errno = 0;
	if (!ioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime) ||
	    errno != EOPNOTSUPP)
		fail("PRIME export should return EOPNOTSUPP");
}

static void test_syncobj(int fd)
{
	struct drm_syncobj_create create = {
		.flags = DRM_SYNCOBJ_CREATE_SIGNALED,
	};
	struct drm_syncobj_destroy destroy;

	if (ioctl(fd, DRM_IOCTL_SYNCOBJ_CREATE, &create)) {
		fail("create core DRM sync object");
		return;
	}
	if (!create.handle)
		fail_value("sync object handle", create.handle, 1);
	destroy.handle = create.handle;
	destroy.pad = 0;
	if (ioctl(fd, DRM_IOCTL_SYNCOBJ_DESTROY, &destroy))
		fail("destroy core DRM sync object");
}

static void test_provider(int fd, int other_fd, uint64_t free_before)
{
	struct drm_gcn_gem_create objects[MAX_OBJECTS];
	void *mapping = NULL;
	uint64_t free_after;
	uint64_t expected;
	unsigned int count = 0;
	unsigned int i;

	test_contexts(fd, other_fd);

	if (create_bo(fd, &objects[count])) {
		fail("create first MEM1 object");
		return;
	}
	if (objects[count].size != TEST_WIDTH * TEST_HEIGHT * 2ULL)
		fail_value("MEM1 object size", objects[count].size,
			   TEST_WIDTH * TEST_HEIGHT * 2ULL);
	count++;
	test_mapping(fd, &objects[0], &mapping);
	test_wait_and_prime(fd, objects[0].handle);

	while (count < MAX_OBJECTS) {
		if (!create_bo(fd, &objects[count])) {
			count++;
			continue;
		}
		if (errno != ENOSPC)
			fail("allocator exhaustion should return ENOSPC");
		break;
	}
	if (count == MAX_OBJECTS)
		fail_value("allocator did not exhaust", count, MAX_OBJECTS - 1);

	if (get_param(fd, DRM_GCN_PARAM_MEM1_FREE_BYTES, &free_after)) {
		fail("query free bytes after allocation");
	} else {
		expected = free_before - count * objects[0].size;
		if (free_after != expected)
			fail_value("free-byte accounting after allocation", free_after,
				   expected);
	}

	if (mapping && munmap(mapping, objects[0].size))
		fail("munmap MEM1 object");
	for (i = 0; i < count; i++) {
		if (close_bo(fd, objects[i].handle))
			fail("close MEM1 object");
	}
	if (get_param(fd, DRM_GCN_PARAM_MEM1_FREE_BYTES, &free_after)) {
		fail("query free bytes after release");
	} else if (free_after != free_before) {
		fail_value("free-byte accounting after release", free_after,
			   free_before);
	}

	printf("MEM1: allocated %u x %llu bytes before ENOSPC\n", count,
	       (unsigned long long)objects[0].size);
}

int main(int argc, char **argv)
{
	const char *node = argc > 1 ? argv[1] : "/dev/dri/renderD128";
	uint64_t provider = 0;
	uint64_t abi = 0;
	uint64_t total = 0;
	uint64_t free_bytes = 0;
	uint64_t alignment = 0;
	uint64_t formats = 0;
	uint64_t layouts = 0;
	uint64_t features = 0;
	uint64_t max_width = 0;
	uint64_t max_height = 0;
	int other_fd;
	int fd;

	fd = open(node, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		fail("open render node");
		return EXIT_FAILURE;
	}
	other_fd = open(node, O_RDWR | O_CLOEXEC);
	if (other_fd < 0) {
		fail("open second render node fd");
		close(fd);
		return EXIT_FAILURE;
	}

	if (get_param(fd, DRM_GCN_PARAM_ABI_VERSION, &abi))
		fail("query ABI version");
	else if (abi != DRM_GCN_RENDER_ABI_VERSION)
		fail_value("ABI version", abi, DRM_GCN_RENDER_ABI_VERSION);
	if (get_param(fd, DRM_GCN_PARAM_PROVIDER_AVAILABLE, &provider))
		fail("query provider availability");
	if (get_param(fd, DRM_GCN_PARAM_FEATURES, &features))
		fail("query features");
	test_syncobj(fd);

	if (!provider) {
		if (features & DRM_GCN_FEATURE_MEM1_GEM)
			fail_value("MEM1 feature without provider", features, 0);
		printf("GCN render ABI %llu: provider absent\n",
		       (unsigned long long)abi);
		test_provider_absent(fd);
	} else {
		if (get_param(fd, DRM_GCN_PARAM_MEM1_TOTAL_BYTES, &total) ||
		    get_param(fd, DRM_GCN_PARAM_MEM1_FREE_BYTES, &free_bytes) ||
		    get_param(fd, DRM_GCN_PARAM_MEM1_ALIGNMENT, &alignment) ||
		    get_param(fd, DRM_GCN_PARAM_MAX_EFB_WIDTH, &max_width) ||
		    get_param(fd, DRM_GCN_PARAM_MAX_EFB_HEIGHT, &max_height) ||
		    get_param(fd, DRM_GCN_PARAM_FORMATS, &formats) ||
		    get_param(fd, DRM_GCN_PARAM_LAYOUTS, &layouts)) {
			fail("query provider capabilities");
		} else {
			printf("GCN render ABI %llu: MEM1 total=%llu free=%llu alignment=%llu\n",
			       (unsigned long long)abi,
			       (unsigned long long)total,
			       (unsigned long long)free_bytes,
			       (unsigned long long)alignment);
			if (!(features & DRM_GCN_FEATURE_MEM1_GEM) ||
			    !(formats & DRM_GCN_FORMAT_RGB565) ||
			    !(layouts & DRM_GCN_LAYOUT_TILED_4X4))
				fail_value("provider capability bits", features, 0);
			if (!alignment || free_bytes > total)
				fail_value("provider capacity", free_bytes, total);
			if (max_width != 640)
				fail_value("provider EFB width", max_width, 640);
			if (max_height != 576)
				fail_value("provider EFB height", max_height, 576);
		}
		test_provider(fd, other_fd, free_bytes);
	}

	close(other_fd);
	close(fd);
	if (failures) {
		fprintf(stderr, "%d render UAPI test(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	puts("PASS: GCN render UAPI");
	return EXIT_SUCCESS;
}
