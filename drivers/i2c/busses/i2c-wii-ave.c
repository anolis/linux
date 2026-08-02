// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPIO-backed I2C adapter for the Nintendo Wii audio/video encoder.
 */

#include <linux/gpio/consumer.h>
#include <linux/i2c-algo-bit.h>
#include <linux/i2c.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

struct wii_ave_i2c {
	struct device *dev;
	struct gpio_desc *sda;
	struct gpio_desc *scl;
	struct i2c_adapter adapter;
	struct i2c_algo_bit_data bit;
	bool sda_output;
};

static void wii_ave_setsda(void *data, int state)
{
	struct wii_ave_i2c *i2c = data;
	int ret;

	if (!i2c->sda_output) {
		ret = gpiod_direction_output_raw(i2c->sda, state);
		if (ret) {
			dev_err_ratelimited(i2c->dev,
					    "failed to drive SDA: %d\n", ret);
			return;
		}
		i2c->sda_output = true;
		return;
	}

	gpiod_set_raw_value(i2c->sda, state);
}

static void wii_ave_setscl(void *data, int state)
{
	struct wii_ave_i2c *i2c = data;

	gpiod_set_raw_value(i2c->scl, state);
}

static int wii_ave_getsda(void *data)
{
	struct wii_ave_i2c *i2c = data;
	int ret;

	if (i2c->sda_output) {
		ret = gpiod_direction_input(i2c->sda);
		if (ret) {
			dev_err_ratelimited(i2c->dev,
					    "failed to release SDA: %d\n", ret);
			return 1;
		}
		i2c->sda_output = false;
	}

	return gpiod_get_raw_value(i2c->sda);
}

static int wii_ave_i2c_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct wii_ave_i2c *i2c;
	int ret;

	i2c = devm_kzalloc(dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	i2c->dev = dev;
	i2c->sda = devm_gpiod_get(dev, "sda", GPIOD_OUT_HIGH);
	if (IS_ERR(i2c->sda))
		return dev_err_probe(dev, PTR_ERR(i2c->sda),
				     "failed to acquire SDA\n");

	i2c->scl = devm_gpiod_get(dev, "scl", GPIOD_OUT_HIGH);
	if (IS_ERR(i2c->scl))
		return dev_err_probe(dev, PTR_ERR(i2c->scl),
				     "failed to acquire SCL\n");

	if (gpiod_cansleep(i2c->sda) || gpiod_cansleep(i2c->scl))
		return dev_err_probe(dev, -EINVAL,
				     "GPIO callbacks must be atomic\n");

	ret = gpiod_direction_output_raw(i2c->sda, 1);
	if (ret)
		return dev_err_probe(dev, ret, "failed to idle SDA high\n");
	i2c->sda_output = true;

	ret = gpiod_direction_output_raw(i2c->scl, 1);
	if (ret)
		return dev_err_probe(dev, ret, "failed to idle SCL high\n");

	i2c->bit.data = i2c;
	i2c->bit.setsda = wii_ave_setsda;
	i2c->bit.setscl = wii_ave_setscl;
	i2c->bit.getsda = wii_ave_getsda;
	i2c->bit.udelay = 2;
	i2c->bit.timeout = HZ / 10;
	i2c->bit.can_do_atomic = true;

	i2c->adapter.owner = THIS_MODULE;
	strscpy(i2c->adapter.name, "Nintendo Wii AVE I2C",
		sizeof(i2c->adapter.name));
	i2c->adapter.algo_data = &i2c->bit;
	i2c->adapter.dev.parent = dev;
	i2c->adapter.dev.of_node = dev->of_node;

	ret = i2c_bit_add_bus(&i2c->adapter);
	if (ret)
		return dev_err_probe(dev, ret, "failed to add I2C adapter\n");

	/* i2c-algo-bit tests the bus during registration; restore known idle. */
	wii_ave_setsda(i2c, 1);
	wii_ave_setscl(i2c, 1);

	platform_set_drvdata(pdev, i2c);
	dev_info(dev, "registered with active SCL and read-release SDA\n");
	return 0;
}

static void wii_ave_i2c_remove(struct platform_device *pdev)
{
	struct wii_ave_i2c *i2c = platform_get_drvdata(pdev);

	i2c_del_adapter(&i2c->adapter);
	wii_ave_setsda(i2c, 1);
	wii_ave_setscl(i2c, 1);
}

static const struct of_device_id wii_ave_i2c_of_match[] = {
	{ .compatible = "nintendo,wii-ave-i2c" },
	{ }
};
MODULE_DEVICE_TABLE(of, wii_ave_i2c_of_match);

static struct platform_driver wii_ave_i2c_driver = {
	.probe = wii_ave_i2c_probe,
	.remove = wii_ave_i2c_remove,
	.driver = {
		.name = "wii-ave-i2c",
		.of_match_table = wii_ave_i2c_of_match,
	},
};
module_platform_driver(wii_ave_i2c_driver);

MODULE_AUTHOR("Bill Carson <anolisporcatus@gmail.com> and OpenAI");
MODULE_DESCRIPTION("Nintendo Wii AVE GPIO I2C adapter");
MODULE_LICENSE("GPL");
