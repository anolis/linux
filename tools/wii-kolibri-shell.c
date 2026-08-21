// SPDX-License-Identifier: GPL-2.0-only
/* Minimal Kolibri-inspired desktop shell for the Wii VI DRM/KMS driver. */

#define main wii_drm_test_main
#include "wii-drm-test.c"
#undef main

#include <dirent.h>
#include <limits.h>
#include <linux/input.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>

#ifdef WII_HAVE_VNC
#include <arpa/inet.h>
#include <rfb/rfb.h>
#endif

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
#define TERMINAL_COLUMNS 50
#define TERMINAL_ROWS 14
#define TERMINAL_CSI_PARAMS 4
#define FILES_ENTRY_COUNT 64
#define FILES_VISIBLE_ROWS 10
#define FILES_PATH_SIZE 512
#define SYSTEM_NETWORK_NAME 16

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

struct terminal_cell {
	uint8_t character;
	uint8_t color;
};

enum terminal_parser_state {
	TERMINAL_NORMAL,
	TERMINAL_ESCAPE,
	TERMINAL_CSI,
};

struct shell_terminal {
	struct terminal_cell cells[TERMINAL_ROWS][TERMINAL_COLUMNS];
	pid_t child_pid;
	int master_fd;
	unsigned int cursor_x;
	unsigned int cursor_y;
	unsigned int saved_x;
	unsigned int saved_y;
	unsigned int csi_params[TERMINAL_CSI_PARAMS];
	unsigned int csi_count;
	enum terminal_parser_state parser_state;
	uint8_t color;
	int csi_private;
	int child_exited;
};

struct shell_file_entry {
	char name[NAME_MAX + 1];
	int directory;
};

struct shell_files {
	struct shell_file_entry entries[FILES_ENTRY_COUNT];
	char path[FILES_PATH_SIZE];
	char status[64];
	unsigned int count;
	unsigned int selected;
	unsigned int scroll;
	uint64_t last_click_ms;
	int last_clicked;
	int loaded;
};

struct shell_system {
	uint64_t cpu_total;
	uint64_t cpu_idle;
	uint64_t uptime_seconds;
	uint64_t network_rx_bytes;
	uint64_t network_tx_bytes;
	unsigned long memory_total_kb;
	unsigned long memory_available_kb;
	unsigned int cpu_percent;
	char network_name[SYSTEM_NETWORK_NAME];
	int network_up;
	int gx_loaded;
	int drm_present;
	int valid;
};

#ifdef WII_HAVE_VNC
struct shell_vnc {
	rfbScreenInfoPtr screen;
};
#endif

