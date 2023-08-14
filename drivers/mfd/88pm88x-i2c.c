/*
 * 88pm88x-i2c.c  --  88pm88x i2c bus interface
 *
 * Copyright (C) 2015 Marvell International Ltd.
 *  Yi Zhang <yizhang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/mfd/core.h>
#include <linux/mfd/88pm886.h>
#include <linux/mfd/88pm880.h>
#include <linux/mfd/88pm88x.h>

static int pm88x_i2c_probe(struct i2c_client *client)
{
	struct pm88x_chip *chip;
	struct device_node *node = client->dev.of_node;
	int ret = 0;

	chip = pm88x_init_chip(client);
	if (IS_ERR(chip)) {
		ret = PTR_ERR(chip);
		dev_err(chip->dev, "initialize 88pm88x chip fails!\n");
		goto err;
	}

	ret = pm88x_parse_dt(node, chip);
	if (ret < 0) {
		dev_err(chip->dev, "parse dt fails!\n");
		goto err;
	}

	ret = pm88x_init_pages(chip);
	if (ret) {
		dev_err(chip->dev, "initialize 88pm88x pages fails!\n");
		goto err;
	}

	ret = pm88x_post_init_chip(chip);
	if (ret) {
		dev_err(chip->dev, "post initialize 88pm88x fails!\n");
		goto err;
	}

	ret = pm88x_irq_init(chip);
	if (ret) {
		dev_err(chip->dev, "initialize 88pm88x interrupt fails!\n");
		goto err_init_irq;
	}

	ret = pm88x_init_subdev(chip);
	if (ret) {
		dev_err(chip->dev, "initialize 88pm88x sub-device fails\n");
		goto err_init_subdev;
	}

	/* patch for PMIC chip itself */
	ret = pm88x_apply_patch(chip);
	if (ret) {
		dev_err(chip->dev, "apply 88pm88x register patch fails\n");
		goto err_apply_patch;
	}

	/* fixup according PMIC stepping */
	ret = pm88x_stepping_fixup(chip);
	if (ret) {
		dev_err(chip->dev, "fixup according to chip stepping\n");
		goto err_apply_patch;
	}

	/* patch for board configuration */
	ret = pm88x_apply_board_fixup(chip, node);
	if (ret) {
		dev_err(chip->dev, "apply 88pm88x register for board fails\n");
		goto err_apply_patch;
	}

	pm_power_off = pm88x_power_off;

	chip->reboot_notifier.notifier_call = pm88x_reboot_notifier_callback;
	register_reboot_notifier(&(chip->reboot_notifier));

	return 0;

err_apply_patch:
	mfd_remove_devices(chip->dev);
err_init_subdev:
	regmap_del_irq_chip(chip->irq, chip->irq_data);
err_init_irq:
	pm800_exit_pages(chip);
err:
	return ret;
}

static void pm88x_i2c_remove(struct i2c_client *i2c)
{
	struct pm88x_chip *chip = dev_get_drvdata(&i2c->dev);
	pm88x_dev_exit(chip);
}

static const struct i2c_device_id pm88x_i2c_id[] = {
	{ "88pm886", PM886 },
	{ "88pm880", PM880 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pm88x_i2c_id);

static int pm88x_i2c_suspend(struct device *dev)
{
	return 0;
}

static int pm88x_i2c_resume(struct device *dev)
{
	return 0;
}

static SIMPLE_DEV_PM_OPS(pm88x_pm_ops, pm88x_i2c_suspend, pm88x_i2c_resume);

static struct i2c_driver pm88x_i2c_driver = {
	.driver = {
		.name	= "88pm88x",
		.owner	= THIS_MODULE,
		.pm	= &pm88x_pm_ops,
		.of_match_table	= of_match_ptr(pm88x_of_match),
	},
	.probe		= pm88x_i2c_probe,
	.remove		= pm88x_i2c_remove,
	.id_table	= pm88x_i2c_id,
};

static int __init pm88x_i2c_init(void)
{
	int ret;

	ret = i2c_add_driver(&pm88x_i2c_driver);
	if (ret != 0) {
		pr_err("88pm88x I2C registration failed %d\n", ret);
		return ret;
	}

	return 0;
}
subsys_initcall(pm88x_i2c_init);

static void __exit pm88x_i2c_exit(void)
{
	i2c_del_driver(&pm88x_i2c_driver);
}
module_exit(pm88x_i2c_exit);

MODULE_DESCRIPTION("88pm88x I2C bus interface");
MODULE_AUTHOR("Yi Zhang<yizhang@marvell.com>");
MODULE_LICENSE("GPL v2");
