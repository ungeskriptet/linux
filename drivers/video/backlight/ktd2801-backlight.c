// SPDX-License-Identifier: GPL-2.0-only
#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>

#define EW_DELAY	150
#define EW_DET		270
#define LOW_BIT_HIGH	5
#define LOW_BIT_LOW	(4 * HIGH_BIT_LOW)
#define HIGH_BIT_LOW	5
#define HIGH_BIT_HIGH	(4 * HIGH_BIT_LOW)
#define DS		5
#define EOD_L		10
#define EOD_H		350
#define PWR_DOWN_DELAY	2600

#define KTD2801_DEFAULT_BRIGHTNESS	100
#define KTD2801_MAX_BRIGHTNESS		255

struct ktd2801_backlight {
	struct device *dev;
	struct backlight_device *bd;
	struct gpio_desc *desc;
	bool was_on;
};

static int ktd2801_update_status(struct backlight_device *bd)
{
	struct ktd2801_backlight *ktd2801 = bl_get_data(bd);
	u8 brightness = (u8) backlight_get_brightness(bd);

	if (backlight_is_blank(bd)) {
		gpiod_set_value(ktd2801->desc, 1);
		udelay(PWR_DOWN_DELAY);
		ktd2801->was_on = false;
		return 0;
	}

	if (!ktd2801->was_on) {
		gpiod_set_value(ktd2801->desc, 0);
		udelay(EW_DELAY);
		gpiod_set_value(ktd2801->desc, 1);
		udelay(EW_DET);
		gpiod_set_value(ktd2801->desc, 0);
		ktd2801->was_on = true;
	}

	gpiod_set_value(ktd2801->desc, 0);
	udelay(DS);

	for (int i = 0; i < 8; i++) {
		u8 next_bit = (brightness & 0x80) >> 7;

		if (!next_bit) {
			gpiod_set_value(ktd2801->desc, 1);
			udelay(LOW_BIT_LOW);
			gpiod_set_value(ktd2801->desc, 0);
			udelay(LOW_BIT_HIGH);
		} else {
			gpiod_set_value(ktd2801->desc, 1);
			udelay(HIGH_BIT_LOW);
			gpiod_set_value(ktd2801->desc, 0);
			udelay(HIGH_BIT_HIGH);
		}
		brightness <<= 1;
	}
	gpiod_set_value(ktd2801->desc, 1);
	udelay(EOD_L);
	gpiod_set_value(ktd2801->desc, 0);
	udelay(EOD_H);
	return 0;
}

static const struct backlight_ops ktd2801_backlight_ops = {
	.update_status = ktd2801_update_status,
};

static int ktd2801_backlight_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct backlight_device *bd;
	struct ktd2801_backlight *ktd2801;
	u32 brightness, max_brightness;
	int ret;

	ktd2801 = devm_kzalloc(dev, sizeof(*ktd2801), GFP_KERNEL);
	if (!ktd2801)
		return -ENOMEM;
	ktd2801->dev = dev;
	ktd2801->was_on = true;

	ret = device_property_read_u32(dev, "max-brightness", &max_brightness);
	if (ret)
		max_brightness = KTD2801_MAX_BRIGHTNESS;
	if (max_brightness > KTD2801_MAX_BRIGHTNESS) {
		dev_err(dev, "illegal max brightness specified\n");
		max_brightness = KTD2801_MAX_BRIGHTNESS;
	}

	ret = device_property_read_u32(dev, "default-brightness", &brightness);
	if (ret)
		brightness = KTD2801_DEFAULT_BRIGHTNESS;
	if (brightness > max_brightness) {
		dev_err(dev, "default brightness exceeds max\n");
		brightness = max_brightness;
	}

	ktd2801->desc = devm_gpiod_get(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(ktd2801->desc))
		return dev_err_probe(dev, PTR_ERR(ktd2801->desc),
				"failed to get backlight GPIO");
	gpiod_set_consumer_name(ktd2801->desc, dev_name(dev));

	bd = devm_backlight_device_register(dev, dev_name(dev), dev, ktd2801,
			&ktd2801_backlight_ops, NULL);
	if (IS_ERR(bd))
		return dev_err_probe(dev, PTR_ERR(bd),
				"failed to register backlight");

	bd->props.max_brightness = max_brightness;
	bd->props.brightness = brightness;

	ktd2801->bd = bd;
	platform_set_drvdata(pdev, bd);
	backlight_update_status(bd);

	return 0;
}

static const struct of_device_id ktd2801_of_match[] = {
	{ .compatible = "kinetic,ktd2801" },
	{ }
};
MODULE_DEVICE_TABLE(of, ktd2801_of_match);

static struct platform_driver ktd2801_backlight_driver = {
	.driver = {
		.name = "ktd2801-backlight",
		.of_match_table = ktd2801_of_match,
	},
	.probe = ktd2801_backlight_probe,
};
module_platform_driver(ktd2801_backlight_driver);

MODULE_AUTHOR("Duje Mihanović <duje.mihanovic@skole.hr>");
MODULE_DESCRIPTION("Kinetic KTD2801 Backlight Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ktd2801-backlight");
