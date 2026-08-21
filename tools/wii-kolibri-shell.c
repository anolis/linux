// SPDX-License-Identifier: GPL-2.0-only
/* Minimal Kolibri-inspired desktop shell for the Wii VI DRM/KMS driver. */

#define main wii_drm_test_main
#include "wii-drm-test.c"
#undef main

#include <dirent.h>
#include <linux/input.h>

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define BIT(bit) (1U << (bit))

/* Reuse the kernel's built-in GPL VGA font without kernel-only headers. */
#define _VIDEO_FONT_H
#define _LINUX_MODULE_H
#define __packed
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

#define SHELL_BUFFER_COUNT 3
#define SHELL_INPUT_COUNT 16
#define SHELL_POLL_MS 100
#define SHELL_FONT_WIDTH 8
#define SHELL_FONT_HEIGHT 16
#define SHELL_WORKSPACE_LEFT 112
#define SHELL_WORKSPACE_TOP 32
#define SHELL_WORKSPACE_BOTTOM (TEST_HEIGHT - 24)

enum shell_app {
	SHELL_APP_TERMINAL,
	SHELL_APP_FILES,
	SHELL_APP_SYSTEM,
	SHELL_APP_COUNT,
};

struct shell_input {
	int fd;
	char path[32];
};

struct shell_window {
	enum shell_app app;
	int x;
	int y;
	int width;
	int height;
	int visible;
};

struct shell_state {
	struct test_buffer buffers[SHELL_BUFFER_COUNT];
	struct shell_input inputs[SHELL_INPUT_COUNT];
	struct shell_window windows[SHELL_APP_COUNT];
	unsigned int z_order[SHELL_APP_COUNT];
	unsigned int input_count;
	unsigned int visible;
	unsigned int selected;
	__u64 serial;
	int focused;
	int dragging;
	int drag_offset_x;
	int drag_offset_y;
	int cursor_visible;
	int pointer_x;
	int pointer_y;
};

static const char *const app_titles[SHELL_APP_COUNT] = {
	"Terminal", "Files", "System",
};

static const uint32_t shell_colors[] = {
	0x00171c1f, /* desktop */
	0x00242b2f, /* panel */
	0x00333b40, /* border */
	0x00eef2f3, /* text */
	0x0099a5aa, /* muted */
	0x002fc4b2, /* teal */
	0x00e35d4f, /* red */
	0x00d9a441, /* gold */
	0x008e71c7, /* violet */
	0x000d1113, /* terminal */
};

enum shell_color {
	COLOR_DESKTOP,
	COLOR_PANEL,
	COLOR_BORDER,
	COLOR_TEXT,
	COLOR_MUTED,
	COLOR_TEAL,
	COLOR_RED,
	COLOR_GOLD,
	COLOR_VIOLET,
	COLOR_TERMINAL,
};

static const enum shell_color app_accents[SHELL_APP_COUNT] = {
	COLOR_TEAL, COLOR_GOLD, COLOR_VIOLET,
};

static uint16_t rgb565(enum shell_color color)
{
	return xrgb8888_to_rgb565(shell_colors[color]);
}

static uint64_t shell_monotonic_ms(void)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
		return 0;
	return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void fill_rect(struct test_buffer *buffer, int x, int y,
		      int width, int height, uint16_t color)
{
	int row;
	int column;

	if (x < 0) {
		width += x;
		x = 0;
	}
	if (y < 0) {
		height += y;
		y = 0;
	}
	if (x + width > TEST_WIDTH)
		width = TEST_WIDTH - x;
	if (y + height > TEST_HEIGHT)
		height = TEST_HEIGHT - y;
	if (width <= 0 || height <= 0)
		return;

	for (row = 0; row < height; row++) {
		uint16_t *pixels = (uint16_t *)((uint8_t *)buffer->map +
			(size_t)(y + row) * buffer->create.pitch) + x;

		for (column = 0; column < width; column++)
			pixels[column] = color;
	}
}

