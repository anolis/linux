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

enum action {
	ACTION_READ,
	ACTION_DUMP_STATE,
	ACTION_CLEAR_SWAP,
	ACTION_SET_SWAP,
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
	fprintf(stderr,
		"usage: %s [i2c-device] [--dump-state|--clear-swap|--set-swap]\n",
		program);
}

int main(int argc, char **argv)
{
	const char *device = "/dev/i2c-0";
	enum action action = ACTION_READ;
	uint8_t value;
	int fd;

	if (argc > 3) {
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
	} else if (argc > 2) {
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

	if (read_reg(fd, AVE_SWAP_REG, &value))
		goto fail;
	printf("AVE[0x%02x] before: 0x%02x\n", AVE_SWAP_REG, value);

	if (action != ACTION_READ) {
		value = action == ACTION_SET_SWAP ? AVE_SWAP_ENABLE : 0;
		printf("AVE[0x%02x] write:  0x%02x\n", AVE_SWAP_REG, value);
		if (write_reg(fd, AVE_SWAP_REG, value))
			goto fail;
		if (read_reg(fd, AVE_SWAP_REG, &value))
			goto fail;
		printf("AVE[0x%02x] after:  0x%02x\n", AVE_SWAP_REG, value);
	}

	close(fd);
	return 0;

fail:
	close(fd);
	return 1;
}
