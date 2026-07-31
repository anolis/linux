// SPDX-License-Identifier: GPL-2.0-only
/* Minimal raw-ioctl KMS test client for the Wii VI DRM driver. */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <drm/drm.h>
#include <drm/drm_mode.h>

#define TEST_WIDTH 640
#define TEST_HEIGHT 480
#define TEST_DRM_MODE_CONNECTED 1

static volatile sig_atomic_t stop;

static void handle_signal(int signo)
{
	(void)signo;
	stop = 1;
}

static int xioctl(int fd, unsigned long request, void *arg)
{
	int ret;

	do {
		ret = ioctl(fd, request, arg);
	} while (ret < 0 && errno == EINTR);
	return ret;
}

static void *xcalloc(size_t count, size_t size)
{
	void *ptr;

	if (!count)
		return NULL;
	ptr = calloc(count, size);
	if (!ptr) {
		perror("calloc");
		exit(EXIT_FAILURE);
	}
	return ptr;
}

static __u64 user_ptr(const void *ptr)
{
	return (__u64)(uintptr_t)ptr;
}

static int get_resources(int fd, struct drm_mode_card_res *res,
			 __u32 **crtcs, __u32 **connectors)
{
	__u32 *fbs;
	__u32 *encoders;

	memset(res, 0, sizeof(*res));
	if (xioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, res) < 0)
		return -1;

	fbs = xcalloc(res->count_fbs, sizeof(*fbs));
	*crtcs = xcalloc(res->count_crtcs, sizeof(**crtcs));
	*connectors = xcalloc(res->count_connectors, sizeof(**connectors));
	encoders = xcalloc(res->count_encoders, sizeof(*encoders));
	res->fb_id_ptr = user_ptr(fbs);
	res->crtc_id_ptr = user_ptr(*crtcs);
	res->connector_id_ptr = user_ptr(*connectors);
	res->encoder_id_ptr = user_ptr(encoders);

	if (xioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, res) < 0) {
		free(fbs);
		free(encoders);
		return -1;
	}

	free(fbs);
	free(encoders);
	return 0;
}

static int get_connector(int fd, __u32 connector_id,
			 struct drm_mode_get_connector *connector,
			 struct drm_mode_modeinfo **modes)
{
	__u32 *props;
	__u32 *encoders;
	__u64 *prop_values;

	memset(connector, 0, sizeof(*connector));
	connector->connector_id = connector_id;
	if (xioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, connector) < 0)
		return -1;

	*modes = xcalloc(connector->count_modes, sizeof(**modes));
	props = xcalloc(connector->count_props, sizeof(*props));
	prop_values = xcalloc(connector->count_props, sizeof(*prop_values));
	encoders = xcalloc(connector->count_encoders, sizeof(*encoders));
	connector->modes_ptr = user_ptr(*modes);
	connector->props_ptr = user_ptr(props);
	connector->prop_values_ptr = user_ptr(prop_values);
	connector->encoders_ptr = user_ptr(encoders);

	if (xioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, connector) < 0) {
		free(*modes);
		*modes = NULL;
		free(props);
		free(prop_values);
		free(encoders);
		return -1;
	}

	free(props);
	free(prop_values);
	free(encoders);
	return 0;
}

static int select_output(int fd, const struct drm_mode_card_res *res,
			 const __u32 *connector_ids, __u32 *connector_id,
			 struct drm_mode_modeinfo *mode)
{
	unsigned int i;

	for (i = 0; i < res->count_connectors; i++) {
		struct drm_mode_get_connector connector;
		struct drm_mode_modeinfo *modes;
		unsigned int j;

		if (get_connector(fd, connector_ids[i], &connector, &modes) < 0)
			continue;
		if (connector.connection != TEST_DRM_MODE_CONNECTED ||
		    !connector.count_modes) {
			free(modes);
			continue;
		}

		for (j = 0; j < connector.count_modes; j++) {
			if (modes[j].hdisplay == TEST_WIDTH &&
			    modes[j].vdisplay == TEST_HEIGHT)
				break;
		}
		if (j == connector.count_modes)
			j = 0;
		*connector_id = connector.connector_id;
		*mode = modes[j];
		free(modes);
		return 0;
	}

	errno = ENODEV;
	return -1;
}

static void draw_pattern(void *map, __u32 pitch)
{
	static const uint32_t colors[4] = {
		0x00ff2020, 0x0020ff20, 0x002040ff, 0x00ffffff,
	};
	unsigned int x;
	unsigned int y;

	for (y = 0; y < TEST_HEIGHT; y++) {
		uint32_t *row = (uint32_t *)((uint8_t *)map +
					       (size_t)y * pitch);

		for (x = 0; x < TEST_WIDTH; x++) {
			unsigned int quadrant = (y >= TEST_HEIGHT / 2) * 2 +
						  (x >= TEST_WIDTH / 2);
			uint32_t pixel = colors[quadrant];

			if (x < 8 || x >= TEST_WIDTH - 8 || y < 8 ||
			    y >= TEST_HEIGHT - 8)
				pixel = 0x00ffffff;
			else if ((x % 80) < 3 || (y % 60) < 3)
				pixel = 0x00000000;
			if (x >= 240 && x < 400 && y >= 180 && y < 300)
				pixel = ((x / 10) ^ (y / 10)) & 1 ?
					0x00ff00ff : 0x0000ffff;
			row[x] = pixel;
		}
	}
}