static void stroke_rect(struct test_buffer *buffer, int x, int y,
			int width, int height, uint16_t color)
{
	fill_rect(buffer, x, y, width, 1, color);
	fill_rect(buffer, x, y + height - 1, width, 1, color);
	fill_rect(buffer, x, y, 1, height, color);
	fill_rect(buffer, x + width - 1, y, 1, height, color);
}

static void draw_character(struct test_buffer *buffer, int x, int y,
			   unsigned char character, uint16_t foreground)
{
	const uint8_t *glyph = font_vga_8x16.data +
		character * SHELL_FONT_HEIGHT;
	unsigned int glyph_y;

	for (glyph_y = 0; glyph_y < SHELL_FONT_HEIGHT; glyph_y++) {
		uint16_t *pixels;
		unsigned int glyph_x;

		if (y + (int)glyph_y < 0 || y + (int)glyph_y >= TEST_HEIGHT)
			continue;
		pixels = (uint16_t *)((uint8_t *)buffer->map +
			(size_t)(y + glyph_y) * buffer->create.pitch);
		for (glyph_x = 0; glyph_x < SHELL_FONT_WIDTH; glyph_x++) {
			int pixel_x = x + glyph_x;

			if (pixel_x >= 0 && pixel_x < TEST_WIDTH &&
			    (glyph[glyph_y] & BIT(7 - glyph_x)))
				pixels[pixel_x] = foreground;
		}
	}
}

static void draw_text(struct test_buffer *buffer, int x, int y,
		      const char *text, uint16_t color)
{
	while (*text) {
		draw_character(buffer, x, y, (unsigned char)*text++, color);
		x += SHELL_FONT_WIDTH;
	}
}

static void draw_launcher(struct test_buffer *buffer,
			  const struct shell_state *shell)
{
	unsigned int i;

	fill_rect(buffer, 0, 32, 112, TEST_HEIGHT - 56, rgb565(COLOR_PANEL));
	fill_rect(buffer, 111, 32, 1, TEST_HEIGHT - 56, rgb565(COLOR_BORDER));
	for (i = 0; i < SHELL_APP_COUNT; i++) {
		int y = 62 + i * 56;

		if (shell->selected == i) {
			fill_rect(buffer, 8, y - 8, 96, 40,
				  rgb565(COLOR_BORDER));
			fill_rect(buffer, 8, y - 8, 3, 40,
				  rgb565(app_accents[i]));
		}
		fill_rect(buffer, 18, y, 16, 16, rgb565(app_accents[i]));
		draw_text(buffer, 42, y, app_titles[i], rgb565(COLOR_TEXT));
		if (shell->windows[i].visible)
			fill_rect(buffer, 96, y + 5, 5, 5,
				  rgb565(app_accents[i]));
	}
}

static void draw_terminal(struct test_buffer *buffer,
			  const struct shell_window *window)
{
	int x = window->x + 8;
	int y = window->y + 36;

	fill_rect(buffer, x, y, window->width - 16, window->height - 44,
		  rgb565(COLOR_TERMINAL));
	draw_text(buffer, x + 16, y + 18, "Wii Linux NGX", rgb565(COLOR_TEAL));
	draw_text(buffer, x + 16, y + 50, "Native KMS shell online",
		  rgb565(COLOR_TEXT));
	draw_text(buffer, x + 16, y + 82, "root@wii:~#", rgb565(COLOR_GOLD));
	draw_text(buffer, x + 112, y + 82, "_", rgb565(COLOR_TEXT));
}

