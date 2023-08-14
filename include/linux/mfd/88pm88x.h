/*
 * Marvell 88PM88X PMIC Common Interface
 *
 * Copyright (C) 2014 Marvell International Ltd.
 *  Yi Zhang <yizhang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * This file configures the common part of the 88pm88x series PMIC
 */

#ifndef __LINUX_MFD_88PM88X_H
#define __LINUX_MFD_88PM88X_H

#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/atomic.h>
#include <linux/reboot.h>
#include <linux/mod_devicetable.h>
#include "88pm88x-reg.h"
#include "88pm886-reg.h"

#define PM88X_RTC_NAME		"88pm88x-rtc"
#define PM88X_ONKEY_NAME	"88pm88x-onkey"
#define PM88X_CHARGER_NAME	"88pm88x-charger"
#define PM88X_BATTERY_NAME	"88pm88x-battery"
#define PM88X_HEADSET_NAME	"88pm88x-headset"
#define PM88X_VBUS_NAME		"88pm88x-vbus"
#define PM88X_CFD_NAME		"88pm88x-leds"
#define PM88X_RGB_NAME		"88pm88x-rgb"
#define PM88X_GPADC_NAME	"88pm88x-gpadc"
#define PM88X_DVC_NAME		"88pm88x-dvc"

enum pm88x_type {
	PM886 = 1,
	PM880 = 2,
};

enum pm88x_pages {
	PM88X_BASE_PAGE = 0,
	PM88X_LDO_PAGE,
	PM88X_GPADC_PAGE,
	PM88X_BATTERY_PAGE,
	PM88X_BUCK_PAGE = 4,
	PM88X_TEST_PAGE = 7,
};

enum pm88x_gpadc {
	PM88X_NO_GPADC = -1,
	PM88X_GPADC0 = 0,
	PM88X_GPADC1,
	PM88X_GPADC2,
	PM88X_GPADC3,
};

/* interrupt number */
enum pm88x_irq_number {
	PM88X_IRQ_ONKEY,	/* EN1b0 *//* 0 */
	PM88X_IRQ_EXTON,	/* EN1b1 */
	PM88X_IRQ_CHG_GOOD,	/* EN1b2 */
	PM88X_IRQ_BAT_DET,	/* EN1b3 */
	PM88X_IRQ_RTC,		/* EN1b4 */
	PM88X_IRQ_CLASSD,	/* EN1b5 *//* 5 */
	PM88X_IRQ_XO,		/* EN1b6 */
	PM88X_IRQ_GPIO,		/* EN1b7 */

	PM88X_IRQ_VBAT,		/* EN2b0 *//* 8 */
				/* EN2b1 */
	PM88X_IRQ_VBUS,		/* EN2b2 */
	PM88X_IRQ_ITEMP,	/* EN2b3 *//* 10 */
	PM88X_IRQ_BUCK_PGOOD,	/* EN2b4 */
	PM88X_IRQ_LDO_PGOOD,	/* EN2b5 */

	PM88X_IRQ_GPADC0,	/* EN3b0 */
	PM88X_IRQ_GPADC1,	/* EN3b1 */
	PM88X_IRQ_GPADC2,	/* EN3b2 *//* 15 */
	PM88X_IRQ_GPADC3,	/* EN3b3 */
	PM88X_IRQ_MIC_DET,	/* EN3b4 */
	PM88X_IRQ_HS_DET,	/* EN3b5 */
	PM88X_IRQ_GND_DET,	/* EN3b6 */

	PM88X_IRQ_CHG_FAIL,	/* EN4b0 *//* 20 */
	PM88X_IRQ_CHG_DONE,	/* EN4b1 */
				/* EN4b2 */
	PM88X_IRQ_CFD_FAIL,	/* EN4b3 */
	PM88X_IRQ_OTG_FAIL,	/* EN4b4 */
	PM88X_IRQ_CHG_ILIM,	/* EN4b5 *//* 25 */
				/* EN4b6 */
	PM88X_IRQ_CC,		/* EN4b7 *//* 27 */