int main(int argc, char **argv)
{
	const char *card = argc > 1 ? argv[1] : "/dev/dri/card0";
	struct drm_mode_create_dumb create = {
		.width = TEST_WIDTH,
		.height = TEST_HEIGHT,
		.bpp = 32,
	};
	struct drm_mode_map_dumb map_req = { };
	struct drm_mode_destroy_dumb destroy = { };
	struct drm_mode_card_res res;
	struct drm_mode_modeinfo mode;
	struct drm_mode_fb_cmd fb = { };
	struct drm_mode_crtc crtc = { };
	__u32 *connector_ids = NULL;
	__u32 *crtc_ids = NULL;
	__u32 connector_id;
	void *map = MAP_FAILED;
	int fd = -1;
	int status = EXIT_FAILURE;

	fd = open(card, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		perror(card);
		goto out;
	}
	if (ioctl(fd, DRM_IOCTL_SET_MASTER, 0) < 0 && errno != EINVAL) {
		perror("DRM_IOCTL_SET_MASTER");
		goto out;
	}
	if (get_resources(fd, &res, &crtc_ids, &connector_ids) < 0) {
		perror("DRM_IOCTL_MODE_GETRESOURCES");
		goto out;
	}
	if (!res.count_crtcs || !res.count_connectors) {
		fprintf(stderr, "DRM device exposes no usable CRTC/connector\n");
		goto out;
	}
	if (select_output(fd, &res, connector_ids, &connector_id, &mode) < 0) {
		perror("no connected DRM output");
		goto out;
	}
	if (mode.hdisplay != TEST_WIDTH || mode.vdisplay != TEST_HEIGHT) {
		fprintf(stderr, "unsupported mode %ux%u\n", mode.hdisplay,
			mode.vdisplay);
		goto out;
	}

	if (xioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) {
		perror("DRM_IOCTL_MODE_CREATE_DUMB");
		goto out;
	}
	destroy.handle = create.handle;
	map_req.handle = create.handle;
	if (xioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map_req) < 0) {
		perror("DRM_IOCTL_MODE_MAP_DUMB");
		goto out;
	}
	map = mmap(NULL, create.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
		   map_req.offset);
	if (map == MAP_FAILED) {
		perror("mmap dumb buffer");
		goto out;
	}
	draw_pattern(map, create.pitch);
	if (msync(map, create.size, MS_SYNC) < 0) {
		perror("msync dumb buffer");
		goto out;
	}

	fb.width = TEST_WIDTH;
	fb.height = TEST_HEIGHT;
	fb.pitch = create.pitch;
	fb.bpp = 32;
	fb.depth = 24;
	fb.handle = create.handle;
	if (xioctl(fd, DRM_IOCTL_MODE_ADDFB, &fb) < 0) {
		perror("DRM_IOCTL_MODE_ADDFB");
		goto out;
	}

	crtc.set_connectors_ptr = user_ptr(&connector_id);
	crtc.count_connectors = 1;
	crtc.crtc_id = crtc_ids[0];
	crtc.fb_id = fb.fb_id;
	crtc.mode_valid = 1;
	crtc.mode = mode;
	if (xioctl(fd, DRM_IOCTL_MODE_SETCRTC, &crtc) < 0) {
		perror("DRM_IOCTL_MODE_SETCRTC");
		goto out;
	}

	printf("wii-drm-test: active %ux%u %s crtc=%u connector=%u\n",
	       mode.hdisplay, mode.vdisplay, mode.name, crtc.crtc_id,
	       connector_id);
	printf("wii-drm-test: fb=%u handle=%u pitch=%u size=%llu\n",
	       fb.fb_id, create.handle, create.pitch,
	       (unsigned long long)create.size);
	fflush(stdout);
	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);
	while (!stop)
		pause();
	status = EXIT_SUCCESS;

out:
	if (fb.fb_id && xioctl(fd, DRM_IOCTL_MODE_RMFB, &fb.fb_id) < 0)
		perror("DRM_IOCTL_MODE_RMFB");
	if (map != MAP_FAILED)
		munmap(map, create.size);
	if (destroy.handle &&
	    xioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy) < 0)
		perror("DRM_IOCTL_MODE_DESTROY_DUMB");
	free(crtc_ids);
	free(connector_ids);
	if (fd >= 0)
		close(fd);
	return status;
}
