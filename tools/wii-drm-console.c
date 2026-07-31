// SPDX-License-Identifier: GPL-2.0-only
/* Mirror a Linux virtual console onto the Wii VI DRM/KMS device. */

#define main wii_drm_test_main
#include "wii-drm-test.c"
#undef main

#include <time.h>

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define BIT(bit) (1U << (bit))

/* Reuse the kernel's built-in GPL VGA font without kernel-only headers. */
#define _VIDEO_FONT_H
#define _LINUX_MODULE_H
#define __packed __attribute__((packed))
#define VGA8x16_IDX 1
#define EXPORT_SYMBOL(symbol)
struct font_desc {
	int idx;
	const char *name;
	unsigned int width;
	unsigned int height;
	unsigned int charcount;
	const void *data;
	int pref;
};
struct font_data {
	unsigned int extra[4];
	const unsigned char data[];
} __packed;
#include "../lib/fonts/font_8x16.c"

#define CONSOLE_CELL_WIDTH 8
#define CONSOLE_CELL_HEIGHT 16
#define CONSOLE_COLUMNS (TEST_WIDTH / CONSOLE_CELL_WIDTH)
#define CONSOLE_ROWS (TEST_HEIGHT / CONSOLE_CELL_HEIGHT)
#define CONSOLE_CELL_BYTES 2
#define CONSOLE_CURSOR_PERIOD_MS 500
#define CONSOLE_POLL_MS 100

struct console_screen {
	uint8_t rows;
	uint8_t columns;
	uint8_t cursor_x;
	uint8_t cursor_y;
	uint16_t cells[CONSOLE_ROWS * CONSOLE_COLUMNS];
};

static const uint32_t console_palette[16] = {
	0x00000000, 0x000000aa, 0x0000aa00, 0x0000aaaa,
	0x00aa0000, 0x00aa00aa, 0x00aa5500, 0x00aaaaaa,
	0x00555555, 0x005555ff, 0x0055ff55, 0x0055ffff,
	0x00ff5555, 0x00ff55ff, 0x00ffff55, 0x00ffffff,
};