static void draw_files(struct test_buffer *buffer,
		       const struct shell_window *window)
{
	static const char *const names[] = {
		"Applications", "Documents", "System", "Network",
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(names); i++) {
		int x = window->x + 24;
		int y = window->y + 48 + i * 48;

		fill_rect(buffer, x, y, 30, 24,
			  rgb565(i & 1 ? COLOR_GOLD : COLOR_TEAL));
		draw_text(buffer, x + 46, y + 4, names[i], rgb565(COLOR_TEXT));
		fill_rect(buffer, x, y + 34, window->width - 48, 1,
			  rgb565(COLOR_BORDER));
	}
}

static void draw_status_row(struct test_buffer *buffer, int x, int y,
			    const char *label, const char *value,
			    enum shell_color indicator)
{
	fill_rect(buffer, x, y + 4, 8, 8, rgb565(indicator));
	draw_text(buffer, x + 22, y, label, rgb565(COLOR_MUTED));
	draw_text(buffer, x + 166, y, value, rgb565(COLOR_TEXT));
}

static void draw_system(struct test_buffer *buffer,
			const struct shell_window *window)
{
	int x = window->x + 26;
	int y = window->y + 52;

	draw_status_row(buffer, x, y, "Processor", "Broadway PowerPC", COLOR_TEAL);
	draw_status_row(buffer, x, y + 38, "Graphics", "GX DRM/KMS", COLOR_RED);
	draw_status_row(buffer, x, y + 76, "Display", "640 x 480 RGB565",
			COLOR_GOLD);
	draw_status_row(buffer, x, y + 114, "Network", "Online", COLOR_TEAL);
	draw_status_row(buffer, x, y + 152, "Session", "Ready", COLOR_VIOLET);
}

static void draw_window(struct test_buffer *buffer,
			const struct shell_state *shell,
			const struct shell_window *window)
{
	uint16_t frame = shell->focused == (int)window->app ?
		rgb565(app_accents[window->app]) : rgb565(COLOR_BORDER);

	fill_rect(buffer, window->x + 4, window->y + 4, window->width,
		  window->height, rgb565(COLOR_TERMINAL));
	fill_rect(buffer, window->x, window->y, window->width, window->height,
		  rgb565(COLOR_PANEL));
	stroke_rect(buffer, window->x, window->y, window->width, window->height,
		    frame);
	fill_rect(buffer, window->x, window->y, window->width, 28,
		  rgb565(COLOR_BORDER));
	fill_rect(buffer, window->x, window->y, 3, 28, frame);
	draw_text(buffer, window->x + 14, window->y + 6,
		  app_titles[window->app], rgb565(COLOR_TEXT));
	fill_rect(buffer, window->x + window->width - 24, window->y + 8,
		  12, 12, rgb565(COLOR_RED));

	switch (window->app) {
	case SHELL_APP_TERMINAL:
		draw_terminal(buffer, window);
		break;
	case SHELL_APP_FILES:
		draw_files(buffer, window);
		break;
	case SHELL_APP_SYSTEM:
		draw_system(buffer, window);
		break;
	default:
		break;
	}
}

static void draw_pointer(struct test_buffer *buffer, int x, int y)
{
	int row;

	for (row = 0; row < 16; row++) {
		int width = row / 2 + 2;

		fill_rect(buffer, x, y + row, width, 1, rgb565(COLOR_TERMINAL));
		if (width > 2)
			fill_rect(buffer, x + 1, y + row, width - 2, 1,
				  rgb565(COLOR_TEXT));
	}
	fill_rect(buffer, x + 3, y + 12, 3, 8, rgb565(COLOR_TERMINAL));
	fill_rect(buffer, x + 4, y + 12, 1, 6, rgb565(COLOR_TEXT));
}

