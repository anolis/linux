// SPDX-License-Identifier: GPL-2.0-only
/* Render into a linear GCN system object, then present it through KMS. */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <drm/gcn_drm.h>

#define SRC_WIDTH 320U
#define SRC_HEIGHT 240U
#define DST_WIDTH 640U
#define DST_HEIGHT 480U
#define TEST_DRM_MODE_CONNECTED 1

static int xioctl(int fd, unsigned long request, void *arg)
{
	int ret;

	do {
		ret = ioctl(fd, request, arg);
	} while (ret < 0 && errno == EINTR);
	return ret;
}

static __u64 user_ptr(const void *ptr)
{
	return (__u64)(uintptr_t)ptr;
}

static void *xcalloc(size_t count, size_t size)
{
	void *ptr = calloc(count, size);

	if (!ptr && count) {
		perror("calloc");
		exit(EXIT_FAILURE);
	}
	return ptr;
}

static uint16_t pattern(unsigned int x, unsigned int y)
{
	static const uint16_t colors[] = {
		0xf800, 0x07e0, 0x001f, 0xffff,
	};
	unsigned int quadrant = (y >= SRC_HEIGHT / 2) * 2 +
				  (x >= SRC_WIDTH / 2);
	uint16_t pixel = colors[quadrant];

	if (x < 4 || x >= SRC_WIDTH - 4 || y < 4 || y >= SRC_HEIGHT - 4)
		pixel = 0xffff;
	else if (!(x % 40) || !(y % 30))
		pixel = 0;
	if (x >= 120 && x < 200 && y >= 90 && y < 150)
		pixel = ((x / 5) ^ (y / 5)) & 1 ? 0xf81f : 0xffe0;

	return pixel;
}

static unsigned int scaled_source(unsigned int dst, unsigned int src_extent,
				  unsigned int dst_extent)
{
	uint64_t numerator = (uint64_t)(2 * dst + 1) * src_extent;
	unsigned int source = numerator / (2 * dst_extent);

	return source < src_extent ? source : src_extent - 1;
}

static int create_linear_bo(int fd, struct drm_gcn_gem_create *bo,
			    unsigned int width, unsigned int height, void **map)
{
	struct drm_gcn_gem_mmap mmap_args = {};

	*bo = (struct drm_gcn_gem_create) {
		.width = width,
		.height = height,
		.format = DRM_GCN_GEM_FORMAT_RGB565,
		.layout = DRM_GCN_GEM_LAYOUT_LINEAR,
		.flags = DRM_GCN_GEM_CREATE_SYSTEM,
	};
	if (xioctl(fd, DRM_IOCTL_GCN_GEM_CREATE, bo) < 0)
		return -1;

	mmap_args.handle = bo->handle;
	if (xioctl(fd, DRM_IOCTL_GCN_GEM_MMAP, &mmap_args) < 0)
		return -1;
	*map = mmap(NULL, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
		    mmap_args.offset);
	return *map == MAP_FAILED ? -1 : 0;
}

static int create_tiled_bo(int fd, struct drm_gcn_gem_create *bo,
			   unsigned int width, unsigned int height, void **map)
{
	struct drm_gcn_gem_mmap mmap_args = {};

	*bo = (struct drm_gcn_gem_create) {
		.width = width,
		.height = height,
		.format = DRM_GCN_GEM_FORMAT_RGB565,
		.layout = DRM_GCN_GEM_LAYOUT_TILED_4X4,
	};
	if (xioctl(fd, DRM_IOCTL_GCN_GEM_CREATE, bo) < 0)
		return -1;

	mmap_args.handle = bo->handle;
	if (xioctl(fd, DRM_IOCTL_GCN_GEM_MMAP, &mmap_args) < 0)
		return -1;
	*map = mmap(NULL, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
		    mmap_args.offset);
	return *map == MAP_FAILED ? -1 : 0;
}

static void close_bo(int fd, struct drm_gcn_gem_create *bo, void *map)
{
	struct drm_gem_close close_args = { .handle = bo->handle };

	if (map != MAP_FAILED)
		munmap(map, bo->size);
	if (bo->handle)
		xioctl(fd, DRM_IOCTL_GEM_CLOSE, &close_args);
}