struct shell_state {
	struct test_buffer buffers[SHELL_BUFFER_COUNT];
	struct shell_input inputs[SHELL_INPUT_COUNT];
	struct shell_window windows[SHELL_APP_COUNT];
	struct shell_terminal terminal;
	struct shell_files files;
	struct shell_system system;
#ifdef WII_HAVE_VNC
	struct shell_vnc vnc;
#endif
	unsigned int z_order[SHELL_APP_COUNT];
	unsigned int input_count;
	unsigned int visible;
	unsigned int selected;
	__u64 serial;
	int focused;
	int dragging;
	int drag_offset_x;
	int drag_offset_y;
	int shift_down;
	int control_down;
	int caps_lock;
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
	0x004b7bec, /* blue */
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
	COLOR_BLUE,
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

static void terminal_clear_row(struct shell_terminal *terminal,
			       unsigned int row, unsigned int first)
{
	unsigned int column;

	if (row >= TERMINAL_ROWS)
		return;
	for (column = first; column < TERMINAL_COLUMNS; column++) {
		terminal->cells[row][column].character = ' ';
		terminal->cells[row][column].color = COLOR_TEXT;
	}
}

static void terminal_clear(struct shell_terminal *terminal)
{
	unsigned int row;

	for (row = 0; row < TERMINAL_ROWS; row++)
		terminal_clear_row(terminal, row, 0);
	terminal->cursor_x = 0;
	terminal->cursor_y = 0;
}

static void terminal_scroll(struct shell_terminal *terminal)
{
	size_t move_size = sizeof(terminal->cells) -
		sizeof(terminal->cells[0]);

	memmove(terminal->cells[0], terminal->cells[1], move_size);
	terminal_clear_row(terminal, TERMINAL_ROWS - 1, 0);
}

static void terminal_newline(struct shell_terminal *terminal)
{
	terminal->cursor_y++;
	if (terminal->cursor_y >= TERMINAL_ROWS) {
		terminal_scroll(terminal);
		terminal->cursor_y = TERMINAL_ROWS - 1;
	}
}

static void terminal_put_character(struct shell_terminal *terminal,
				   unsigned char character)
{
	terminal->cells[terminal->cursor_y][terminal->cursor_x].character =
		character;
	terminal->cells[terminal->cursor_y][terminal->cursor_x].color =
		terminal->color;
	terminal->cursor_x++;
	if (terminal->cursor_x >= TERMINAL_COLUMNS) {
		terminal->cursor_x = 0;
		terminal_newline(terminal);
	}
}

static void terminal_set_color(struct shell_terminal *terminal,
			       unsigned int parameter)
{
	static const enum shell_color ansi_colors[8] = {
		COLOR_TEXT, COLOR_RED, COLOR_TEAL, COLOR_GOLD,
		COLOR_BLUE, COLOR_VIOLET, COLOR_TEAL, COLOR_TEXT,
	};

	if (!parameter || parameter == 39)
		terminal->color = COLOR_TEXT;
	else if (parameter >= 30 && parameter <= 37)
		terminal->color = ansi_colors[parameter - 30];
	else if (parameter >= 90 && parameter <= 97)
		terminal->color = ansi_colors[parameter - 90];
}

static void terminal_handle_csi(struct shell_terminal *terminal,
				unsigned char command)
{
	unsigned int first = terminal->csi_params[0];
	unsigned int second = terminal->csi_count > 1 ?
		terminal->csi_params[1] : 0;
	unsigned int amount = first ? first : 1;
	unsigned int i;

	switch (command) {
	case 'A':
		terminal->cursor_y = amount > terminal->cursor_y ?
			0 : terminal->cursor_y - amount;
		break;
	case 'B':
		terminal->cursor_y += amount;
		if (terminal->cursor_y >= TERMINAL_ROWS)
			terminal->cursor_y = TERMINAL_ROWS - 1;
		break;
	case 'C':
		terminal->cursor_x += amount;
		if (terminal->cursor_x >= TERMINAL_COLUMNS)
			terminal->cursor_x = TERMINAL_COLUMNS - 1;
		break;
	case 'D':
		terminal->cursor_x = amount > terminal->cursor_x ?
			0 : terminal->cursor_x - amount;
		break;
	case 'H':
	case 'f':
		terminal->cursor_y = first ? first - 1 : 0;
		terminal->cursor_x = second ? second - 1 : 0;
		if (terminal->cursor_y >= TERMINAL_ROWS)
			terminal->cursor_y = TERMINAL_ROWS - 1;
		if (terminal->cursor_x >= TERMINAL_COLUMNS)
			terminal->cursor_x = TERMINAL_COLUMNS - 1;
		break;
	case 'J':
		if (first == 2) {
			terminal_clear(terminal);
		} else {
			terminal_clear_row(terminal, terminal->cursor_y,
					   terminal->cursor_x);
			for (i = terminal->cursor_y + 1; i < TERMINAL_ROWS; i++)
				terminal_clear_row(terminal, i, 0);
		}
		break;
	case 'K':
		terminal_clear_row(terminal, terminal->cursor_y,
				   first == 2 ? 0 : terminal->cursor_x);
		break;
	case 'm':
		for (i = 0; i < terminal->csi_count; i++)
			terminal_set_color(terminal, terminal->csi_params[i]);
		break;
	case 's':
		terminal->saved_x = terminal->cursor_x;
		terminal->saved_y = terminal->cursor_y;
		break;
	case 'u':
		terminal->cursor_x = terminal->saved_x;
		terminal->cursor_y = terminal->saved_y;
		break;
	default:
		break;
	}
}

static void terminal_feed_byte(struct shell_terminal *terminal,
			       unsigned char byte)
{
	if (terminal->parser_state == TERMINAL_ESCAPE) {
		terminal->parser_state = TERMINAL_NORMAL;
		if (byte == '[') {
			memset(terminal->csi_params, 0,
			       sizeof(terminal->csi_params));
			terminal->csi_count = 1;
			terminal->csi_private = 0;
			terminal->parser_state = TERMINAL_CSI;
		} else if (byte == '7') {
			terminal->saved_x = terminal->cursor_x;
			terminal->saved_y = terminal->cursor_y;
		} else if (byte == '8') {
			terminal->cursor_x = terminal->saved_x;
			terminal->cursor_y = terminal->saved_y;
		} else if (byte == 'c') {
			terminal_clear(terminal);
		}
		return;
	}
	if (terminal->parser_state == TERMINAL_CSI) {
		unsigned int *parameter =
			&terminal->csi_params[terminal->csi_count - 1];

		if (byte >= '0' && byte <= '9') {
			*parameter = *parameter * 10 + byte - '0';
			return;
		}
		if ((byte == '?' || byte == '>') &&
		    terminal->csi_count == 1 && !*parameter) {
			terminal->csi_private = 1;
			return;
		}
		if (byte == ';' && terminal->csi_count < TERMINAL_CSI_PARAMS) {
			terminal->csi_count++;
			return;
		}
		if (!terminal->csi_private)
			terminal_handle_csi(terminal, byte);
		terminal->parser_state = TERMINAL_NORMAL;
		return;
	}

	switch (byte) {
	case '\033':
		terminal->parser_state = TERMINAL_ESCAPE;
		break;
	case '\r':
		terminal->cursor_x = 0;
		break;
	case '\n':
		terminal_newline(terminal);
		break;
	case '\b':
		if (terminal->cursor_x)
			terminal->cursor_x--;
		break;
	case '\t':
		do {
			terminal_put_character(terminal, ' ');
		} while (terminal->cursor_x % 8);
		break;
	default:
		if (byte >= 32)
			terminal_put_character(terminal,
					       byte < 127 ? byte : '?');
		break;
	}
}

static void terminal_feed_text(struct shell_terminal *terminal,
			       const char *text)
{
	while (*text)
		terminal_feed_byte(terminal, *text++);
}

static int terminal_write(struct shell_terminal *terminal,
			  const void *data, size_t length)
{
	const uint8_t *bytes = data;

	while (length) {
		ssize_t written = write(terminal->master_fd, bytes, length);

		if (written > 0) {
			bytes += written;
			length -= written;
			continue;
		}
		if (written < 0 && errno == EINTR)
			continue;
		if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return 0;
		return -1;
	}
	return 0;
}

static int start_terminal(struct shell_terminal *terminal)
{
	struct winsize size = {
		.ws_row = TERMINAL_ROWS,
		.ws_col = TERMINAL_COLUMNS,
	};
	char slave_path[32];
	unsigned int number;
	int unlock = 0;
	int flags;
	pid_t child;
	int master;

	if (terminal->master_fd >= 0 || terminal->child_pid > 0)
		return 0;
	memset(terminal, 0, sizeof(*terminal));
	terminal->master_fd = -1;
	terminal_clear(terminal);
	terminal->color = COLOR_TEXT;
	master = open("/dev/ptmx", O_RDWR | O_NOCTTY | O_CLOEXEC);
	if (master < 0 || ioctl(master, TIOCSPTLCK, &unlock) < 0 ||
	    ioctl(master, TIOCGPTN, &number) < 0) {
		if (master >= 0)
			close(master);
		return -1;
	}
	snprintf(slave_path, sizeof(slave_path), "/dev/pts/%u", number);
	(void)ioctl(master, TIOCSWINSZ, &size);
	child = fork();
	if (child < 0) {
		close(master);
		return -1;
	}
	if (!child) {
		int slave;

		if (setsid() < 0)
			_exit(126);
		slave = open(slave_path, O_RDWR);
		if (slave < 0 || ioctl(slave, TIOCSCTTY, 0) < 0)
			_exit(126);
		if (dup2(slave, STDIN_FILENO) < 0 ||
		    dup2(slave, STDOUT_FILENO) < 0 ||
		    dup2(slave, STDERR_FILENO) < 0)
			_exit(126);
		if (slave > STDERR_FILENO)
			close(slave);
		close(master);
		setenv("TERM", "vt100", 1);
		setenv("HOME", "/root", 1);
		setenv("PS1", "root@wii:\\w# ", 1);
		execl("/bin/sh", "sh", "-i", (char *)NULL);
		_exit(127);
	}
	flags = fcntl(master, F_GETFL);
	if (flags < 0 || fcntl(master, F_SETFL, flags | O_NONBLOCK) < 0) {
		kill(child, SIGHUP);
		close(master);
		(void)waitpid(child, NULL, 0);
		return -1;
	}
	terminal->master_fd = master;
	terminal->child_pid = child;
	printf("wii-kolibri-shell: terminal child pid=%d pty=%s\n",
	       child, slave_path);
	return 0;
}

static void stop_terminal(struct shell_terminal *terminal)
{
	int status;
	int i;

	if (terminal->master_fd >= 0) {
		close(terminal->master_fd);
		terminal->master_fd = -1;
	}
	if (terminal->child_pid <= 0)
		return;
	(void)kill(-terminal->child_pid, SIGHUP);
	for (i = 0; i < 50; i++) {
		pid_t result = waitpid(terminal->child_pid, &status, WNOHANG);

		if (result == terminal->child_pid ||
		    (result < 0 && errno == ECHILD)) {
			terminal->child_pid = 0;
			return;
		}
		(void)poll(NULL, 0, 10);
	}
	(void)kill(-terminal->child_pid, SIGKILL);
	(void)waitpid(terminal->child_pid, &status, 0);
	terminal->child_pid = 0;
}

static int drain_terminal(struct shell_terminal *terminal)
{
	uint8_t bytes[512];
	int changed = 0;
	ssize_t length;

	while ((length = read(terminal->master_fd, bytes, sizeof(bytes))) > 0) {
		ssize_t i;

		for (i = 0; i < length; i++)
			terminal_feed_byte(terminal, bytes[i]);
		changed = 1;
	}
	if (length < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
	    errno != EIO)
		return -1;
	if (terminal->child_pid > 0) {
		int status;
		pid_t result = waitpid(terminal->child_pid, &status, WNOHANG);

		if (result == terminal->child_pid) {
			terminal->child_pid = 0;
			terminal->child_exited = 1;
			terminal_feed_text(terminal,
					   "\r\n[process exited]\r\n");
			changed = 1;
		}
	}
	if (terminal->child_exited && terminal->master_fd >= 0) {
		close(terminal->master_fd);
		terminal->master_fd = -1;
	}
	return changed;
}

static int file_entry_compare(const void *left, const void *right)
{
	const struct shell_file_entry *a = left;
	const struct shell_file_entry *b = right;

	if (!strcmp(a->name, ".."))
		return -1;
	if (!strcmp(b->name, ".."))
		return 1;
	if (a->directory != b->directory)
		return b->directory - a->directory;
	return strcmp(a->name, b->name);
}

static int files_load(struct shell_files *files, const char *path)
{
	struct shell_file_entry entries[FILES_ENTRY_COUNT];
	struct dirent *directory_entry;
	unsigned int count = 0;
	int truncated = 0;
	DIR *directory;

	directory = opendir(path);
	if (!directory) {
		snprintf(files->status, sizeof(files->status),
			 "Open failed: %s", strerror(errno));
		return -1;
	}
	if (strcmp(path, "/")) {
		strcpy(entries[count].name, "..");
		entries[count++].directory = 1;
	}
	while ((directory_entry = readdir(directory))) {
		struct shell_file_entry *entry;
		char full_path[FILES_PATH_SIZE + NAME_MAX + 2];
		struct stat status;
		size_t name_length;

		if (!strcmp(directory_entry->d_name, ".") ||
		    !strcmp(directory_entry->d_name, ".."))
			continue;
		if (count == FILES_ENTRY_COUNT) {
			truncated = 1;
			break;
		}
		entry = &entries[count++];
		name_length = strnlen(directory_entry->d_name, NAME_MAX);
		memcpy(entry->name, directory_entry->d_name, name_length);
		entry->name[name_length] = '\0';
		entry->directory = directory_entry->d_type == DT_DIR;
		if (directory_entry->d_type != DT_UNKNOWN &&
		    directory_entry->d_type != DT_LNK)
			continue;
		if (!strcmp(path, "/"))
			snprintf(full_path, sizeof(full_path), "/%s", entry->name);
		else
			snprintf(full_path, sizeof(full_path), "%s/%s", path,
				 entry->name);
		if (!stat(full_path, &status))
			entry->directory = S_ISDIR(status.st_mode);
	}
	closedir(directory);
	qsort(entries, count, sizeof(entries[0]), file_entry_compare);
	memcpy(files->entries, entries, count * sizeof(entries[0]));
	strncpy(files->path, path, sizeof(files->path) - 1);
	files->path[sizeof(files->path) - 1] = '\0';
	files->count = count;
	files->selected = 0;
	files->scroll = 0;
	files->last_clicked = -1;
	files->loaded = 1;
	if (truncated)
		snprintf(files->status, sizeof(files->status),
			 "%u+ entries", count);
	else
		snprintf(files->status, sizeof(files->status),
			 "%u entr%s", count, count == 1 ? "y" : "ies");
	return 0;
}

static void files_parent_path(const char *path, char *parent, size_t size)
{
	char *separator;

	strncpy(parent, path, size - 1);
	parent[size - 1] = '\0';
	separator = strrchr(parent, '/');
	if (!separator || separator == parent)
		strcpy(parent, "/");
	else
		*separator = '\0';
}

static int files_open_selected(struct shell_files *files)
{
	const struct shell_file_entry *entry;
	char target[FILES_PATH_SIZE];

	if (!files->count || files->selected >= files->count)
		return 0;
	entry = &files->entries[files->selected];
	if (!entry->directory) {
		snprintf(files->status, sizeof(files->status), "File: %.38s",
			 entry->name);
		return 1;
	}
	if (!strcmp(entry->name, "..")) {
		files_parent_path(files->path, target, sizeof(target));
	} else if (!strcmp(files->path, "/")) {
		snprintf(target, sizeof(target), "/%s", entry->name);
	} else if (snprintf(target, sizeof(target), "%s/%s", files->path,
			    entry->name) >= (int)sizeof(target)) {
		snprintf(files->status, sizeof(files->status), "Path too long");
		return 1;
	}
	(void)files_load(files, target);
	return 1;
}

static int files_move_selection(struct shell_files *files, int movement)
{
	int selected;

	if (!files->count)
		return 0;
	selected = files->selected + movement;
	if (selected < 0)
		selected = 0;
	if (selected >= (int)files->count)
		selected = files->count - 1;
	if (selected == (int)files->selected)
		return 0;
	files->selected = selected;
	if (files->selected < files->scroll)
		files->scroll = files->selected;
	else if (files->selected >= files->scroll + FILES_VISIBLE_ROWS)
		files->scroll = files->selected - FILES_VISIBLE_ROWS + 1;
	return 1;
}

static int read_u64_file(const char *path, uint64_t *value)
{
	unsigned long long parsed;
	FILE *file;
	int result;

	file = fopen(path, "r");
	if (!file)
		return -1;
	result = fscanf(file, "%llu", &parsed);
	fclose(file);
	if (result != 1)
		return -1;
	*value = parsed;
	return 0;
}

static void system_read_cpu(struct shell_system *system)
{
	unsigned long long user = 0;
	unsigned long long nice = 0;
	unsigned long long kernel = 0;
	unsigned long long idle = 0;
	unsigned long long iowait = 0;
	unsigned long long irq = 0;
	unsigned long long softirq = 0;
	unsigned long long steal = 0;
	uint64_t total;
	uint64_t idle_total;
	FILE *file;

	file = fopen("/proc/stat", "r");
	if (!file)
		return;
	if (fscanf(file, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
		   &user, &nice, &kernel, &idle, &iowait, &irq, &softirq,
		   &steal) < 4) {
		fclose(file);
		return;
	}
	fclose(file);
	total = user + nice + kernel + idle + iowait + irq + softirq + steal;
	idle_total = idle + iowait;
	if (system->cpu_total && total > system->cpu_total) {
		uint64_t total_delta = total - system->cpu_total;
		uint64_t idle_delta = idle_total - system->cpu_idle;

		if (idle_delta > total_delta)
			idle_delta = total_delta;
		system->cpu_percent =
			(total_delta - idle_delta) * 100 / total_delta;
	}
	system->cpu_total = total;
	system->cpu_idle = idle_total;
}

static void system_read_memory(struct shell_system *system)
{
	char line[128];
	FILE *file;

	system->memory_total_kb = 0;
	system->memory_available_kb = 0;
	file = fopen("/proc/meminfo", "r");
	if (!file)
		return;
	while (fgets(line, sizeof(line), file)) {
		unsigned long value;

		if (sscanf(line, "MemTotal: %lu kB", &value) == 1)
			system->memory_total_kb = value;
		else if (sscanf(line, "MemAvailable: %lu kB", &value) == 1)
			system->memory_available_kb = value;
	}
	fclose(file);
}

static int system_read_operstate(const char *name)
{
	char path[128];
	char state[16];
	FILE *file;

	snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", name);
	file = fopen(path, "r");
	if (!file)
		return 0;
	state[0] = '\0';
	if (fscanf(file, "%15s", state) != 1)
		state[0] = '\0';
	fclose(file);
	return !strcmp(state, "up");
}

static void system_copy_network_name(char *destination, const char *source)
{
	size_t length = strnlen(source, SYSTEM_NETWORK_NAME - 1);

	memcpy(destination, source, length);
	destination[length] = '\0';
}

static void system_read_network(struct shell_system *system)
{
	struct dirent *entry;
	char fallback[SYSTEM_NETWORK_NAME] = "";
	DIR *directory;

	system->network_name[0] = '\0';
	system->network_up = 0;
	system->network_rx_bytes = 0;
	system->network_tx_bytes = 0;
	directory = opendir("/sys/class/net");
	if (!directory)
		return;
	while ((entry = readdir(directory))) {
		char candidate[SYSTEM_NETWORK_NAME];

		if (entry->d_name[0] == '.' || !strcmp(entry->d_name, "lo"))
			continue;
		system_copy_network_name(candidate, entry->d_name);
		if (!fallback[0])
			system_copy_network_name(fallback, candidate);
		if (system_read_operstate(candidate)) {
			system_copy_network_name(system->network_name, candidate);
			system->network_up = 1;
			break;
		}
	}
	closedir(directory);
	if (!system->network_name[0])
		strcpy(system->network_name, fallback);
	if (system->network_name[0]) {
		char path[128];

		snprintf(path, sizeof(path),
			 "/sys/class/net/%s/statistics/rx_bytes",
			 system->network_name);
		(void)read_u64_file(path, &system->network_rx_bytes);
		snprintf(path, sizeof(path),
			 "/sys/class/net/%s/statistics/tx_bytes",
			 system->network_name);
		(void)read_u64_file(path, &system->network_tx_bytes);
	}
}

static void refresh_system(struct shell_system *system)
{
	unsigned long long uptime;
	FILE *file;

	system_read_cpu(system);
	system_read_memory(system);
	system_read_network(system);
	file = fopen("/proc/uptime", "r");
	if (file) {
		if (fscanf(file, "%llu", &uptime) == 1)
			system->uptime_seconds = uptime;
		fclose(file);
	}
	system->gx_loaded = !access("/sys/module/gcn_gx", F_OK);
	system->drm_present = !access("/sys/class/drm/card0", F_OK);
	system->valid = 1;
}

#ifdef WII_HAVE_VNC
static void vnc_key_event(rfbBool down, rfbKeySym key, rfbClientPtr client)
{
	(void)down;
	(void)key;
	(void)client;
}

static void vnc_pointer_event(int buttons, int x, int y,
			      rfbClientPtr client)
{
	(void)buttons;
	(void)x;
	(void)y;
	(void)client;
}

static int start_vnc(struct shell_state *shell, struct test_buffer *buffer)
{
	char program[] = "wii-kolibri-shell";
	char *arguments[2];
	int argument_count = 1;
	rfbScreenInfoPtr screen;

	arguments[0] = program;
	arguments[1] = NULL;
	screen = rfbGetScreen(&argument_count, arguments, TEST_WIDTH,
			      TEST_HEIGHT, 5, 3, 2);
	if (!screen)
		return -1;
	screen->desktopName = "Wii Linux NGX";
	screen->frameBuffer = buffer->map;
	screen->screenData = shell;
	screen->listenInterface = htonl(INADDR_LOOPBACK);
	screen->port = 5900;
	screen->alwaysShared = TRUE;
	screen->kbdAddEvent = vnc_key_event;
	screen->ptrAddEvent = vnc_pointer_event;
	screen->serverFormat.bitsPerPixel = 16;
	screen->serverFormat.depth = 16;
	screen->serverFormat.bigEndian = TRUE;
	screen->serverFormat.trueColour = TRUE;
	screen->serverFormat.redMax = 31;
	screen->serverFormat.greenMax = 63;
	screen->serverFormat.blueMax = 31;
	screen->serverFormat.redShift = 11;
	screen->serverFormat.greenShift = 5;
	screen->serverFormat.blueShift = 0;
	rfbInitServer(screen);
	if (screen->listenSock == RFB_INVALID_SOCKET) {
		rfbScreenCleanup(screen);
		return -1;
	}
	shell->vnc.screen = screen;
	printf("wii-kolibri-shell: read-only VNC on 127.0.0.1:5900\n");
	return 0;
}

static void process_vnc(struct shell_state *shell)
{
	if (shell->vnc.screen)
		(void)rfbProcessEvents(shell->vnc.screen, 0);
}

static void update_vnc(struct shell_state *shell, struct test_buffer *buffer)
{
	if (!shell->vnc.screen)
		return;
	shell->vnc.screen->frameBuffer = buffer->map;
	rfbMarkRectAsModified(shell->vnc.screen, 0, 0, TEST_WIDTH, TEST_HEIGHT);
}

static void stop_vnc(struct shell_state *shell)
{
	if (!shell->vnc.screen)
		return;
	rfbShutdownServer(shell->vnc.screen, TRUE);
	rfbScreenCleanup(shell->vnc.screen);
	shell->vnc.screen = NULL;
}
#else
static int start_vnc(struct shell_state *shell, struct test_buffer *buffer)
{
	(void)shell;
	(void)buffer;
	return 0;
}

static void process_vnc(struct shell_state *shell)
{
	(void)shell;
}

static void update_vnc(struct shell_state *shell, struct test_buffer *buffer)
{
	(void)shell;
	(void)buffer;
}

static void stop_vnc(struct shell_state *shell)
{
	(void)shell;
}
#endif

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
			  const struct shell_state *shell,
			  const struct shell_window *window)
{
	int x = window->x + 8;
	int y = window->y + 36;
	unsigned int row;
	unsigned int column;

	fill_rect(buffer, x, y, window->width - 16, window->height - 44,
		  rgb565(COLOR_TERMINAL));
	x += 7;
	for (row = 0; row < TERMINAL_ROWS; row++)
		for (column = 0; column < TERMINAL_COLUMNS; column++) {
			const struct terminal_cell *cell =
				&shell->terminal.cells[row][column];
			int cell_x = x + column * SHELL_FONT_WIDTH;
			int cell_y = y + row * SHELL_FONT_HEIGHT;

			if (shell->focused == SHELL_APP_TERMINAL &&
			    shell->cursor_visible &&
			    row == shell->terminal.cursor_y &&
			    column == shell->terminal.cursor_x) {
				fill_rect(buffer, cell_x, cell_y, SHELL_FONT_WIDTH,
					  SHELL_FONT_HEIGHT, rgb565(COLOR_TEXT));
				if (cell->character != ' ')
					draw_character(buffer, cell_x, cell_y,
						       cell->character,
						       rgb565(COLOR_TERMINAL));
			} else if (cell->character != ' ') {
				draw_character(buffer, cell_x, cell_y,
					       cell->character,
					       rgb565(cell->color));
			}
		}
}