static void draw_shell(struct test_buffer *buffer,
		       const struct shell_state *shell)
{
	time_t now = time(NULL);
	struct tm local;
	char clock_text[16] = "--:--";
	unsigned int i;

	fill_rect(buffer, 0, 0, TEST_WIDTH, TEST_HEIGHT, rgb565(COLOR_DESKTOP));
	fill_rect(buffer, 0, 0, TEST_WIDTH, 32, rgb565(COLOR_PANEL));
	fill_rect(buffer, 0, 31, TEST_WIDTH, 1, rgb565(COLOR_BORDER));
	fill_rect(buffer, 14, 8, 16, 16, rgb565(COLOR_RED));
	draw_text(buffer, 40, 8, "WII LINUX NGX", rgb565(COLOR_TEXT));
	if (localtime_r(&now, &local))
		strftime(clock_text, sizeof(clock_text), "%H:%M", &local);
	draw_text(buffer, 580, 8, clock_text, rgb565(COLOR_MUTED));

	draw_launcher(buffer, shell);
	for (i = 0; i < SHELL_APP_COUNT; i++) {
		const struct shell_window *window =
			&shell->windows[shell->z_order[i]];

		if (window->visible)
			draw_window(buffer, shell, window);
	}

	fill_rect(buffer, 0, TEST_HEIGHT - 24, TEST_WIDTH, 24,
		  rgb565(COLOR_PANEL));
	fill_rect(buffer, 0, TEST_HEIGHT - 24, TEST_WIDTH, 1,
		  rgb565(COLOR_BORDER));
	draw_text(buffer, 14, TEST_HEIGHT - 20, "Ready", rgb565(COLOR_MUTED));
	if (shell->focused >= 0)
		draw_text(buffer, 88, TEST_HEIGHT - 20,
			  app_titles[shell->focused], rgb565(COLOR_TEXT));
	fill_rect(buffer, 602, TEST_HEIGHT - 16, 8, 8,
		  rgb565(shell->cursor_visible ? COLOR_TEAL : COLOR_BORDER));
	draw_pointer(buffer, shell->pointer_x, shell->pointer_y);
}

static int event_bit(const unsigned long *bits, unsigned int bit)
{
	return bits[bit / (sizeof(unsigned long) * 8)] &
		(1UL << (bit % (sizeof(unsigned long) * 8)));
}

static void open_inputs(struct shell_state *shell)
{
	unsigned int index;

	for (index = 0; index < 32 && shell->input_count < SHELL_INPUT_COUNT;
	     index++) {
		unsigned long event_bits[(EV_MAX + 8 * sizeof(unsigned long)) /
			(8 * sizeof(unsigned long))] = { };
		unsigned long key_bits[(KEY_MAX + 8 * sizeof(unsigned long)) /
			(8 * sizeof(unsigned long))] = { };
		unsigned long relative_bits[(REL_MAX + 8 * sizeof(unsigned long)) /
			(8 * sizeof(unsigned long))] = { };
		struct shell_input *input = &shell->inputs[shell->input_count];
		char name[128] = "unknown";
		int keyboard;
		int pointer;
		int fd;

		snprintf(input->path, sizeof(input->path), "/dev/input/event%u",
			 index);
		fd = open(input->path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;
		if (ioctl(fd, EVIOCGBIT(0, sizeof(event_bits)), event_bits) < 0) {
			close(fd);
			continue;
		}
		if (event_bit(event_bits, EV_KEY))
			(void)ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits);
		if (event_bit(event_bits, EV_REL))
			(void)ioctl(fd, EVIOCGBIT(EV_REL, sizeof(relative_bits)),
				    relative_bits);
		keyboard = event_bit(event_bits, EV_KEY) &&
			(event_bit(key_bits, KEY_ENTER) ||
			 event_bit(key_bits, KEY_SPACE));
		pointer = event_bit(event_bits, EV_REL) &&
			event_bit(relative_bits, REL_X) &&
			event_bit(relative_bits, REL_Y) &&
			event_bit(key_bits, BTN_LEFT);
		if (!keyboard && !pointer) {
			close(fd);
			continue;
		}
		(void)ioctl(fd, EVIOCGNAME(sizeof(name)), name);
		input->fd = fd;
		shell->input_count++;
		printf("wii-kolibri-shell: input %s (%s, %s%s)\n", input->path,
		       name, keyboard ? "keyboard" : "",
		       pointer ? (keyboard ? "+pointer" : "pointer") : "");
	}
}