static int get_resources(int fd, struct drm_mode_card_res *res,
			 __u32 **crtcs, __u32 **connectors)
{
	memset(res, 0, sizeof(*res));
	if (xioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, res) < 0)
		return -1;
	*crtcs = xcalloc(res->count_crtcs, sizeof(**crtcs));
	*connectors = xcalloc(res->count_connectors, sizeof(**connectors));
	res->crtc_id_ptr = user_ptr(*crtcs);
	res->connector_id_ptr = user_ptr(*connectors);
	res->count_fbs = 0;
	res->count_encoders = 0;
	if (xioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, res) < 0)
		return -1;
	return 0;
}

static int get_connector(int fd, __u32 id,
			 struct drm_mode_get_connector *connector,
			 struct drm_mode_modeinfo **modes)
{
	__u32 *encoders;
	__u32 *props;
	__u64 *values;

	memset(connector, 0, sizeof(*connector));
	connector->connector_id = id;
	if (xioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, connector) < 0)
		return -1;

	*modes = xcalloc(connector->count_modes, sizeof(**modes));
	encoders = xcalloc(connector->count_encoders, sizeof(*encoders));
	props = xcalloc(connector->count_props, sizeof(*props));
	values = xcalloc(connector->count_props, sizeof(*values));
	connector->modes_ptr = user_ptr(*modes);
	connector->encoders_ptr = user_ptr(encoders);
	connector->props_ptr = user_ptr(props);
	connector->prop_values_ptr = user_ptr(values);
	if (xioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, connector) < 0) {
		free(*modes);
		*modes = NULL;
		free(encoders);
		free(props);
		free(values);
		return -1;
	}
	free(encoders);
	free(props);
	free(values);
	return 0;
}

static int select_output(int fd, const struct drm_mode_card_res *res,
			 const __u32 *connector_ids, __u32 *connector_id,
			 struct drm_mode_modeinfo *mode)
{
	unsigned int i;

	for (i = 0; i < res->count_connectors; i++) {
		struct drm_mode_get_connector connector;
		struct drm_mode_modeinfo *modes = NULL;
		unsigned int j;

		if (get_connector(fd, connector_ids[i], &connector, &modes) < 0)
			continue;
		if (connector.connection != TEST_DRM_MODE_CONNECTED) {
			free(modes);
			continue;
		}
		for (j = 0; j < connector.count_modes; j++) {
			if (modes[j].hdisplay == DST_WIDTH &&
			    modes[j].vdisplay == DST_HEIGHT)
				break;
		}
		if (j < connector.count_modes) {
			*connector_id = connector.connector_id;
			*mode = modes[j];
			free(modes);
			return 0;
		}
		free(modes);
	}
	errno = ENODEV;
	return -1;
}

