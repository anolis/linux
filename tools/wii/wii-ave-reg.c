// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define AVE_ADDR 0x70
#define AVE_SWAP_REG 0x62
#define AVE_SWAP_ENABLE 0x02
#define AVE_OVERSAMPLING_REG 0x65
#define AVE_OVERSAMPLING_LEGACY 0x01
#define AVE_OVERSAMPLING_LIBOGC 0x03
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

struct register_range {
	uint8_t first;
	uint8_t count;
};

struct ave_snapshot {
	uint8_t magic[8];
	uint8_t values[256];
};

enum action {
	ACTION_READ,
	ACTION_DUMP_STATE,
	ACTION_CLEAR_SWAP,
	ACTION_SET_SWAP,
	ACTION_SET_OVERSAMPLING_LEGACY,
	ACTION_SET_OVERSAMPLING_LIBOGC,
	ACTION_APPLY_LIBOGC_NTSC,
	ACTION_RESTORE_STATE,
};

static const uint8_t snapshot_magic[8] = {
	'W', 'I', 'I', 'A', 'V', 'E', '0', '1',
};

/* Keep the transaction order byte-exact with libogc's __VISetupEncoder(). */
static const struct register_range encoder_ranges[] = {
	{ 0x6a, 1 },
	{ 0x65, 1 },
	{ 0x01, 1 },
	{ 0x00, 1 },
	{ 0x71, 2 },
	{ 0x02, 1 },
	{ 0x05, 2 },
	{ 0x08, 2 },
	{ 0x7a, 4 },
	{ 0x40, 0x1a },
	{ 0x0a, 1 },
	{ 0x03, 1 },
	{ 0x10, 0x21 },
	{ 0x04, 1 },
	{ 0x6e, 1 },
};

static const uint8_t libogc_gamma[0x21] = {
	0x10, 0x00, 0x10, 0x00, 0x10, 0x00, 0x10, 0x00,
	0x10, 0x00, 0x10, 0x00, 0x10, 0x20, 0x40, 0x60,
	0x80, 0xa0, 0xeb, 0x10, 0x00, 0x20, 0x00, 0x40,
	0x00, 0x60, 0x00, 0x80, 0x00, 0xa0, 0x00, 0xeb,
	0x00,
};

static const uint8_t state_registers[] = {
	0x00, 0x01, 0x02, 0x03, 0x04,
	0x05, 0x06, 0x08, 0x09, 0x0a,
	0x62, 0x65, 0x6a, 0x6e,
	0x71, 0x72, 0x7a, 0x7b, 0x7c, 0x7d,
};

static int transfer(int fd, struct i2c_msg *msgs, unsigned int count)
{
	struct i2c_rdwr_ioctl_data data = {
		.msgs = msgs,
		.nmsgs = count,
	};

	if (ioctl(fd, I2C_RDWR, &data) < 0) {
		fprintf(stderr, "AVE I2C transaction failed: %s\n",
			strerror(errno));
		return -1;
	}

	return 0;
}

static int read_reg(int fd, uint8_t reg, uint8_t *value)
{
	struct i2c_msg msgs[] = {
		{
			.addr = AVE_ADDR,
			.len = 1,
			.buf = &reg,
		},
		{
			.addr = AVE_ADDR,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = value,
		},
	};

	return transfer(fd, msgs, 2);
}

static int write_reg(int fd, uint8_t reg, uint8_t value)
{
	uint8_t bytes[] = { reg, value };
	struct i2c_msg msg = {
		.addr = AVE_ADDR,
		.len = sizeof(bytes),
		.buf = bytes,
	};

	return transfer(fd, &msg, 1);
}

static int write_regs(int fd, uint8_t reg, const uint8_t *values, size_t count)
{
	uint8_t bytes[1 + sizeof(libogc_gamma)];
	struct i2c_msg msg = {
		.addr = AVE_ADDR,
		.len = count + 1,
		.buf = bytes,
	};

	if (count > sizeof(bytes) - 1) {
		errno = EINVAL;
		return -1;
	}

	bytes[0] = reg;
	memcpy(&bytes[1], values, count);
	return transfer(fd, &msg, 1);
}