static void init_windows(struct shell_state *shell)
{
	shell->windows[SHELL_APP_TERMINAL] = (struct shell_window) {
		.app = SHELL_APP_TERMINAL,
		.x = 150,
		.y = 70,
		.width = 430,
		.height = 270,
		.visible = 1,
	};
	shell->windows[SHELL_APP_FILES] = (struct shell_window) {
		.app = SHELL_APP_FILES,
		.x = 170,
		.y = 90,
		.width = 400,
		.height = 300,
	};
	shell->windows[SHELL_APP_SYSTEM] = (struct shell_window) {
		.app = SHELL_APP_SYSTEM,
		.x = 190,
		.y = 110,
		.width = 410,
		.height = 280,
	};
	shell->z_order[0] = SHELL_APP_SYSTEM;
	shell->z_order[1] = SHELL_APP_FILES;
	shell->z_order[2] = SHELL_APP_TERMINAL;
	shell->focused = SHELL_APP_TERMINAL;
	shell->dragging = -1;
}

static void focus_top_window(struct shell_state *shell)
{
	int i;

	shell->focused = -1;
	for (i = SHELL_APP_COUNT - 1; i >= 0; i--)
		if (shell->windows[shell->z_order[i]].visible) {
			shell->focused = shell->z_order[i];
			break;
		}
}

static void raise_window(struct shell_state *shell, unsigned int app)
{
	unsigned int i;

	for (i = 0; i < SHELL_APP_COUNT; i++)
		if (shell->z_order[i] == app)
			break;
	if (i == SHELL_APP_COUNT)
		return;
	for (; i + 1 < SHELL_APP_COUNT; i++)
		shell->z_order[i] = shell->z_order[i + 1];
	shell->z_order[SHELL_APP_COUNT - 1] = app;
	shell->focused = app;
}

static void open_window(struct shell_state *shell, unsigned int app)
{
	shell->windows[app].visible = 1;
	raise_window(shell, app);
}

static void close_window(struct shell_state *shell, unsigned int app)
{
	shell->windows[app].visible = 0;
	if (shell->dragging == (int)app)
		shell->dragging = -1;
	focus_top_window(shell);
}

static int launcher_at(int x, int y)
{
	unsigned int i;

	if (x < 8 || x >= 104)
		return -1;
	for (i = 0; i < SHELL_APP_COUNT; i++) {
		int top = 54 + i * 56;

		if (y >= top && y < top + 40)
			return i;
	}
	return -1;
}

static int window_at(const struct shell_state *shell, int x, int y)
{
	int i;

	for (i = SHELL_APP_COUNT - 1; i >= 0; i--) {
		const struct shell_window *window =
			&shell->windows[shell->z_order[i]];

		if (window->visible && x >= window->x &&
		    x < window->x + window->width && y >= window->y &&
		    y < window->y + window->height)
			return window->app;
	}
	return -1;
}

static void focus_next_window(struct shell_state *shell)
{
	int focused_index = -1;
	int step;
	int i;

	for (i = 0; i < SHELL_APP_COUNT; i++)
		if (shell->z_order[i] == (unsigned int)shell->focused)
			focused_index = i;
	if (focused_index < 0) {
		focus_top_window(shell);
		return;
	}
	for (step = 1; step <= SHELL_APP_COUNT; step++) {
		i = (focused_index - step + SHELL_APP_COUNT) % SHELL_APP_COUNT;
		if (shell->windows[shell->z_order[i]].visible) {
			raise_window(shell, shell->z_order[i]);
			return;
		}
	}
}