int main(int argc, char **argv)
{
	const char *card = argc > 1 ? argv[1] : "/dev/dri/card0";
	unsigned int hold_seconds = argc > 2 ? strtoul(argv[2], NULL, 10) : 5;
	const char *scene = argc > 3 ? argv[3] : "pattern";
	int triangle_scene = !strcmp(scene, "triangle");
	struct drm_gcn_gem_create src = {}, dst = {};
	struct drm_gcn_ctx_create ctx = {};
	struct drm_gcn_ctx_free free_ctx = {};
	struct drm_gcn_blit_scaled blit = {};
	struct drm_gcn_submit fill = {
		.op = DRM_GCN_RENDER_OP_FILL_RGB565,
	};
	struct drm_gcn_draw_triangle draw = {
		.vertices = {
			{ SRC_WIDTH / 2, 20,
			  DRM_GCN_RGBA8(0xff, 0, 0, 0xff) },
			{ 24, SRC_HEIGHT - 20,
			  DRM_GCN_RGBA8(0, 0, 0xff, 0xff) },
			{ SRC_WIDTH - 24, SRC_HEIGHT - 20,
			  DRM_GCN_RGBA8(0, 0xff, 0, 0xff) },
		},
	};
	struct drm_mode_card_res resources;
	struct drm_mode_modeinfo mode;
	struct drm_mode_crtc old_crtc = {};
	struct drm_mode_crtc set_crtc = {};
	struct drm_mode_fb_cmd fb = {};
	__u32 *connector_ids = NULL;
	__u32 *crtc_ids = NULL;
	__u32 connector_id = 0;
	uint16_t *src_map = MAP_FAILED;
	uint16_t *dst_map = MAP_FAILED;
	unsigned int x;
	unsigned int y;
	int displayed = 0;
	int fd = -1;
	int ret = EXIT_FAILURE;

	if (strcmp(scene, "pattern") && !triangle_scene) {
		fprintf(stderr, "unknown scene '%s' (expected pattern or triangle)\n",
			scene);
		goto out;
	}

	fd = open(card, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		perror(card);
		goto out;
	}
	if (xioctl(fd, DRM_IOCTL_SET_MASTER, NULL) < 0 && errno != EINVAL) {
		perror("DRM_IOCTL_SET_MASTER");
		goto out;
	}
	if ((triangle_scene ?
	     create_tiled_bo(fd, &src, SRC_WIDTH, SRC_HEIGHT,
			     (void **)&src_map) :
	     create_linear_bo(fd, &src, SRC_WIDTH, SRC_HEIGHT,
			      (void **)&src_map)) < 0 ||
	    create_linear_bo(fd, &dst, DST_WIDTH, DST_HEIGHT,
			     (void **)&dst_map) < 0) {
		perror("create render objects");
		goto out;
	}
	if (!triangle_scene) {
		for (y = 0; y < SRC_HEIGHT; y++)
			for (x = 0; x < SRC_WIDTH; x++)
				src_map[(size_t)y * SRC_WIDTH + x] =
					pattern(x, y);
	}
	memset(dst_map, 0x5a, DST_WIDTH * DST_HEIGHT * sizeof(*dst_map));

	if (xioctl(fd, DRM_IOCTL_GCN_CTX_CREATE, &ctx) < 0) {
		perror("DRM_IOCTL_GCN_CTX_CREATE");
		goto out;
	}
	if (triangle_scene) {
		fill.ctx_id = ctx.id;
		fill.dst_handle = src.handle;
		if (xioctl(fd, DRM_IOCTL_GCN_SUBMIT, &fill) < 0) {
			perror("seed triangle target");
			goto out;
		}
		draw.ctx_id = ctx.id;
		draw.dst_handle = src.handle;
		if (xioctl(fd, DRM_IOCTL_GCN_DRAW_TRIANGLE, &draw) < 0) {
			perror("DRM_IOCTL_GCN_DRAW_TRIANGLE");
			goto out;
		}
	}
	blit = (struct drm_gcn_blit_scaled) {
		.ctx_id = ctx.id,
		.src_handle = src.handle,
		.dst_handle = dst.handle,
		.src_width = SRC_WIDTH,
		.src_height = SRC_HEIGHT,
		.dst_width = DST_WIDTH,
		.dst_height = DST_HEIGHT,
	};
	if (xioctl(fd, DRM_IOCTL_GCN_BLIT_SCALED, &blit) < 0) {
		perror("DRM_IOCTL_GCN_BLIT_SCALED");
		goto out;
	}
	if (triangle_scene) {
		unsigned int black = 0;
		unsigned int red = 0;
		unsigned int green = 0;
		unsigned int blue = 0;
		unsigned int distinct = 0;
		unsigned char *seen = xcalloc(1U << 16, sizeof(*seen));

		for (y = 0; y < DST_HEIGHT; y++) {
			for (x = 0; x < DST_WIDTH; x++) {
				uint16_t pixel = dst_map[(size_t)y * DST_WIDTH + x];
				unsigned int r = (pixel >> 11) & 0x1f;
				unsigned int g = (pixel >> 5) & 0x3f;
				unsigned int b = pixel & 0x1f;

				if (!seen[pixel]) {
					seen[pixel] = 1;
					distinct++;
				}
				if (!pixel)
					black++;
				else if (r > 20 && g < 20 && b < 10)
					red++;
				else if (g > 40 && r < 10 && b < 10)
					green++;
				else if (b > 20 && r < 10 && g < 20)
					blue++;
			}
		}
		free(seen);
		if (black < 100000 || red < 500 || green < 500 || blue < 500 ||
		    distinct < 128) {
			fprintf(stderr,
				"triangle sanity failed: black=%u red=%u green=%u blue=%u distinct=%u\n",
				black, red, green, blue, distinct);
			goto out;
		}
		printf("gcn-kms-render-test: triangle black=%u red=%u green=%u blue=%u distinct=%u\n",
		       black, red, green, blue, distinct);
	} else {
		for (y = 0; y < DST_HEIGHT; y++) {
			for (x = 0; x < DST_WIDTH; x++) {
				unsigned int sx = scaled_source(x, SRC_WIDTH,
							DST_WIDTH);
				unsigned int sy = scaled_source(y, SRC_HEIGHT,
							DST_HEIGHT);
				uint16_t expected = pattern(sx, sy);

				if (dst_map[(size_t)y * DST_WIDTH + x] != expected) {
					fprintf(stderr,
						"pixel mismatch at (%u,%u): got=%04x expected=%04x\n",
						x, y,
						dst_map[(size_t)y * DST_WIDTH + x],
						expected);
					goto out;
				}
			}
		}
		puts("gcn-kms-render-test: all 307200 linear pixels passed");
	}

	if (get_resources(fd, &resources, &crtc_ids, &connector_ids) < 0 ||
	    !resources.count_crtcs ||
	    select_output(fd, &resources, connector_ids, &connector_id,
			  &mode) < 0) {
		perror("select DRM output");
		goto out;
	}
	old_crtc.crtc_id = crtc_ids[0];
	if (xioctl(fd, DRM_IOCTL_MODE_GETCRTC, &old_crtc) < 0) {
		perror("DRM_IOCTL_MODE_GETCRTC");
		goto out;
	}
	fb = (struct drm_mode_fb_cmd) {
		.width = DST_WIDTH,
		.height = DST_HEIGHT,
		.pitch = DST_WIDTH * sizeof(uint16_t),
		.bpp = 16,
		.depth = 16,
		.handle = dst.handle,
	};
	if (xioctl(fd, DRM_IOCTL_MODE_ADDFB, &fb) < 0) {
		perror("DRM_IOCTL_MODE_ADDFB");
		goto out;
	}
	set_crtc = (struct drm_mode_crtc) {
		.set_connectors_ptr = user_ptr(&connector_id),
		.count_connectors = 1,
		.crtc_id = crtc_ids[0],
		.fb_id = fb.fb_id,
		.mode_valid = 1,
		.mode = mode,
	};
	if (xioctl(fd, DRM_IOCTL_MODE_SETCRTC, &set_crtc) < 0) {
		perror("DRM_IOCTL_MODE_SETCRTC");
		goto out;
	}
	displayed = 1;
	printf("gcn-kms-render-test: displaying GX %s scene for %u seconds\n",
	       scene, hold_seconds);
	fflush(stdout);
	sleep(hold_seconds);
	ret = EXIT_SUCCESS;

out:
	if (displayed) {
		old_crtc.set_connectors_ptr = user_ptr(&connector_id);
		old_crtc.count_connectors = 1;
		if (xioctl(fd, DRM_IOCTL_MODE_SETCRTC, &old_crtc) < 0) {
			perror("restore DRM CRTC");
			ret = EXIT_FAILURE;
		} else {
			puts("gcn-kms-render-test: restored previous console framebuffer");
		}
	}
	if (fb.fb_id)
		xioctl(fd, DRM_IOCTL_MODE_RMFB, &fb.fb_id);
	if (ctx.id) {
		free_ctx.id = ctx.id;
		xioctl(fd, DRM_IOCTL_GCN_CTX_FREE, &free_ctx);
	}
	close_bo(fd, &dst, dst_map);
	close_bo(fd, &src, src_map);
	free(crtc_ids);
	free(connector_ids);
	if (fd >= 0)
		close(fd);
	return ret;
}
