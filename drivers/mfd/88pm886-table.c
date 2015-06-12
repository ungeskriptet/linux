/*
 * Marvell 88PM886 specific setting
 *
 * Copyright (C) 2015 Marvell International Ltd.
 *  Yi Zhang <yizhang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/mfd/88pm88x.h>
#include <linux/mfd/88pm886.h>
#include <linux/mfd/88pm886-reg.h>
#include <linux/mfd/88pm88x-reg.h>

#include "88pm88x.h"

#define PM886_BUCK_NAME		"88pm886-buck"
#define PM886_LDO_NAME		"88pm886-ldo"

const struct regmap_config pm886_base_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xfe,
};

const struct regmap_config pm886_power_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xfe,
};

const struct regmap_config pm886_gpadc_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xfe,
};

const struct regmap_config pm886_battery_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xfe,
};

const struct regmap_config pm886_test_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xfe,
};

static const struct resource buck_resources[] = {
	{
	.name = PM886_BUCK_NAME,
	},
};

static const struct resource ldo_resources[] = {
	{
	.name = PM886_LDO_NAME,
	},
};

const struct mfd_cell pm886_cell_devs[] = {
	CELL_DEV(PM886_BUCK_NAME, buck_resources, "marvell,88pm886-buck1", 0),
	CELL_DEV(PM886_BUCK_NAME, buck_resources, "marvell,88pm886-buck2", 1),
	CELL_DEV(PM886_BUCK_NAME, buck_resources, "marvell,88pm886-buck3", 2),
	CELL_DEV(PM886_BUCK_NAME, buck_resources, "marvell,88pm886-buck4", 3),
	CELL_DEV(PM886_BUCK_NAME, buck_resources, "marvell,88pm886-buck5", 4),
	CELL_DEV(PM886_LDO_NAME, ldo_resources, "marvell,88pm886-ldo1", 5),
	CELL_DEV(PM886_LDO_NAME, ldo_resources, "marvell,88pm886-ldo2", 6),
	CELL_DEV(PM886_LDO_NAME, ldo_resources, "marvell,88pm886-ldo3", 7),
	CELL_DEV(PM886_LDO_NAME, ldo_resources, "marvell,88pm886-ldo4", 8),
	CELL_DEV(PM886_LDO_NAME, ldo_resources, "marvell,88pm886-ldo5", 9),
	CELL_DEV(PM886_LDO_NAME, ldo_resources, "marvell,88pm886-ldo6", 10),
	CELL_DEV(PM886_LDO_NAME, ldo_resources, "marvell,88pm886-ldo7", 11),
	CELL_DEV(PM886_LDO_NAME, ldo_resources, "marvell,88pm886-ldo8", 12),
	CELL_DEV(PM886_LDO_NAME, ldo_resources, "marvell,88pm886-ldo9", 13),
	CELL_DEV(PM886_LDO_NAME, ldo_resources, "marvell,88pm886-ldo10", 14),
	CELL_DEV(PM886_LDO_NAME, ldo_resources, "marvell,88pm886-ldo11", 15),
	CELL_DEV(PM886_LDO_NAME, ldo_resources, "marvell,88pm886-ldo12", 16),
	CELL_DEV(PM886_LDO_NAME, ldo_resources, "marvell,88pm886-ldo13", 17),
	CELL_DEV(PM886_LDO_NAME, ldo_resources, "marvell,88pm886-ldo14", 18),
	CELL_DEV(PM886_LDO_NAME, ldo_resources, "marvell,88pm886-ldo15", 19),
	CELL_DEV(PM886_LDO_NAME, ldo_resources, "marvell,88pm886-ldo16", 20),
};

struct pmic_cell_info pm886_cell_info = {
	.cells   = pm886_cell_devs,
	.cell_nr = ARRAY_SIZE(pm886_cell_devs),
};

static const struct reg_default pm886_base_patch[] = {
	{PM88X_WDOG, 0x1},	 /* disable watchdog */
	{PM88X_GPIO_CTRL1, 0x40}, /* gpio1: dvc    , gpio0: input   */
	{PM88X_GPIO_CTRL2, 0x00}, /*               , gpio2: input   */
	{PM88X_GPIO_CTRL3, 0x44}, /* dvc2          , dvc1           */
	{PM88X_GPIO_CTRL4, 0x00}, /* gpio5v_1:input, gpio5v_2: input*/
	{PM88X_AON_CTRL2, 0x2a},  /* output 32kHZ from XO */
	{PM88X_BK_OSC_CTRL1, 0x0f}, /* OSC_FREERUN = 1, to lock FLL */
	{PM88X_LOWPOWER2, 0x20}, /* XO_LJ = 1, enable low jitter for 32kHZ */
	/* enable LPM for internal reference group in sleep */
	{PM88X_LOWPOWER4, 0xc0},
	{PM88X_BK_OSC_CTRL3, 0xc0}, /* set the duty cycle of charger DC/DC to max */
};

static const struct reg_default pm886_power_patch[] = {
};

static const struct reg_default pm886_gpadc_patch[] = {
	{PM88X_GPADC_CONFIG6, 0x03}, /* enable non-stop mode */
};

static const struct reg_default pm886_battery_patch[] = {
	{PM88X_CHGBK_CONFIG6, 0xe1},
};

static const struct reg_default pm886_test_patch[] = {
};

/* 88pm886 chip itself related */
int pm886_apply_patch(struct pm88x_chip *chip)
{
	int ret, size;

	if (!chip || !chip->base_regmap || !chip->power_regmap ||
	    !chip->gpadc_regmap || !chip->battery_regmap ||
	    !chip->test_regmap)
		return -EINVAL;

	size = ARRAY_SIZE(pm886_base_patch);
	if (size == 0)
		goto power;
	ret = regmap_register_patch(chip->base_regmap, pm886_base_patch, size);
	if (ret < 0)
		return ret;

power:
	size = ARRAY_SIZE(pm886_power_patch);
	if (size == 0)
		goto gpadc;
	ret = regmap_register_patch(chip->power_regmap, pm886_power_patch, size);
	if (ret < 0)
		return ret;

gpadc:
	size = ARRAY_SIZE(pm886_gpadc_patch);
	if (size == 0)
		goto battery;
	ret = regmap_register_patch(chip->gpadc_regmap, pm886_gpadc_patch, size);
	if (ret < 0)
		return ret;
battery:
	size = ARRAY_SIZE(pm886_battery_patch);
	if (size == 0)
		goto test;
	ret = regmap_register_patch(chip->battery_regmap, pm886_battery_patch, size);
	if (ret < 0)
		return ret;

test:
	size = ARRAY_SIZE(pm886_test_patch);
	if (size == 0)
		goto out;
	ret = regmap_register_patch(chip->test_regmap, pm886_test_patch, size);
	if (ret < 0)
		return ret;
out:
	return 0;
}
EXPORT_SYMBOL_GPL(pm886_apply_patch);