static int handle_key(struct shell_state *shell, unsigned int key)
{
	switch (key) {
	case KEY_LEFT:
	case KEY_UP:
		if (shell->selected)
			shell->selected--;
		else
			shell->selected = SHELL_APP_COUNT - 1;
		return 1;
	case KEY_RIGHT:
	case KEY_DOWN:
	case KEY_TAB:
		shell->selected = (shell->selected + 1) % SHELL_APP_COUNT;
		return 1;
	case KEY_ENTER:
	case KEY_SPACE:
		open_window(shell, shell->selected);
		return 1;
	case KEY_F1:
		shell->selected = SHELL_APP_TERMINAL;
		open_window(shell, SHELL_APP_TERMINAL);
		return 1;
	case KEY_F2:
		shell->selected = SHELL_APP_FILES;
		open_window(shell, SHELL_APP_FILES);
		return 1;
	case KEY_F3:
		shell->selected = SHELL_APP_SYSTEM;
		open_window(shell, SHELL_APP_SYSTEM);
		return 1;
	case KEY_F4:
		if (shell->focused >= 0)
			close_window(shell, shell->focused);
		return 1;
	case KEY_F6:
		focus_next_window(shell);
		return 1;
	case KEY_ESC:
	case KEY_F12:
		stop = 1;
		return 0;
	default:
		return 0;
	}
}

static int update_pointer(struct shell_state *shell, int delta_x, int delta_y)
{
	int launcher;

	if (!delta_x && !delta_y)
		return 0;
	shell->pointer_x += delta_x;
	shell->pointer_y += delta_y;
	if (shell->pointer_x < 0)
		shell->pointer_x = 0;
	if (shell->pointer_x >= TEST_WIDTH)
		shell->pointer_x = TEST_WIDTH - 1;
	if (shell->pointer_y < 0)
		shell->pointer_y = 0;
	if (shell->pointer_y >= TEST_HEIGHT)
		shell->pointer_y = TEST_HEIGHT - 1;

	launcher = launcher_at(shell->pointer_x, shell->pointer_y);
	if (launcher >= 0)
		shell->selected = launcher;

	if (shell->dragging >= 0) {
		struct shell_window *window = &shell->windows[shell->dragging];
		int maximum_x = TEST_WIDTH - window->width;
		int maximum_y = SHELL_WORKSPACE_BOTTOM - window->height;

		window->x = shell->pointer_x - shell->drag_offset_x;
		window->y = shell->pointer_y - shell->drag_offset_y;
		if (window->x < SHELL_WORKSPACE_LEFT)
			window->x = SHELL_WORKSPACE_LEFT;
		if (window->x > maximum_x)
			window->x = maximum_x;
		if (window->y < SHELL_WORKSPACE_TOP)
			window->y = SHELL_WORKSPACE_TOP;
		if (window->y > maximum_y)
			window->y = maximum_y;
	}
	return 1;
}

static int handle_pointer_button(struct shell_state *shell, int pressed)
{
	struct shell_window *window;
	int launcher;
	int app;

	if (!pressed) {
		shell->dragging = -1;
		return 1;
	}
	launcher = launcher_at(shell->pointer_x, shell->pointer_y);
	if (launcher >= 0) {
		shell->selected = launcher;
		open_window(shell, launcher);
		return 1;
	}
	app = window_at(shell, shell->pointer_x, shell->pointer_y);
	if (app < 0) {
		shell->focused = -1;
		return 1;
	}
	raise_window(shell, app);
	window = &shell->windows[app];
	if (shell->pointer_x >= window->x + window->width - 32 &&
	    shell->pointer_x < window->x + window->width &&
	    shell->pointer_y >= window->y &&
	    shell->pointer_y < window->y + 28) {
		close_window(shell, app);
		return 1;
	}
	if (shell->pointer_y < window->y + 28) {
		shell->dragging = app;
		shell->drag_offset_x = shell->pointer_x - window->x;
		shell->drag_offset_y = shell->pointer_y - window->y;
		return 1;
	}
	return 1;
}