static int read_full(int fd, void *buffer, size_t count)
{
	uint8_t *bytes = buffer;

	while (count) {
		ssize_t done = read(fd, bytes, count);

		if (done < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (!done) {
			errno = EIO;
			return -1;
		}
		bytes += done;
		count -= done;
	}

	return 0;
}

static int write_full(int fd, const void *buffer, size_t count)
{
	const uint8_t *bytes = buffer;

	while (count) {
		ssize_t done = write(fd, bytes, count);

		if (done < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (!done) {
			errno = EIO;
			return -1;
		}
		bytes += done;
		count -= done;
	}

	return 0;
}

static int capture_state(int fd, struct ave_snapshot *snapshot)
{
	size_t i;

	memset(snapshot, 0, sizeof(*snapshot));
	memcpy(snapshot->magic, snapshot_magic, sizeof(snapshot_magic));

	for (i = 0; i < ARRAY_SIZE(encoder_ranges); i++) {
		const struct register_range *range = &encoder_ranges[i];
		unsigned int offset;

		for (offset = 0; offset < range->count; offset++) {
			uint8_t reg = range->first + offset;

			if (read_reg(fd, reg, &snapshot->values[reg]))
				return -1;
		}
	}

	return 0;
}

static int save_state(int i2c_fd, const char *path,
		      struct ave_snapshot *snapshot)
{
	int fd;
	int saved_errno;

	if (capture_state(i2c_fd, snapshot))
		return -1;

	fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (fd < 0) {
		fprintf(stderr, "cannot create snapshot %s: %s\n", path,
			strerror(errno));
		return -1;
	}

	if (write_full(fd, snapshot, sizeof(*snapshot)) || fsync(fd))
		goto fail;
	if (close(fd)) {
		fd = -1;
		goto fail;
	}
	printf("saved AVE snapshot: %s (%zu bytes)\n", path,
	       sizeof(*snapshot));
	return 0;

fail:
	saved_errno = errno;
	if (fd >= 0)
		close(fd);
	unlink(path);
	errno = saved_errno;
	fprintf(stderr, "cannot persist snapshot %s: %s\n", path,
		strerror(errno));
	return -1;
}

static int load_state(const char *path, struct ave_snapshot *snapshot)
{
	uint8_t extra;
	ssize_t extra_count;
	int fd = open(path, O_RDONLY);
	int rc = -1;

	if (fd < 0) {
		fprintf(stderr, "cannot open snapshot %s: %s\n", path,
			strerror(errno));
		return -1;
	}

	if (read_full(fd, snapshot, sizeof(*snapshot))) {
		fprintf(stderr, "cannot read snapshot %s: %s\n", path,
			strerror(errno));
		goto out;
	}
	do {
		extra_count = read(fd, &extra, 1);
	} while (extra_count < 0 && errno == EINTR);
	if (extra_count < 0) {
		fprintf(stderr, "cannot validate snapshot %s: %s\n", path,
			strerror(errno));
		goto out;
	}
	if (extra_count) {
		fprintf(stderr, "snapshot %s has an invalid size\n", path);
		goto out;
	}
	if (memcmp(snapshot->magic, snapshot_magic, sizeof(snapshot_magic))) {
		fprintf(stderr, "snapshot %s has an invalid header\n", path);
		goto out;
	}

	rc = 0;
out:
	close(fd);
	return rc;
}

static void build_libogc_ntsc_state(struct ave_snapshot *state)
{
	static const uint8_t gamma_first = 0x10;

	memset(state, 0, sizeof(*state));
	memcpy(state->magic, snapshot_magic, sizeof(snapshot_magic));
	state->values[0x6a] = 1;
	state->values[0x65] = 3;
	/* NTSC plus a connected component/DTV output, matching this test rig. */
	state->values[0x01] = 0x20;
	state->values[0x71] = 0x8e;
	state->values[0x72] = 0x8e;
	state->values[0x02] = 7;
	state->values[0x03] = 1;
	memcpy(&state->values[gamma_first], libogc_gamma,
	       sizeof(libogc_gamma));
	state->values[0x04] = 1;
}

static int apply_state(int fd, const struct ave_snapshot *state)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(encoder_ranges); i++) {
		const struct register_range *range = &encoder_ranges[i];

		if (write_regs(fd, range->first, &state->values[range->first],
			       range->count))
			return -1;
		usleep(2);
	}

	return 0;
}

static int verify_state(int fd, const struct ave_snapshot *expected)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(encoder_ranges); i++) {
		const struct register_range *range = &encoder_ranges[i];
		unsigned int offset;

		for (offset = 0; offset < range->count; offset++) {
			uint8_t reg = range->first + offset;
			uint8_t actual;

			if (read_reg(fd, reg, &actual))
				return -1;
			if (actual != expected->values[reg]) {
				fprintf(stderr,
					"AVE[0x%02x] verify failed: expected 0x%02x, got 0x%02x\n",
					reg, expected->values[reg], actual);
				errno = EIO;
				return -1;
			}
		}
	}

	return 0;
}

static int apply_libogc_ntsc(int fd, const char *snapshot_path)
{
	struct ave_snapshot snapshot;
	struct ave_snapshot libogc_state;

	if (save_state(fd, snapshot_path, &snapshot))
		return -1;
	build_libogc_ntsc_state(&libogc_state);
	if (apply_state(fd, &libogc_state) || verify_state(fd, &libogc_state)) {
		fprintf(stderr, "libogc AVE reset failed; restore %s immediately\n",
			snapshot_path);
		return -1;
	}

	printf("applied and verified libogc AVE NTSC/DTV reset\n");
	return 0;
}