static void draw_files(struct test_buffer *buffer,
		       const struct shell_state *shell,
		       const struct shell_window *window)
{
	const struct shell_files *files = &shell->files;
	char status[47];
	char path[47];
	size_t path_length = strlen(files->path);
	unsigned int i;

	if (path_length < sizeof(path))
		strcpy(path, files->path);
	else
		snprintf(path, sizeof(path), "...%s",
			 files->path + path_length - sizeof(path) + 4);
	draw_text(buffer, window->x + 16, window->y + 38, path,
		  rgb565(COLOR_TEXT));
	fill_rect(buffer, window->x + 16, window->y + 58,
		  window->width - 32, 1, rgb565(COLOR_BORDER));

	for (i = 0; i < FILES_VISIBLE_ROWS; i++) {
		unsigned int index = files->scroll + i;
		const struct shell_file_entry *entry;
		char label[39];
		int x = window->x + 16;
		int y = window->y + 64 + i * 20;

		if (index >= files->count)
			break;
		entry = &files->entries[index];
		if (index == files->selected)
			fill_rect(buffer, x, y, window->width - 32, 19,
				  rgb565(COLOR_BORDER));
		fill_rect(buffer, x + 4, y + 5, 10, 10,
			  rgb565(entry->directory ? COLOR_GOLD : COLOR_MUTED));
		snprintf(label, sizeof(label), entry->directory ? "%.35s/" :
			 "%.36s", entry->name);
		draw_text(buffer, x + 22, y + 2, label, rgb565(COLOR_TEXT));
	}
	snprintf(status, sizeof(status), "%.46s", files->status);
	draw_text(buffer, window->x + 16, window->y + window->height - 22,
		  status, rgb565(COLOR_MUTED));
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
			const struct shell_state *shell,
			const struct shell_window *window)
{
	const struct shell_system *system = &shell->system;
	uint64_t memory_used_kb = system->memory_total_kb >
		system->memory_available_kb ?
		system->memory_total_kb - system->memory_available_kb : 0;
	char graphics[28];
	char network[28];
	char traffic[28];
	char memory[28];
	char uptime[28];
	char cpu[28];
	unsigned int rx_mib = system->network_rx_bytes >> 20;
	unsigned int tx_mib = system->network_tx_bytes >> 20;
	int x = window->x + 26;
	int y = window->y + 44;

	snprintf(cpu, sizeof(cpu), "%u%% Broadway", system->cpu_percent);
	if (system->memory_total_kb)
		snprintf(memory, sizeof(memory), "%llu / %lu MiB",
			 (unsigned long long)(memory_used_kb / 1024),
			 system->memory_total_kb / 1024);
	else
		strcpy(memory, "Unavailable");
	snprintf(uptime, sizeof(uptime), "%lluh %02llum %02llus",
		 (unsigned long long)(system->uptime_seconds / 3600),
		 (unsigned long long)(system->uptime_seconds / 60 % 60),
		 (unsigned long long)(system->uptime_seconds % 60));
	if (system->network_name[0])
		snprintf(network, sizeof(network), "%s %s", system->network_name,
			 system->network_up ? "Up" : "Down");
	else
		strcpy(network, "No interface");
	if (system->network_rx_bytes >> 20 > 999999)
		rx_mib = 999999;
	if (system->network_tx_bytes >> 20 > 999999)
		tx_mib = 999999;
	snprintf(traffic, sizeof(traffic), "R%uM T%uM", rx_mib, tx_mib);
	snprintf(graphics, sizeof(graphics), "%s / %s",
		 system->gx_loaded ? "GX" : "CPU",
		 system->drm_present ? "DRM" : "No DRM");

	draw_status_row(buffer, x, y, "CPU", cpu, COLOR_TEAL);
	draw_status_row(buffer, x, y + 34, "Memory", memory, COLOR_GOLD);
	draw_status_row(buffer, x, y + 68, "Uptime", uptime, COLOR_VIOLET);
	draw_status_row(buffer, x, y + 102, "Network", network,
			system->network_up ? COLOR_TEAL : COLOR_RED);
	draw_status_row(buffer, x, y + 136, "Traffic", traffic, COLOR_BLUE);
	draw_status_row(buffer, x, y + 170, "Graphics", graphics,
			system->gx_loaded && system->drm_present ?
			COLOR_TEAL : COLOR_RED);
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
	if (window->app == SHELL_APP_TERMINAL)
		draw_text(buffer, window->x + 102, window->y + 6,
			  shell->terminal.child_pid > 0 ? "Running" : "Exited",
			  rgb565(shell->terminal.child_pid > 0 ?
				 COLOR_TEAL : COLOR_MUTED));
	fill_rect(buffer, window->x + window->width - 24, window->y + 8,
		  12, 12, rgb565(COLOR_RED));

	switch (window->app) {
	case SHELL_APP_TERMINAL:
		draw_terminal(buffer, shell, window);
		break;
	case SHELL_APP_FILES:
		draw_files(buffer, shell, window);
		break;
	case SHELL_APP_SYSTEM:
		draw_system(buffer, shell, window);
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
		if (ioctl(fd, EVIOCGRAB, 1) < 0) {
			perror("EVIOCGRAB");
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
	if (app == SHELL_APP_TERMINAL && shell->terminal.child_pid <= 0 &&
	    shell->terminal.master_fd < 0 && start_terminal(&shell->terminal) < 0) {
		shell->terminal.child_exited = 1;
		terminal_feed_text(&shell->terminal, "[launch failed]\r\n");
		perror("restart terminal");
	}
	if (app == SHELL_APP_FILES && !shell->files.loaded)
		(void)files_load(&shell->files, "/");
	if (app == SHELL_APP_SYSTEM)
		refresh_system(&shell->system);
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

static unsigned char key_character(unsigned int key)
{
	static const unsigned char characters[KEY_MAX + 1] = {
		[KEY_A] = 'a', [KEY_B] = 'b', [KEY_C] = 'c', [KEY_D] = 'd',
		[KEY_E] = 'e', [KEY_F] = 'f', [KEY_G] = 'g', [KEY_H] = 'h',
		[KEY_I] = 'i', [KEY_J] = 'j', [KEY_K] = 'k', [KEY_L] = 'l',
		[KEY_M] = 'm', [KEY_N] = 'n', [KEY_O] = 'o', [KEY_P] = 'p',
		[KEY_Q] = 'q', [KEY_R] = 'r', [KEY_S] = 's', [KEY_T] = 't',
		[KEY_U] = 'u', [KEY_V] = 'v', [KEY_W] = 'w', [KEY_X] = 'x',
		[KEY_Y] = 'y', [KEY_Z] = 'z',
		[KEY_1] = '1', [KEY_2] = '2', [KEY_3] = '3', [KEY_4] = '4',
		[KEY_5] = '5', [KEY_6] = '6', [KEY_7] = '7', [KEY_8] = '8',
		[KEY_9] = '9', [KEY_0] = '0',
		[KEY_MINUS] = '-', [KEY_EQUAL] = '=', [KEY_LEFTBRACE] = '[',
		[KEY_RIGHTBRACE] = ']', [KEY_BACKSLASH] = '\\',
		[KEY_SEMICOLON] = ';', [KEY_APOSTROPHE] = '\'',
		[KEY_GRAVE] = '`', [KEY_COMMA] = ',', [KEY_DOT] = '.',
		[KEY_SLASH] = '/', [KEY_SPACE] = ' ',
	};

	return key <= KEY_MAX ? characters[key] : 0;
}

static unsigned char shifted_character(unsigned char character)
{
	switch (character) {
	case '1': return '!';
	case '2': return '@';
	case '3': return '#';
	case '4': return '$';
	case '5': return '%';
	case '6': return '^';
	case '7': return '&';
	case '8': return '*';
	case '9': return '(';
	case '0': return ')';
	case '-': return '_';
	case '=': return '+';
	case '[': return '{';
	case ']': return '}';
	case '\\': return '|';
	case ';': return ':';
	case '\'': return '"';
	case '`': return '~';
	case ',': return '<';
	case '.': return '>';
	case '/': return '?';
	default: return character;
	}
}

static int terminal_send_key(struct shell_state *shell, unsigned int key)
{
	static const struct {
		unsigned int key;
		const char *sequence;
	} sequences[] = {
		{ KEY_UP, "\033[A" }, { KEY_DOWN, "\033[B" },
		{ KEY_RIGHT, "\033[C" }, { KEY_LEFT, "\033[D" },
		{ KEY_HOME, "\033[H" }, { KEY_END, "\033[F" },
		{ KEY_DELETE, "\033[3~" }, { KEY_PAGEUP, "\033[5~" },
		{ KEY_PAGEDOWN, "\033[6~" },
	};
	unsigned char character = key_character(key);
	unsigned int i;

	if (shell->terminal.master_fd < 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(sequences); i++)
		if (sequences[i].key == key)
			return terminal_write(&shell->terminal, sequences[i].sequence,
					      strlen(sequences[i].sequence));
	if (key == KEY_ENTER || key == KEY_KPENTER)
		return terminal_write(&shell->terminal, "\r", 1);
	if (key == KEY_BACKSPACE)
		return terminal_write(&shell->terminal, "\177", 1);
	if (key == KEY_TAB)
		return terminal_write(&shell->terminal, "\t", 1);
	if (key == KEY_ESC)
		return terminal_write(&shell->terminal, "\033", 1);
	if (!character)
		return 0;
	if (character >= 'a' && character <= 'z') {
		if (shell->shift_down ^ shell->caps_lock)
			character -= 'a' - 'A';
		if (shell->control_down)
			character &= 0x1f;
	} else if (shell->shift_down) {
		character = shifted_character(character);
	}
	return terminal_write(&shell->terminal, &character, 1);
}

static int handle_files_key(struct shell_files *files, unsigned int key)
{
	char parent[FILES_PATH_SIZE];

	switch (key) {
	case KEY_UP:
		return files_move_selection(files, -1);
	case KEY_DOWN:
		return files_move_selection(files, 1);
	case KEY_PAGEUP:
		return files_move_selection(files, -FILES_VISIBLE_ROWS);
	case KEY_PAGEDOWN:
		return files_move_selection(files, FILES_VISIBLE_ROWS);
	case KEY_HOME:
		return files_move_selection(files, -FILES_ENTRY_COUNT);
	case KEY_END:
		return files_move_selection(files, FILES_ENTRY_COUNT);
	case KEY_ENTER:
	case KEY_KPENTER:
	case KEY_RIGHT:
		return files_open_selected(files);
	case KEY_BACKSPACE:
	case KEY_LEFT:
		files_parent_path(files->path, parent, sizeof(parent));
		if (!strcmp(parent, files->path))
			return 0;
		(void)files_load(files, parent);
		return 1;
	default:
		return 0;
	}
}

static int handle_key_event(struct shell_state *shell, unsigned int key,
			    int value)
{
	if (key == KEY_LEFTSHIFT || key == KEY_RIGHTSHIFT) {
		shell->shift_down = value != 0;
		return 0;
	}
	if (key == KEY_LEFTCTRL || key == KEY_RIGHTCTRL) {
		shell->control_down = value != 0;
		return 0;
	}
	if (key == KEY_CAPSLOCK && value == 1) {
		shell->caps_lock = !shell->caps_lock;
		return 0;
	}
	if (value != 1 && value != 2)
		return 0;
	if (key == KEY_F12 || (key >= KEY_F1 && key <= KEY_F6))
		return handle_key(shell, key);
	if (shell->focused == SHELL_APP_TERMINAL &&
	    shell->windows[SHELL_APP_TERMINAL].visible &&
	    shell->terminal.master_fd >= 0) {
		if (terminal_send_key(shell, key) < 0)
			stop = 1;
		return 0;
	}
	if (shell->focused == SHELL_APP_FILES &&
	    shell->windows[SHELL_APP_FILES].visible) {
		if (value == 2 && key != KEY_UP && key != KEY_DOWN &&
		    key != KEY_PAGEUP && key != KEY_PAGEDOWN)
			return 0;
		return handle_files_key(&shell->files, key);
	}
	return value == 1 ? handle_key(shell, key) : 0;
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

static int files_entry_at(const struct shell_state *shell,
			  const struct shell_window *window)
{
	int row;
	unsigned int index;

	if (shell->pointer_x < window->x + 16 ||
	    shell->pointer_x >= window->x + window->width - 16 ||
	    shell->pointer_y < window->y + 64 ||
	    shell->pointer_y >= window->y + 64 + FILES_VISIBLE_ROWS * 20)
		return -1;
	row = (shell->pointer_y - window->y - 64) / 20;
	index = shell->files.scroll + row;
	if (index >= shell->files.count)
		return -1;
	return index;
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
	if (app == SHELL_APP_FILES) {
		int index = files_entry_at(shell, window);
		uint64_t now = shell_monotonic_ms();

		if (index < 0)
			return 1;
		shell->files.selected = index;
		if (shell->files.last_clicked == index &&
		    now - shell->files.last_click_ms <= 500) {
			shell->files.last_clicked = -1;
			return files_open_selected(&shell->files);
		}
		shell->files.last_clicked = index;
		shell->files.last_click_ms = now;
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
	*changed |= handle_key_event(shell, event->code, event->value);
}

static int poll_inputs(struct shell_state *shell)
{
	struct pollfd poll_fds[SHELL_INPUT_COUNT + 1];
	unsigned int i;
	unsigned int poll_count = shell->input_count;
	int terminal_index = -1;
	int changed = 0;
	int ret;

	for (i = 0; i < shell->input_count; i++) {
		poll_fds[i].fd = shell->inputs[i].fd;
		poll_fds[i].events = POLLIN;
		poll_fds[i].revents = 0;
	}
	if (shell->terminal.master_fd >= 0) {
		terminal_index = poll_count++;
		poll_fds[terminal_index].fd = shell->terminal.master_fd;
		poll_fds[terminal_index].events = POLLIN;
		poll_fds[terminal_index].revents = 0;
	}
	do {
		ret = poll(poll_fds, poll_count, SHELL_POLL_MS);
	} while (ret < 0 && errno == EINTR && !stop);
	if (stop)
		return 0;
	if (ret < 0)
		return -1;
	if (terminal_index >= 0 &&
	    (poll_fds[terminal_index].revents & (POLLIN | POLLHUP | POLLERR))) {
		ret = drain_terminal(&shell->terminal);
		if (ret < 0)
			return -1;
		changed |= ret;
	}

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
			unsigned int key = wheel > 0 ? KEY_UP : KEY_DOWN;

			if (shell->focused == SHELL_APP_FILES)
				changed |= handle_files_key(&shell->files, key);
			else
				changed |= handle_key(shell, key);
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
	update_vnc(shell, &shell->buffers[next]);
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
		.terminal = {
			.master_fd = -1,
		},
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
	if (start_terminal(&shell.terminal) < 0) {
		perror("start terminal");
		goto out;
	}
	refresh_system(&shell.system);

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
	if (start_vnc(&shell, &shell.buffers[0]) < 0) {
		fprintf(stderr, "unable to start VNC server\n");
		goto out;
	}

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
	printf("wii-kolibri-shell: terminal owns /bin/sh; F12 exits\n");
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
			if (shell.windows[SHELL_APP_SYSTEM].visible)
				refresh_system(&shell.system);
			changed = 1;
		}
		if (changed && present_shell(drm_fd, crtc.crtc_id, &shell) < 0) {
			perror("page flip");
			goto out;
		}
		process_vnc(&shell);
	}
	status = EXIT_SUCCESS;

out:
	stop_vnc(&shell);
	stop_terminal(&shell.terminal);
	for (i = 0; i < shell.input_count; i++) {
		if (shell.inputs[i].fd >= 0) {
			(void)ioctl(shell.inputs[i].fd, EVIOCGRAB, 0);
			close(shell.inputs[i].fd);
		}
	}
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