static void collect_input_event(struct shell_state *shell,
				const struct input_event *event, int *delta_x,
				int *delta_y, int *wheel, int *button_pressed,
				int *button_released, int *changed)
{
	if (event->type == EV_REL) {
		if (event->code == REL_X)
			*delta_x += event->value;
		else if (event->code == REL_Y)
			*delta_y += event->value;
		else if (event->code == REL_WHEEL)
			*wheel += event->value;
		return;
	}
	if (event->type != EV_KEY)
		return;
	if (event->code == BTN_LEFT) {
		if (event->value == 1)
			*button_pressed = 1;
		else if (event->value == 0)
			*button_released = 1;
		return;
	}
	if (event->value == 1)
		*changed |= handle_key(shell, event->code);
}

static int poll_inputs(struct shell_state *shell)
{
	struct pollfd poll_fds[SHELL_INPUT_COUNT];
	unsigned int i;
	int changed = 0;
	int ret;

	for (i = 0; i < shell->input_count; i++) {
		poll_fds[i].fd = shell->inputs[i].fd;
		poll_fds[i].events = POLLIN;
		poll_fds[i].revents = 0;
	}
	do {
		ret = poll(poll_fds, shell->input_count, SHELL_POLL_MS);
	} while (ret < 0 && errno == EINTR && !stop);
	if (stop)
		return 0;
	if (ret < 0)
		return -1;

	for (i = 0; i < shell->input_count; i++) {
		struct input_event events[16];
		ssize_t bytes;
		unsigned int j;
		int delta_x = 0;
		int delta_y = 0;
		int wheel = 0;
		int button_pressed = 0;
		int button_released = 0;

		if (!(poll_fds[i].revents & POLLIN))
			continue;
		while ((bytes = read(poll_fds[i].fd, events, sizeof(events))) > 0) {
			for (j = 0; j < (unsigned int)bytes / sizeof(events[0]); j++)
				collect_input_event(shell, &events[j], &delta_x,
						    &delta_y, &wheel,
						    &button_pressed,
						    &button_released, &changed);
		}
		if (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
			return -1;
		changed |= update_pointer(shell, delta_x, delta_y);
		if (wheel) {
			if (wheel > 0)
				changed |= handle_key(shell, KEY_UP);
			else
				changed |= handle_key(shell, KEY_DOWN);
		}
		if (button_pressed)
			changed |= handle_pointer_button(shell, 1);
		if (button_released)
			changed |= handle_pointer_button(shell, 0);
	}
	return changed;
}

static int present_shell(int drm_fd, __u32 crtc_id,
			 struct shell_state *shell)
{
	unsigned int next = (shell->visible + 1) % SHELL_BUFFER_COUNT;
	struct drm_mode_crtc_page_flip flip = {
		.crtc_id = crtc_id,
		.fb_id = shell->buffers[next].fb.fb_id,
		.flags = DRM_MODE_PAGE_FLIP_EVENT,
		.user_data = shell->serial++,
	};
	__u32 sequence;

	draw_shell(&shell->buffers[next], shell);
	if (xioctl(drm_fd, DRM_IOCTL_MODE_PAGE_FLIP, &flip) < 0 ||
	    wait_flip_event(drm_fd, flip.user_data, &sequence) < 0)
		return -1;
	shell->visible = next;
	return 0;
}

static void shell_usage(const char *program)
{
	fprintf(stderr, "Usage: %s [CARD]\n", program);
}

int main(int argc, char **argv)
{
	const char *card = "/dev/dri/card0";
	struct shell_state shell = {
		.serial = 1,
		.cursor_visible = 1,
		.pointer_x = TEST_WIDTH / 2,
		.pointer_y = TEST_HEIGHT / 2,
	};
	struct drm_mode_card_res resources;
	struct drm_mode_modeinfo mode;
	struct drm_mode_crtc crtc = { };
	struct drm_mode_crtc saved_crtc = { };
	__u32 *connector_ids = NULL;
	__u32 *crtc_ids = NULL;
	__u32 connector_id;
	unsigned int created = 0;
	unsigned int i;
	uint64_t last_second = 0;
	int crtc_active = 0;
	int drm_fd = -1;
	int status = EXIT_FAILURE;

	if (argc > 2 || (argc == 2 && !strcmp(argv[1], "--help"))) {
		shell_usage(argv[0]);
		return argc == 2 ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (argc == 2)
		card = argv[1];

	for (i = 0; i < ARRAY_SIZE(shell.buffers); i++)
		shell.buffers[i].map = MAP_FAILED;
	for (i = 0; i < ARRAY_SIZE(shell.inputs); i++)
		shell.inputs[i].fd = -1;
	init_windows(&shell);

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
	if (mode.hdisplay != TEST_WIDTH || mode.vdisplay != TEST_HEIGHT) {
		fprintf(stderr, "unsupported mode %ux%u\n", mode.hdisplay,
			mode.vdisplay);
		goto out;
	}

	saved_crtc.crtc_id = crtc_ids[0];
	if (xioctl(drm_fd, DRM_IOCTL_MODE_GETCRTC, &saved_crtc) < 0) {
		perror("DRM_IOCTL_MODE_GETCRTC");
		goto out;
	}
	for (i = 0; i < ARRAY_SIZE(shell.buffers); i++) {
		if (create_buffer(drm_fd, &shell.buffers[i], -1,
				  TEST_FORMAT_RGB565) < 0) {
			created = i + 1;
			perror("create RGB565 dumb framebuffer");
			goto out;
		}
		created = i + 1;
	}
	draw_shell(&shell.buffers[0], &shell);

	crtc.set_connectors_ptr = user_ptr(&connector_id);
	crtc.count_connectors = 1;
	crtc.crtc_id = crtc_ids[0];
	crtc.fb_id = shell.buffers[0].fb.fb_id;
	crtc.mode_valid = 1;
	crtc.mode = mode;
	if (xioctl(drm_fd, DRM_IOCTL_MODE_SETCRTC, &crtc) < 0) {
		perror("DRM_IOCTL_MODE_SETCRTC");
		goto out;
	}
	crtc_active = 1;

	open_inputs(&shell);
	printf("wii-kolibri-shell: active %ux%u rgb565 with %u input device(s)\n",
	       mode.hdisplay, mode.vdisplay, shell.input_count);
	printf("wii-kolibri-shell: Esc or F12 exits\n");
	fflush(stdout);
	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);

	while (!stop) {
		uint64_t second;
		int changed = poll_inputs(&shell);

		if (changed < 0) {
			perror("poll input");
			goto out;
		}
		second = shell_monotonic_ms() / 1000;
		if (second != last_second) {
			last_second = second;
			shell.cursor_visible = !shell.cursor_visible;
			changed = 1;
		}
		if (changed && present_shell(drm_fd, crtc.crtc_id, &shell) < 0) {
			perror("page flip");
			goto out;
		}
	}
	status = EXIT_SUCCESS;

out:
	for (i = 0; i < shell.input_count; i++)
		if (shell.inputs[i].fd >= 0)
			close(shell.inputs[i].fd);
	if (crtc_active && saved_crtc.fb_id) {
		saved_crtc.set_connectors_ptr = user_ptr(&connector_id);
		saved_crtc.count_connectors = 1;
		if (xioctl(drm_fd, DRM_IOCTL_MODE_SETCRTC, &saved_crtc) < 0)
			perror("restore CRTC");
	}
	while (created)
		destroy_buffer(drm_fd, &shell.buffers[--created]);
	free(crtc_ids);
	free(connector_ids);
	if (drm_fd >= 0)
		close(drm_fd);
	return status;
}