	PM88X_MAX_IRQ,			   /* 28 */
};

/* 3 rgb led indicators */
enum {
	PM88X_RGB_LED0,
	PM88X_RGB_LED1,
	PM88X_RGB_LED2,
};

/* camera flash/torch */
enum {
	PM88X_NO_LED = -1,
	PM88X_FLASH_LED = 0,
	PM88X_TORCH_LED,
};

struct pm88x_dvc_ops {
	void (*level_to_reg)(u8 level);
};

struct pm88x_buck1_dvc_desc {
	u8 current_reg;
	int max_level;
	int uV_step1;
	int uV_step2;
	int min_uV;
	int mid_uV;
	int max_uV;
	int mid_reg_val;
};

struct pm88x_dvc {
	struct device *dev;
	struct pm88x_chip *chip;
	struct pm88x_dvc_ops ops;
	struct pm88x_buck1_dvc_desc desc;
};

struct pm88x_chip {
	struct i2c_client *client;
	struct device *dev;

	struct i2c_client *ldo_page;	/* chip client for ldo page */
	struct i2c_client *power_page;	/* chip client for power page */
	struct i2c_client *gpadc_page;	/* chip client for gpadc page */
	struct i2c_client *battery_page;/* chip client for battery page */
	struct i2c_client *buck_page;	/* chip client for buck page */
	struct i2c_client *test_page;	/* chip client for test page */

	struct regmap *base_regmap;
	struct regmap *ldo_regmap;
	struct regmap *power_regmap;
	struct regmap *gpadc_regmap;
	struct regmap *battery_regmap;
	struct regmap *buck_regmap;
	struct regmap *test_regmap;
	struct regmap *codec_regmap;

	unsigned short ldo_page_addr;	/* ldo page I2C address */
	unsigned short power_page_addr;	/* power page I2C address */
	unsigned short gpadc_page_addr;	/* gpadc page I2C address */
	unsigned short battery_page_addr;/* battery page I2C address */
	unsigned short buck_page_addr;	/* buck page I2C address */
	unsigned short test_page_addr;	/* test page I2C address */

	unsigned int chip_id;
	long type;			/* specific chip */
	int irq;

	int irq_mode;			/* write/read clear */
	struct regmap_irq_chip_data *irq_data;

	bool rtc_wakeup;		/* is it powered up by expired alarm? */
	u8 powerdown1;			/* save power down reason */
	u8 powerdown2;
	u8 powerup;			/* the reason of power on */

	struct notifier_block reboot_notifier;
	struct pm88x_dvc *dvc;
};

extern struct regmap_irq_chip pm88x_irq_chip;
extern const struct of_device_id pm88x_of_match[];

struct pm88x_chip *pm88x_init_chip(struct i2c_client *client);
int pm88x_parse_dt(struct device_node *np, struct pm88x_chip *chip);

int pm88x_init_pages(struct pm88x_chip *chip);
int pm88x_post_init_chip(struct pm88x_chip *chip);
void pm800_exit_pages(struct pm88x_chip *chip);

int pm88x_init_subdev(struct pm88x_chip *chip);
long pm88x_of_get_type(struct device *dev);
void pm88x_dev_exit(struct pm88x_chip *chip);

int pm88x_irq_init(struct pm88x_chip *chip);
int pm88x_irq_exit(struct pm88x_chip *chip);
int pm88x_apply_patch(struct pm88x_chip *chip);
int pm88x_stepping_fixup(struct pm88x_chip *chip);
int pm88x_apply_board_fixup(struct pm88x_chip *chip, struct device_node *np);

struct pm88x_chip *pm88x_get_chip(void);
void pm88x_set_chip(struct pm88x_chip *chip);
void pm88x_power_off(void);
int pm88x_reboot_notifier_callback(struct notifier_block *nb,
				   unsigned long code, void *unused);

#endif /* __LINUX_MFD_88PM88X_H */
