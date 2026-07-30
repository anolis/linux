// SPDX-License-Identifier: GPL-2.0
/*
 * Sustained RGB565 framebuffer workload for the Wii GX acceleration path.
 *
 * Build this as a static PowerPC binary and run it from an SSH session.  The
 * pattern keeps most of the frame stable while moving high-contrast markers,
 * making tearing, stale frames, duplicated columns, and transfer stalls easy
 * to distinguish visually.
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stop_requested;

static void request_stop(int signal_number)
{
	(void)signal_number;
	stop_requested = 1;
}

static uint16_t rgb565(unsigned int red, unsigned int green, unsigned int blue)
{
	return ((red & 0x1f) << 11) | ((green & 0x3f) << 5) |
	       (blue & 0x1f);
}

static void fill_rect(uint8_t *frame, unsigned int stride,
		      unsigned int width, unsigned int height,
		      unsigned int left, unsigned int top,
		      unsigned int rect_width, unsigned int rect_height,
		      uint16_t colour)
{
	unsigned int x, y;

	if (left >= width || top >= height)
		return;
	if (rect_width > width - left)
		rect_width = width - left;
	if (rect_height > height - top)
		rect_height = height - top;

	for (y = top; y < top + rect_height; y++) {
		uint16_t *row = (uint16_t *)(frame + (size_t)y * stride);

		for (x = left; x < left + rect_width; x++)
			row[x] = colour;
	}
}

static void build_base_frame(uint8_t *frame, unsigned int stride,
			     unsigned int width, unsigned int height)
{
	static const uint16_t bars[] = {
		0xffff, 0xffe0, 0x07ff, 0x07e0,
		0xf81f, 0xf800, 0x001f, 0x4208,
	};
	unsigned int x, y;

	memset(frame, 0, (size_t)stride * height);
	for (y = 0; y < height; y++) {
		uint16_t *row = (uint16_t *)(frame + (size_t)y * stride);

		for (x = 0; x < width; x++) {
			unsigned int band = (uint64_t)x * 8 / width;
			uint16_t colour = bars[band > 7 ? 7 : band];

			if (((x / 16) ^ (y / 16)) & 1)
				colour ^= rgb565(2, 4, 2);
			if (x % 80 < 2 || y % 60 < 2)
				colour = 0;
			row[x] = colour;
		}
	}
}

static void draw_frame(uint8_t *frame, unsigned int stride, unsigned int width,
		       unsigned int height, uint64_t frame_number)
{
	const uint16_t black = rgb565(0, 0, 0);
	const uint16_t white = rgb565(31, 63, 31);
	const uint16_t red = rgb565(31, 0, 0);
	const uint16_t cyan = rgb565(0, 63, 31);
	unsigned int x, y, marker_x, marker_y;

	build_base_frame(frame, stride, width, height);

	marker_x = (frame_number * 7) % (width - 28);
	fill_rect(frame, stride, width, height, marker_x, 0, 28, height,
		  black);
	fill_rect(frame, stride, width, height, marker_x + 4, 0, 20, height,
		  (frame_number & 1) ? white : red);

	marker_y = (frame_number * 3) % (height - 20);
	fill_rect(frame, stride, width, height, 0, marker_y, width, 20, black);
	fill_rect(frame, stride, width, height, 0, marker_y + 4, width, 12,
		  cyan);

	/* A moving diagonal exposes row displacement independently of the bars. */
	for (y = 0; y < height; y++) {
		uint16_t *row = (uint16_t *)(frame + (size_t)y * stride);
		unsigned int diagonal = (y + frame_number * 5) % width;

		for (x = 0; x < 4 && diagonal + x < width; x++)
			row[diagonal + x] = white;
	}

	/* Encode the low 16 frame bits as stable, camera-readable blocks. */
	fill_rect(frame, stride, width, height, 8, 8, 200, 28, black);
	for (x = 0; x < 16; x++)
		fill_rect(frame, stride, width, height, 12 + x * 12, 12,
			  8, 20, (frame_number & (1ULL << x)) ? white : red);
}

static uint64_t monotonic_ns(void)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
		perror("clock_gettime");
		exit(1);
	}
	return (uint64_t)now.tv_sec * 1000000000ULL + now.tv_nsec;
}

static void sleep_until(uint64_t deadline_ns)
{
	struct timespec deadline = {
		.tv_sec = deadline_ns / 1000000000ULL,
		.tv_nsec = deadline_ns % 1000000000ULL,
	};
	int ret;

	do {
		ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME,
				      &deadline, NULL);
	} while (ret == EINTR && !stop_requested);
	if (ret && ret != EINTR) {
		errno = ret;
		perror("clock_nanosleep");
		exit(1);
	}
}

static void tty_message(const char *message)
{
	const char *cursor = message;
	size_t remaining = strlen(message);
	int fd = open("/dev/tty0", O_WRONLY | O_CLOEXEC);

	if (fd < 0)
		return;
	while (remaining) {
		ssize_t written = write(fd, cursor, remaining);

		if (written > 0) {
			cursor += written;
			remaining -= written;
			continue;
		}
		if (written < 0 && errno == EINTR)
			continue;
		break;
	}
	close(fd);
}