static int restore_state(int fd, const char *snapshot_path)
{
	struct ave_snapshot snapshot;

	if (load_state(snapshot_path, &snapshot))
		return -1;
	if (apply_state(fd, &snapshot) || verify_state(fd, &snapshot)) {
		fprintf(stderr, "AVE snapshot restoration failed: %s\n",
			snapshot_path);
		return -1;
	}

	printf("restored and verified AVE snapshot: %s\n", snapshot_path);
	return 0;
}

static int dump_state(int fd)
{
	unsigned int i;

	for (i = 0; i < sizeof(state_registers); i++) {
		uint8_t reg = state_registers[i];
		uint8_t value;

		if (read_reg(fd, reg, &value))
			return -1;
		printf("AVE[0x%02x]: 0x%02x\n", reg, value);
	}

	return 0;
}

static void print_help(const char *program)
{
	fprintf(stderr, "usage: %s [i2c-device] [action]\n", program);
	fprintf(stderr, "actions: --dump-state --clear-swap --set-swap\n");
	fprintf(stderr, "         --set-oversampling-1 --set-oversampling-3\n");
	fprintf(stderr, "         --apply-libogc-ntsc SNAPSHOT\n");
	fprintf(stderr, "         --restore-state SNAPSHOT\n");
}

int main(int argc, char **argv)
{
	const char *device = "/dev/i2c-0";
	const char *snapshot_path = NULL;
	enum action action = ACTION_READ;
	uint8_t reg = AVE_SWAP_REG;
	uint8_t value;
	int fd;

	if (argc > 4) {
		print_help(argv[0]);
		return 2;
	}
	if (argc > 1)
		device = argv[1];
	if (argc > 2 && !strcmp(argv[2], "--dump-state")) {
		action = ACTION_DUMP_STATE;
	} else if (argc > 2 && !strcmp(argv[2], "--clear-swap")) {
		action = ACTION_CLEAR_SWAP;
	} else if (argc > 2 && !strcmp(argv[2], "--set-swap")) {
		action = ACTION_SET_SWAP;
	} else if (argc > 2 && !strcmp(argv[2], "--set-oversampling-1")) {
		action = ACTION_SET_OVERSAMPLING_LEGACY;
	} else if (argc > 2 && !strcmp(argv[2], "--set-oversampling-3")) {
		action = ACTION_SET_OVERSAMPLING_LIBOGC;
	} else if (argc > 2 && !strcmp(argv[2], "--apply-libogc-ntsc")) {
		action = ACTION_APPLY_LIBOGC_NTSC;
	} else if (argc > 2 && !strcmp(argv[2], "--restore-state")) {
		action = ACTION_RESTORE_STATE;
	} else if (argc > 2) {
		print_help(argv[0]);
		return 2;
	}
	if (action == ACTION_APPLY_LIBOGC_NTSC ||
	    action == ACTION_RESTORE_STATE) {
		if (argc != 4) {
			print_help(argv[0]);
			return 2;
		}
		snapshot_path = argv[3];
	} else if (argc > 3) {
		print_help(argv[0]);
		return 2;
	}

	fd = open(device, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "cannot open %s: %s\n", device, strerror(errno));
		return 1;
	}

	if (action == ACTION_DUMP_STATE) {
		if (dump_state(fd))
			goto fail;
		close(fd);
		return 0;
	}
	if (action == ACTION_APPLY_LIBOGC_NTSC) {
		int rc = apply_libogc_ntsc(fd, snapshot_path);

		close(fd);
		return rc ? 1 : 0;
	}
	if (action == ACTION_RESTORE_STATE) {
		int rc = restore_state(fd, snapshot_path);

		close(fd);
		return rc ? 1 : 0;
	}

	if (action == ACTION_SET_OVERSAMPLING_LEGACY ||
	    action == ACTION_SET_OVERSAMPLING_LIBOGC)
		reg = AVE_OVERSAMPLING_REG;

	if (read_reg(fd, reg, &value))
		goto fail;
	printf("AVE[0x%02x] before: 0x%02x\n", reg, value);

	if (action != ACTION_READ) {
		if (action == ACTION_SET_SWAP)
			value = AVE_SWAP_ENABLE;
		else if (action == ACTION_SET_OVERSAMPLING_LEGACY)
			value = AVE_OVERSAMPLING_LEGACY;
		else if (action == ACTION_SET_OVERSAMPLING_LIBOGC)
			value = AVE_OVERSAMPLING_LIBOGC;
		else
			value = 0;
		printf("AVE[0x%02x] write:  0x%02x\n", reg, value);
		if (write_reg(fd, reg, value))
			goto fail;
		if (read_reg(fd, reg, &value))
			goto fail;
		printf("AVE[0x%02x] after:  0x%02x\n", reg, value);
	}

	close(fd);
	return 0;

fail:
	close(fd);
	return 1;
}
