// SPDX-License-Identifier: GPL-2.0
/*
 * Extcon driver for the onsemi FUSB301 type-c controller
 * Copyright (C) 2022 Xhivat Hoxhiq <xhivo97@gmail.com>
 *
 * Based on the TUSB320 extcon driver
 * Copyright (C) 2020 National Instruments Corporation
 */

#include <linux/extcon-provider.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>

#define FUSB301_DEVICE_ID	0x12

#define FUSB301_REG_DEVICEID	0x01
#define FUSB301_REG_MODE	0x02
#define FUSB301_REG_CONTROL	0x03
#define FUSB301_REG_RESET	0x05
#define FUSB301_REG_STATUS	0x11
#define FUSB301_REG_TYPE	0x12
#define FUSB301_REG_INTERRUPT	0x13

#define FUSB301_TYPE_MASK	0x1b
#define FUSB301_MODE_MASK	0x3f
#define FUSB301_STATUS_MASK	0x3f
#define FUSB301_INTERRUPT_MASK	0x0f

#define FUSB301_SW_RES_BIT	BIT(0)
#define FUSB301_INT_MASK_BIT	BIT(0)

#define FUSB301_ORIENT_SHIFT	4

enum fusb301_attached_state {
	FUSB301_ATTACHED_STATE_NONE,
	FUSB301_ATTACHED_STATE_AUD = BIT(0),
	FUSB301_ATTACHED_STATE_DBG = BIT(1),
	FUSB301_ATTACHED_STATE_DFP = BIT(3),
	FUSB301_ATTACHED_STATE_UFP = BIT(4),
};

struct fusb301_priv {
	struct device *dev;
	struct regmap *regmap;
	struct extcon_dev *edev;
	enum fusb301_attached_state state;
};

enum fusb301_mode {
	FUSB301_MODE_SRC	= BIT(0),
	FUSB301_MODE_SRC_ACC	= BIT(1),
	FUSB301_MODE_SNK	= BIT(2),
	FUSB301_MODE_SNK_ACC	= BIT(3),
	FUSB301_MODE_DRP	= BIT(4),
	FUSB301_MODE_DRP_ACC	= BIT(5),
};

static const unsigned int fusb301_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_NONE,
};

static const char * const fusb301_attached_states[] = {
	[FUSB301_ATTACHED_STATE_NONE]	= "not attached",
	[FUSB301_ATTACHED_STATE_DFP]	= "downstream facing port",
	[FUSB301_ATTACHED_STATE_UFP]	= "upstream facing port",
	[FUSB301_ATTACHED_STATE_AUD]	= "audio accessory",
	[FUSB301_ATTACHED_STATE_DBG]	= "debug accessory",
};

static const struct regmap_config fusb301_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static irqreturn_t fusb301_irq_thread(int irq, void *dev_id)
{
	int error;
	int polarity;
	unsigned int type;
	unsigned int status;
	unsigned int interrupt;

	struct fusb301_priv *priv = dev_id;

	error = regmap_read(priv->regmap, FUSB301_REG_INTERRUPT, &interrupt);
	if (error) {
		dev_err(priv->dev, "Could not read interrupts\n");
		return IRQ_NONE;
	}
	interrupt &= FUSB301_INTERRUPT_MASK;

	if (!interrupt)
		return IRQ_NONE;

	error = regmap_read(priv->regmap, FUSB301_REG_TYPE, &type);
	if (error) {
		dev_err(priv->dev, "Could not read type\n");
		return IRQ_NONE;
	}
	type &= FUSB301_TYPE_MASK;

	error = regmap_read(priv->regmap, FUSB301_REG_STATUS, &status);
	if (error) {
		dev_err(priv->dev, "Could not read status\n");
		return IRQ_NONE;
	}
	status &= FUSB301_STATUS_MASK;

	polarity = (status >> FUSB301_ORIENT_SHIFT) == 2;
	dev_dbg(priv->dev, "attached state: %s, polarity: %d\n",
			fusb301_attached_states[type], polarity);

	extcon_set_state(priv->edev, EXTCON_USB,
			type == FUSB301_ATTACHED_STATE_DFP);
	extcon_set_state(priv->edev, EXTCON_USB_HOST,
			type == FUSB301_ATTACHED_STATE_UFP);
	extcon_set_property(priv->edev, EXTCON_USB,
			EXTCON_PROP_USB_TYPEC_POLARITY,
			(union extcon_property_value)polarity);
	extcon_set_property(priv->edev, EXTCON_USB_HOST,
			EXTCON_PROP_USB_TYPEC_POLARITY,
			(union extcon_property_value)polarity);
	extcon_sync(priv->edev, EXTCON_USB);
	extcon_sync(priv->edev, EXTCON_USB_HOST);

	priv->state = type;

	return IRQ_HANDLED;
}

static int fusb301_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int error;
	unsigned int chip_id;
	struct fusb301_priv *priv;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->dev = &client->dev;

	priv->regmap = devm_regmap_init_i2c(client, &fusb301_regmap_config);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	error = regmap_read(priv->regmap, FUSB301_REG_DEVICEID, &chip_id);
	if (error) {
		dev_err(priv->dev, "Could not read device ID\n");
		return error;
	}
	if (chip_id != FUSB301_DEVICE_ID) {
		dev_err(priv->dev, "Device ID not recognized\n");
		return -ENXIO;
	}

	priv->edev = devm_extcon_dev_allocate(priv->dev, fusb301_extcon_cable);
	if (IS_ERR(priv->edev)) {
		dev_err(priv->dev, "Could not allocate extcon device\n");
		return PTR_ERR(priv->edev);
	}

	error = devm_extcon_dev_register(priv->dev, priv->edev);
	if (error) {
		dev_err(priv->dev, "Could not register extcon device\n");
		return error;
	}

	extcon_set_property_capability(priv->edev,
			EXTCON_USB, EXTCON_PROP_USB_TYPEC_POLARITY);
	extcon_set_property_capability(priv->edev,
			EXTCON_USB_HOST, EXTCON_PROP_USB_TYPEC_POLARITY);

	error = regmap_set_bits(priv->regmap, FUSB301_REG_RESET,
			FUSB301_SW_RES_BIT);
	if (error) {
		dev_err(priv->dev, "Could not reset device\n");
		return error;
	}

	error = regmap_write_bits(priv->regmap, FUSB301_REG_MODE,
			FUSB301_MODE_MASK, FUSB301_MODE_DRP);
	if (error) {
		dev_err(priv->dev, "Could not set mode\n");
		return error;
	}

	error = regmap_clear_bits(priv->regmap, FUSB301_REG_CONTROL,
			FUSB301_INT_MASK_BIT);
	if (error) {
		dev_err(priv->dev, "Could not enable interrupts\n");
		return error;
	}

	fusb301_irq_thread(client->irq, priv);

	error = devm_request_threaded_irq(priv->dev, client->irq,
			NULL, fusb301_irq_thread,
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			client->name, priv);
	if (error) {
		dev_err(priv->dev, "Could not register threaded IRQ\n");
		return error;
	}

	return 0;
}

static const struct of_device_id fusb301_dt_match[] = {
	{ .compatible = "onnn,fusb301" },
	{ },
};
MODULE_DEVICE_TABLE(of, fusb301_dt_match);

static struct i2c_driver fusb301_driver = {
	.probe		= fusb301_probe,
	.driver		= {
		.name	= "fusb301",
		.of_match_table = fusb301_dt_match,
	},
};

module_i2c_driver(fusb301_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Xhivat Hoxhiq <xhivo97@gmail.com>");
MODULE_DESCRIPTION("onsemi FUSB301 extcon driver");
