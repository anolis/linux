// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define AVE_ADDR 0x70
#define AVE_SWAP_REG 0x62

static int transfer(int fd, struct i2c_msg *msgs, unsigned int count)
{
	struct i2c_rdwr_ioctl_data data = {
		.msgs = msgs,
		.nmsgs = count,
	};

	if (ioctl(fd, I2C_RDWR, &data) < 0) {
		fprintf(stderr, "AVE I2C transfer failed: %s\n", strerror(errno));
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

int main(int argc, char **argv)
{
	const char *device = "/dev/i2c-0";
	bool clear_swap = false;
	uint8_t value;
	int fd;

	if (argc > 1)
		device = argv[1];
	if (argc > 2 && !strcmp(argv[2], "--clear-swap"))
		clear_swap = true;
	else if (argc > 2) {
		fprintf(stderr, "usage: %s [i2c-device] [--clear-swap]\n", argv[0]);
		return 2;
	}

	fd = open(device, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "cannot open %s: %s\n", device, strerror(errno));
		return 1;
	}

	if (read_reg(fd, AVE_SWAP_REG, &value))
		goto fail;
	printf("AVE[0x%02x] before: 0x%02x\n", AVE_SWAP_REG, value);

	if (clear_swap) {
		if (write_reg(fd, AVE_SWAP_REG, 0))
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