static void usage(const char *program)
{
	fprintf(stderr,
		"Usage: %s [--duration SECONDS] [--fps RATE] [--device PATH]\n"
		"Defaults: duration=120, fps=30, device=/dev/fb0\n",
		program);
}

int main(int argc, char **argv)
{
	struct fb_fix_screeninfo fixed;
	struct fb_var_screeninfo variable;
	struct sigaction action = { .sa_handler = request_stop };
	const char *device = "/dev/fb0";
	unsigned int duration = 120;
	unsigned int fps = 30;
	uint64_t start_ns, next_ns, report_ns, end_ns, frame_number = 0;
	uint64_t interval_ns;
	size_t frame_bytes;
	uint8_t *frame, *fb;
	int fd, i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--duration") && i + 1 < argc) {
			duration = strtoul(argv[++i], NULL, 10);
		} else if (!strcmp(argv[i], "--fps") && i + 1 < argc) {
			fps = strtoul(argv[++i], NULL, 10);
		} else if (!strcmp(argv[i], "--device") && i + 1 < argc) {
			device = argv[++i];
		} else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			usage(argv[0]);
			return 0;
		} else {
			usage(argv[0]);
			return 2;
		}
	}
	if (!fps || fps > 60 || duration > 86400) {
		fprintf(stderr, "fps must be 1..60 and duration must be 0..86400\n");
		return 2;
	}

	fd = open(device, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		perror(device);
		return 1;
	}
	if (ioctl(fd, FBIOGET_FSCREENINFO, &fixed) < 0 ||
	    ioctl(fd, FBIOGET_VSCREENINFO, &variable) < 0) {
		perror("framebuffer ioctl");
		close(fd);
		return 1;
	}
	if (variable.bits_per_pixel != 16 || variable.red.offset != 11 ||
	    variable.red.length != 5 || variable.green.offset != 5 ||
	    variable.green.length != 6 || variable.blue.offset != 0 ||
	    variable.blue.length != 5 ||
	    fixed.line_length < variable.xres * sizeof(uint16_t)) {
		fprintf(stderr,
			"unsupported framebuffer: %ux%u bpp=%u stride=%u rgba=%u/%u,%u/%u,%u/%u\n",
			variable.xres, variable.yres, variable.bits_per_pixel,
			fixed.line_length, variable.red.length, variable.red.offset,
			variable.green.length, variable.green.offset,
			variable.blue.length, variable.blue.offset);
		close(fd);
		return 1;
	}

	frame_bytes = (size_t)fixed.line_length * variable.yres;
	fb = mmap(NULL, frame_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (fb == MAP_FAILED) {
		perror("mmap framebuffer");
		close(fd);
		return 1;
	}
	frame = malloc(frame_bytes);
	if (!frame) {
		fprintf(stderr, "unable to allocate %zu-byte frame buffer\n",
			frame_bytes);
		free(frame);
		munmap(fb, frame_bytes);
		close(fd);
		return 1;
	}

	sigemptyset(&action.sa_mask);
	sigaction(SIGINT, &action, NULL);
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGHUP, &action, NULL);
	printf("WII_FB_STRESS_START width=%u height=%u stride=%u fps=%u duration=%u\n",
	       variable.xres, variable.yres, fixed.line_length, fps, duration);
	fflush(stdout);
	tty_message("\n=== GX RGB565 STRESS STARTING ===\n");

	interval_ns = 1000000000ULL / fps;
	start_ns = monotonic_ns();
	next_ns = start_ns;
	report_ns = start_ns + 5000000000ULL;
	end_ns = duration ? start_ns + (uint64_t)duration * 1000000000ULL : 0;

	while (!stop_requested) {
		uint64_t now_ns;

		draw_frame(frame, fixed.line_length, variable.xres, variable.yres,
			   frame_number);
		memcpy(fb, frame, frame_bytes);
		frame_number++;

		next_ns += interval_ns;
		now_ns = monotonic_ns();
		if (end_ns && now_ns >= end_ns)
			break;
		if (now_ns >= report_ns) {
			double elapsed = (now_ns - start_ns) / 1000000000.0;

			printf("WII_FB_STRESS_PROGRESS frames=%llu elapsed=%.3f fps=%.2f\n",
			       (unsigned long long)frame_number, elapsed,
			       frame_number / elapsed);
			fflush(stdout);
			report_ns += 5000000000ULL;
		}
		if (next_ns <= now_ns)
			next_ns = now_ns + interval_ns;
		sleep_until(next_ns);
	}

	start_ns = monotonic_ns() - start_ns;
	printf("WII_FB_STRESS_DONE frames=%llu elapsed=%.3f fps=%.2f signal=%u\n",
	       (unsigned long long)frame_number, start_ns / 1000000000.0,
	       frame_number * 1000000000.0 / start_ns, stop_requested != 0);
	fflush(stdout);

	/* Make fbcon repaint a useful recovery screen after direct framebuffer use. */
	tty_message("\033[2J\033[H=== GX RGB565 STRESS COMPLETE: CONSOLE LIVE ===\n");
	free(frame);
	munmap(fb, frame_bytes);
	close(fd);
	return 0;
}