static int read_full(int fd, void *buffer, size_t length)
{
	uint8_t *bytes = buffer;
	size_t offset = 0;

	while (offset < length) {
		ssize_t ret = read(fd, bytes + offset, length - offset);

		if (ret < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (!ret) {
			errno = EIO;
			return -1;
		}
		offset += ret;
	}
	return 0;
}

static int read_console(int fd, struct console_screen *screen)
{
	uint8_t header[4];
	size_t cells;

	if (lseek(fd, 0, SEEK_SET) < 0 || read_full(fd, header, sizeof(header)))
		return -1;
	if (!header[0] || !header[1] || header[0] > CONSOLE_ROWS ||
	    header[1] > CONSOLE_COLUMNS) {
		errno = ERANGE;
		return -1;
	}

	screen->rows = header[0];
	screen->columns = header[1];
	screen->cursor_x = header[2];
	screen->cursor_y = header[3];
	cells = (size_t)screen->rows * screen->columns;
	if (read_full(fd, screen->cells, cells * CONSOLE_CELL_BYTES))
		return -1;
	memset(screen->cells + cells, 0,
	       (ARRAY_SIZE(screen->cells) - cells) * sizeof(screen->cells[0]));
	return 0;
}

static void draw_console(void *map, __u32 pitch,
			 const struct console_screen *screen,
			 int cursor_visible,
			 enum test_pixel_format format)
{
	unsigned int x_offset =
		(CONSOLE_COLUMNS - screen->columns) * CONSOLE_CELL_WIDTH / 2;
	unsigned int y_offset =
		(CONSOLE_ROWS - screen->rows) * CONSOLE_CELL_HEIGHT / 2;
	unsigned int row;
	unsigned int column;

	memset(map, 0, (size_t)pitch * TEST_HEIGHT);
	for (row = 0; row < screen->rows; row++) {
		for (column = 0; column < screen->columns; column++) {
			uint16_t cell = screen->cells[row * screen->columns + column];
			uint8_t character = cell & 0xff;
			uint8_t attribute = cell >> 8;
			uint32_t foreground = console_palette[attribute & 0x0f];
			uint32_t background = console_palette[(attribute >> 4) & 0x07];
			const uint8_t *glyph =
				font_vga_8x16.data + character * CONSOLE_CELL_HEIGHT;
			unsigned int glyph_y;

			if (cursor_visible && column == screen->cursor_x &&
			    row == screen->cursor_y) {
				uint32_t swap = foreground;

				foreground = background;
				background = swap;
			}

			for (glyph_y = 0; glyph_y < CONSOLE_CELL_HEIGHT;
			     glyph_y++) {
				uint8_t *pixel_row = (uint8_t *)map +
					(y_offset + row * CONSOLE_CELL_HEIGHT + glyph_y) *
					pitch;
				unsigned int pixel_x =
					x_offset + column * CONSOLE_CELL_WIDTH;
				unsigned int glyph_x;

				for (glyph_x = 0; glyph_x < CONSOLE_CELL_WIDTH;
				     glyph_x++) {
					uint32_t color =
						glyph[glyph_y] & BIT(7 - glyph_x) ?
						foreground : background;

					if (format == TEST_FORMAT_RGB565) {
						uint16_t rgb565 = xrgb8888_to_rgb565(color);
						size_t offset =
							2 * (pixel_x + glyph_x);

						pixel_row[offset] = rgb565;
						pixel_row[offset + 1] = rgb565 >> 8;
					} else {
						((uint32_t *)pixel_row)[pixel_x + glyph_x] =
							color;
					}
				}
			}
		}
	}
}

static int page_flip(int fd, __u32 crtc_id, __u32 fb_id, __u64 serial)
{
	struct drm_mode_crtc_page_flip flip = {
		.crtc_id = crtc_id,
		.fb_id = fb_id,
		.flags = DRM_MODE_PAGE_FLIP_EVENT,
		.user_data = serial,
	};
	__u32 sequence;

	if (xioctl(fd, DRM_IOCTL_MODE_PAGE_FLIP, &flip) < 0)
		return -1;
	return wait_flip_event(fd, serial, &sequence);
}

static uint64_t monotonic_ms(void)
{
	struct timespec time;

	if (clock_gettime(CLOCK_MONOTONIC, &time) < 0)
		return 0;
	return (uint64_t)time.tv_sec * 1000 + time.tv_nsec / 1000000;
}

static void console_usage(const char *program)
{
	fprintf(stderr, "Usage: %s [--format rgb565|xrgb8888] ", program);
	fprintf(stderr, "[--vcsa DEVICE] [CARD]\n");
}

int main(int argc, char **argv)
{
	const char *card = "/dev/dri/card0";
	const char *vcsa = "/dev/vcsa1";
	struct test_buffer buffers[2] = {
		{ .map = MAP_FAILED },
		{ .map = MAP_FAILED },
	};
	struct drm_mode_card_res resources;
	struct drm_mode_modeinfo mode;
	struct drm_mode_crtc crtc = { };
	struct console_screen current = { };
	struct console_screen previous = { };
	__u32 *connector_ids = NULL;
	__u32 *crtc_ids = NULL;
	__u32 connector_id;
	unsigned int created = 0;
	unsigned int visible = 0;
	unsigned int i;
	__u64 serial = 1;
	enum test_pixel_format format = TEST_FORMAT_RGB565;
	int previous_cursor = -1;
	int card_set = 0;
	int drm_fd = -1;
	int vcsa_fd = -1;
	int status = EXIT_FAILURE;

	for (i = 1; i < (unsigned int)argc; i++) {
		if (!strcmp(argv[i], "--format")) {
			if (++i >= (unsigned int)argc) {
				console_usage(argv[0]);
				return EXIT_FAILURE;
			}
			if (!strcmp(argv[i], "rgb565"))
				format = TEST_FORMAT_RGB565;
			else if (!strcmp(argv[i], "xrgb8888"))
				format = TEST_FORMAT_XRGB8888;
			else {
				console_usage(argv[0]);
				return EXIT_FAILURE;
			}
			continue;
		}
		if (!strcmp(argv[i], "--vcsa")) {
			if (++i >= (unsigned int)argc) {
				console_usage(argv[0]);
				return EXIT_FAILURE;
			}
			vcsa = argv[i];
			continue;
		}
		if (!strcmp(argv[i], "--help")) {
			console_usage(argv[0]);
			return EXIT_SUCCESS;
		}
		if (argv[i][0] == '-' || card_set) {
			console_usage(argv[0]);
			return EXIT_FAILURE;
		}
		card = argv[i];
		card_set = 1;
	}

	vcsa_fd = open(vcsa, O_RDONLY | O_CLOEXEC);
	if (vcsa_fd < 0) {
		perror(vcsa);
		goto out;
	}
	if (read_console(vcsa_fd, &current) < 0) {
		perror("read virtual console");
		goto out;
	}

	drm_fd = open(card, O_RDWR | O_CLOEXEC);
	if (drm_fd < 0) {
		perror(card);
		goto out;
	}
	if (ioctl(drm_fd, DRM_IOCTL_SET_MASTER, 0) < 0 && errno != EINVAL) {
		perror("DRM_IOCTL_SET_MASTER");
		goto out;
	}
	if (get_resources(drm_fd, &resources, &crtc_ids, &connector_ids) < 0 ||
	    !resources.count_crtcs || !resources.count_connectors) {
		perror("DRM_IOCTL_MODE_GETRESOURCES");
		goto out;
	}
	if (select_output(drm_fd, &resources, connector_ids, &connector_id,
			  &mode) < 0) {
		perror("no connected DRM output");
		goto out;
	}

	for (i = 0; i < ARRAY_SIZE(buffers); i++) {
		if (create_buffer(drm_fd, &buffers[i], -1, format) < 0) {
			created = i + 1;
			perror("create dumb framebuffer");
			goto out;
		}
		created = i + 1;
	}
	draw_console(buffers[0].map, buffers[0].create.pitch, &current, 1,
		     format);
	previous = current;
	previous_cursor = 1;

	crtc.set_connectors_ptr = user_ptr(&connector_id);
	crtc.count_connectors = 1;
	crtc.crtc_id = crtc_ids[0];
	crtc.fb_id = buffers[0].fb.fb_id;
	crtc.mode_valid = 1;
	crtc.mode = mode;
	if (xioctl(drm_fd, DRM_IOCTL_MODE_SETCRTC, &crtc) < 0) {
		perror("DRM_IOCTL_MODE_SETCRTC");
		goto out;
	}

	printf("wii-drm-console: active %ux%u %s, mirroring %s (%ux%u)\n",
	       mode.hdisplay, mode.vdisplay,
	       format == TEST_FORMAT_RGB565 ? "rgb565" : "xrgb8888",
	       vcsa, current.columns, current.rows);
	fflush(stdout);
	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);

	while (!stop) {
		struct pollfd poll_fd = {
			.fd = vcsa_fd,
			.events = POLLPRI,
		};
		int cursor_visible;
		int changed;
		int ret;

		do {
			ret = poll(&poll_fd, 1, CONSOLE_POLL_MS);
		} while (ret < 0 && errno == EINTR && !stop);
		if (stop)
			break;
		if (ret < 0) {
			perror("poll virtual console");
			goto out;
		}
		if (read_console(vcsa_fd, &current) < 0) {
			perror("read virtual console");
			goto out;
		}
		cursor_visible =
			(monotonic_ms() / CONSOLE_CURSOR_PERIOD_MS) % 2 == 0;
		changed = memcmp(&current, &previous, sizeof(current)) ||
			  cursor_visible != previous_cursor;
		if (!changed)
			continue;

		visible ^= 1;
		draw_console(buffers[visible].map,
			     buffers[visible].create.pitch, &current,
			     cursor_visible, format);
		if (page_flip(drm_fd, crtc.crtc_id,
			      buffers[visible].fb.fb_id, serial++) < 0) {
			perror("page flip");
			goto out;
		}
		previous = current;
		previous_cursor = cursor_visible;
	}
	status = EXIT_SUCCESS;

out:
	while (created)
		destroy_buffer(drm_fd, &buffers[--created]);
	free(crtc_ids);
	free(connector_ids);
	if (drm_fd >= 0)
		close(drm_fd);
	if (vcsa_fd >= 0)
		close(vcsa_fd);
	return status;
}
