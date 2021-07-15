// SPDX-License-Identifier: GPL-2.0-only
/*
 * Power supply driver for Qualcomm Switch-Mode Battery Charger
 *
 * Copyright (c) 2021 Yassine Oudjana <y.oudjana@protonmail.com>
 * Copyright (c) 2021 Alejandro Tafalla <atafalla@dnyon.com>
 */

#include <linux/errno.h>
#include <linux/extcon-provider.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/regulator/driver.h>
#include <linux/util_macros.h>
#include <asm/unaligned.h>
#include <linux/reboot.h>

#include "qcom_smbcharger.h"

/**
 * @brief smbchg_sec_write() - Write to secure registers
 *
 * @param chip Pointer to smbchg_chip
 * @param val  Buffer to write bytes from
 * @param addr Address of register to write into
 * @param len  Length of register in bytes
 * @return int 0 on success, -errno on failure
 *
 * @details: Some registers need to be unlocked first before being written.
 * This function unlocks a given secure register then writes a given value
 * to it.
 */
static int smbchg_sec_write(struct smbchg_chip *chip, u8 *val, u16 addr,
			    int len)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&chip->sec_access_lock, flags);

	ret = regmap_write(chip->regmap, ((chip->base + addr) & PERIPHERAL_MASK)
			| SEC_ACCESS_OFFSET, SEC_ACCESS_VALUE);
	if (ret)
		goto out;

	ret = regmap_bulk_write(chip->regmap, chip->base + addr, val, len);
out:
	spin_unlock_irqrestore(&chip->sec_access_lock, flags);
	return ret;
}

/**
 * @brief smbchg_sec_masked_write() - Masked write to secure registers
 *
 * @param chip Pointer to smbchg_chip
 * @param addr Address of register to write into
 * @param mask Mask to apply on value
 * @param val  value to be written
 * @return int 0 on success, -errno on failure
 *
 * @details: Masked version of smbchg_sec_write().
 */
static int smbchg_sec_masked_write(struct smbchg_chip *chip, u16 addr, u8 mask, u8 val)
{
	u8 reg;
	int ret;

	ret = regmap_bulk_read(chip->regmap, chip->base + addr, &reg, 1);
	if (ret)
		return ret;

	reg &= ~mask;
	reg |= val & mask;

	return smbchg_sec_write(chip, &reg, addr, 1);
}

/**
 * @brief smbchg_usb_is_present() - Check for USB presence
 *
 * @param chip Pointer to smbchg_chip.
 * @return true if USB present, false otherwise
 */
static bool smbchg_usb_is_present(struct smbchg_chip *chip)
{
	u32 value;
	int ret;

	ret = regmap_read(chip->regmap,
			chip->base + SMBCHG_USB_CHGPTH_RT_STS, &value);
	if (ret) {
		dev_err(chip->dev,
			"Failed to read USB charge path real-time status: %pe\n",
			ERR_PTR(ret));
		return false;
	}

	if (!(value & USBIN_SRC_DET_BIT) || (value & USBIN_OV_BIT))
		return false;

	ret = regmap_read(chip->regmap,
			chip->base + SMBCHG_USB_CHGPTH_INPUT_STS, &value);
	if (ret) {
		dev_err(chip->dev,
			"Failed to read USB charge path input status: %pe\n",
			ERR_PTR(ret));
		return false;
	}

	return !!(value & (USBIN_9V | USBIN_UNREG | USBIN_LV));
}

/**
 * @brief smbchg_usb_enable() - Enable/disable USB charge path
 *
 * @param chip   Pointer to smbchg_chip
 * @param enable true to enable, false to disable
 * @return int 0 on success, -errno on failure
 */
static int smbchg_usb_enable(struct smbchg_chip *chip, bool enable)
{
	int ret;

	dev_dbg(chip->dev, "%sabling USB charge path\n", enable ? "En" : "Dis");

	ret = regmap_update_bits(chip->regmap,
				chip->base + SMBCHG_USB_CHGPTH_CMD_IL,
				USBIN_SUSPEND_BIT,
				enable ? 0 : USBIN_SUSPEND_BIT);
	if (ret)
		dev_err(chip->dev, "Failed to %sable USB charge path: %pe\n",
			enable ? "en" : "dis", ERR_PTR(ret));

	return ret;
}

/**
 * @brief smbchg_usb_get_type() - Get USB port type
 *
 * @param chip Pointer to smbchg_chip
 * @return enum power_supply_usb_type, the type of the connected USB port
 */
static enum power_supply_usb_type smbchg_usb_get_type(struct smbchg_chip *chip)
{
	u32 reg;
	int ret;

	ret = regmap_read(chip->regmap, chip->base + SMBCHG_MISC_IDEV_STS, &reg);
	if (ret) {
		dev_err(chip->dev, "Failed to read USB type: %pe\n", ERR_PTR(ret));
		return POWER_SUPPLY_USB_TYPE_UNKNOWN;
	}

	if (reg & USB_TYPE_SDP_BIT)
		return POWER_SUPPLY_USB_TYPE_SDP;
	else if (reg & USB_TYPE_OTHER_BIT ||
		 reg & USB_TYPE_DCP_BIT)
		return POWER_SUPPLY_USB_TYPE_DCP;
	else if (reg & USB_TYPE_CDP_BIT)
		return POWER_SUPPLY_USB_TYPE_CDP;
	else
		return POWER_SUPPLY_USB_TYPE_UNKNOWN;
}

/**
 * @brief smbchg_usb_set_ilim_lc() - Get USB input current limit
 *
 * @param chip Pointer to smbchg_chip
 * @return Current limit in microamperes on success, -errno on failure
 *
 * @details: Get currently configured input current limit on the USB
 * charge path.
 */
static int smbchg_usb_get_ilim(struct smbchg_chip *chip)
{
	bool usb_3, full_current;
	int value, ret;

	ret = regmap_read(chip->regmap, chip->base + SMBCHG_USB_CHGPTH_CMD_IL,
			&value);
	if (ret)
		return ret;

	if (value & USBIN_MODE_HC_BIT) {
		/* High current mode */
		ret = regmap_read(chip->regmap,
				chip->base + SMBCHG_USB_CHGPTH_IL_CFG, &value);
		if (ret)
			return ret;

		return chip->data->ilim_usb_table[value & USBIN_INPUT_MASK];
	} else {
		/* Low current mode */
		ret = regmap_read(chip->regmap,
				chip->base + SMBCHG_USB_CHGPTH_CHGPTH_CFG, &value);
		if (ret)
			return ret;

		usb_3 = value & USB_2_3_SEL_BIT;

		ret = regmap_read(chip->regmap,
				chip->base + SMBCHG_USB_CHGPTH_CMD_IL, &value);
		if (ret)
			return ret;

		full_current = value & USB51_MODE_BIT;

		return smbchg_lc_ilim(usb_3, full_current);
	}
}

/**
 * @brief smbchg_usb_set_ilim_lc() - Set USB input current limit using
 * low current mode
 *
 * @param chip       Pointer to smbchg_chip
 * @param current_ua Target current limit in microamperes
 * @return Actual current limit in microamperes on success, -errno on failure
 *
 * @details: Low current mode provides four options for USB input current
 * limiting:
 * - 100mA (USB 2.0 limited SDP current)
 * - 150mA (USB 3.0 limited SDP current)
 * - 500mA (USB 2.0 full SDP current)
 * - 900mA (USB 3.0 full SDP current)
 * This mode is most suitable for use with SDPs.
 */
static int smbchg_usb_set_ilim_lc(struct smbchg_chip *chip, int current_ua)
{
	bool usb_3;
	bool full_current;
	u8 ilim_mask;
	int ret;

	if (current_ua < smbchg_lc_ilim_options[0])
		/* Target current limit too small */
		return -EINVAL;

	ilim_mask = find_smaller_in_array(smbchg_lc_ilim_options,
				ARRAY_SIZE(smbchg_lc_ilim_options),
				current_ua);
	usb_3 = ilim_mask & LC_ILIM_USB3_BIT;
	full_current = ilim_mask & LC_ILIM_FULL_CURRENT_BIT;

	/* Set USB version */
	ret = smbchg_sec_masked_write(chip, SMBCHG_USB_CHGPTH_CHGPTH_CFG,
				      USB_2_3_SEL_BIT, usb_3 ? USB_2_3_SEL_BIT : 0);
	if (ret) {
		dev_err(chip->dev,
			"Failed to set USB version: %pe\n", ERR_PTR(ret));
		return ret;
	}

	/* Set USB current level */
	ret = regmap_update_bits(chip->regmap,
				 chip->base + SMBCHG_USB_CHGPTH_CMD_IL,
				 USB51_MODE_BIT, full_current ? USB51_MODE_BIT : 0);
	if (ret) {
		dev_err(chip->dev, "Failed to set USB current level: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	/* Disable high current mode */
	ret = regmap_update_bits(chip->regmap,
				 chip->base + SMBCHG_USB_CHGPTH_CMD_IL,
				 USBIN_MODE_HC_BIT, 0);
	if (ret) {
		dev_err(chip->dev, "Failed to disable high current mode: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	dev_dbg(chip->dev, "LC mode current limit set to %duA (%duA requested)\n",
		smbchg_lc_ilim_options[ilim_mask], current_ua);

	return smbchg_lc_ilim_options[ilim_mask];
}

/**
 * @brief smbchg_usb_set_ilim_hc() - Set USB input current limit using
 * high current mode
 *
 * @param chip       Pointer to smbchg_chip
 * @param current_ua Target current limit in microamperes
 * @return Actual current limit in microamperes on success, -errno on failure
 *
 * @details: High current mode provides a large range of input current limits
 * with granular control, but does not reach limits as low as the low
 * current mode does. This mode is most suitable for use with DCPs and CDPs.
 */
static int smbchg_usb_set_ilim_hc(struct smbchg_chip *chip, int current_ua)
{
	size_t ilim_index;
	int ilim;
	int ret;

	if (current_ua < chip->data->ilim_usb_table[0])
		/* Target current limit too small */
		return -EINVAL;

	/*
	 * Get index of closest current limit supported by the charger.
	 * Always round down to avoid exceeding the target limit.
	 */
	ilim_index = find_smaller_in_array(chip->data->ilim_usb_table,
					chip->data->ilim_usb_table_len,
					current_ua);
	ilim = chip->data->ilim_usb_table[ilim_index];

	/* Set the current limit index */
	ret = smbchg_sec_masked_write(chip, SMBCHG_USB_CHGPTH_IL_CFG,
				USBIN_INPUT_MASK, ilim_index);
	if (ret) {
		dev_err(chip->dev,
			"Failed to set current limit index: %pe\n", ERR_PTR(ret));
		return ret;
	}

	/* Enable high current mode to bring the current limit into effect */
	ret = regmap_update_bits(chip->regmap,
				chip->base + SMBCHG_USB_CHGPTH_CMD_IL,
				USBIN_MODE_HC_BIT, USBIN_MODE_HC_BIT);
	if (ret) {
		dev_err(chip->dev,
			"Failed to set high current mode: %pe\n", ERR_PTR(ret));
		return ret;
	}

	dev_dbg(chip->dev, "HC mode current limit set to %duA (%duA requested)\n",
		ilim, current_ua);

	return ilim;
}

/**
 * @brief smbchg_charging_enable() - Enable battery charging
 *
 * @param chip   Pointer to smbchg_chip
 * @param enable true to enable, false to disable
 * @return int 0 on success, -errno on failure
 */
static int smbchg_charging_enable(struct smbchg_chip *chip, bool enable)
{
	int ret;

	ret = regmap_update_bits(chip->regmap,
				chip->base + SMBCHG_BAT_IF_CMD_CHG,
				CHG_EN_BIT,
				enable ? 0 : CHG_EN_BIT);
	if (ret)
		dev_err(chip->dev, "Failed to enable battery charging: %pe\n",
			ERR_PTR(ret));

	return ret;
}

/**
 * @brief smbchg_charging_get_iterm() - Get charge termination current
 *
 * @param chip Pointer to smbchg_chip
 * @return Charge termination current in microamperes on success,
 * -errno on failure
 */
static int smbchg_charging_get_iterm(struct smbchg_chip *chip)
{
	unsigned int iterm_index;
	int ret;

	ret = regmap_read(chip->regmap, chip->base + SMBCHG_CHGR_TCC_CFG, &iterm_index);
	if (ret)
		return ret;

	return chip->data->iterm_table[iterm_index & CHG_ITERM_MASK];
}

/**
 * @brief smbchg_charging_set_iterm() - Set charge termination current
 *
 * @param chip       Pointer to smbchg_chip
 * @param current_ua Termination current in microamperes
 * @return int 0 on success, -errno on failure
 */
static int smbchg_charging_set_iterm(struct smbchg_chip *chip, int current_ua)
{
	size_t iterm_count = chip->data->iterm_table_len;
	unsigned int iterm_index;

	iterm_index = find_closest(current_ua, chip->data->iterm_table,	iterm_count);

	dev_dbg(chip->dev, "Setting termination current to %dmA",
		chip->data->iterm_table[iterm_index]);

	return smbchg_sec_masked_write(chip, SMBCHG_CHGR_TCC_CFG,
				CHG_ITERM_MASK, iterm_index);
}

/**
 * @brief smbchg_batt_is_present() - Check for battery presence
 *
 * @param chip Pointer to smbchg_chip
 * @return true if battery present, false otherwise
 */
static bool smbchg_batt_is_present(struct smbchg_chip *chip)
{
	unsigned int value;
	int ret;

	ret = regmap_read(chip->regmap, chip->base + SMBCHG_BAT_IF_RT_STS, &value);
	if (ret) {
		dev_err(chip->dev,
			"Failed to read battery real-time status: %pe\n", ERR_PTR(ret));
		return false;
	}

	return !(value & BAT_MISSING_BIT);
}

/**
 * @brief smbchg_otg_is_present() - Check for OTG presence
 *
 * @param chip Pointer to smbchg_chip
 * @return true if OTG present, false otherwise
 */
static bool smbchg_otg_is_present(struct smbchg_chip *chip)
{
	u32 value;
	u16 usb_id;
	int ret;

	/* Check ID pin */
	ret = regmap_bulk_read(chip->regmap,
				chip->base + SMBCHG_USB_CHGPTH_USBID_MSB,
				&value, 2);
	if(ret) {
		dev_err(chip->dev, "Failed to read ID pin: %pe\n", ERR_PTR(ret));
		return false;
	}

	put_unaligned_be16(value, &usb_id);
	if (usb_id > USBID_GND_THRESHOLD) {
		dev_dbg(chip->dev,
			"0x%02x read on ID pin, too high to be ground\n", usb_id);
		return false;
	}

	ret = regmap_read(chip->regmap, chip->base + SMBCHG_USB_CHGPTH_RID_STS,
				&value);
	if(ret) {
		dev_err(chip->dev, "Failed to read resistance ID: %pe\n", ERR_PTR(ret));
		return false;
	}

	return (value & RID_MASK) == 0;
}

/**
 * @brief smbchg_otg_enable() - Enable OTG regulator
 *
 * @param rdev Pointer to regulator_dev
 * @return int 0 on success, -errno on failure
 */
static int smbchg_otg_enable(struct regulator_dev *rdev)
{
	struct smbchg_chip *chip = rdev_get_drvdata(rdev);
	int ret;

	dev_dbg(chip->dev, "Enabling OTG VBUS regulator");

	ret = regmap_update_bits(chip->regmap,
				chip->base + SMBCHG_BAT_IF_CMD_CHG,
				OTG_EN_BIT, OTG_EN_BIT);
	if(ret)
		dev_err(chip->dev, "Failed to enable OTG regulator: %pe\n", ERR_PTR(ret));

	return ret;
}

/**
 * @brief smbchg_otg_disable() - Disable OTG regulator
 *
 * @param rdev Pointer to regulator_dev
 * @return int 0 on success, -errno on failure
 */
static int smbchg_otg_disable(struct regulator_dev *rdev)
{
	struct smbchg_chip *chip = rdev_get_drvdata(rdev);
	int ret;

	dev_dbg(chip->dev, "Disabling OTG VBUS regulator");

	ret = regmap_update_bits(chip->regmap,
				chip->base + SMBCHG_BAT_IF_CMD_CHG,
				OTG_EN_BIT, 0);
	if (ret) {
		dev_err(chip->dev, "Failed to disable OTG regulator: %pe\n", ERR_PTR(ret));
		return ret;
	}

	return 0;
}

/**
 * @brief smbchg_otg_is_enabled() - Check if OTG regulator is enabled
 *
 * @param rdev Pointer to regulator_dev
 * @return int 1 if enabled, 0 if disabled
 */
static int smbchg_otg_is_enabled(struct regulator_dev *rdev)
{
	struct smbchg_chip *chip = rdev_get_drvdata(rdev);
	u32 value = 0;
	int ret;

	ret = regmap_read(chip->regmap,
			chip->base + SMBCHG_BAT_IF_CMD_CHG, &value);
	if (ret)
		dev_err(chip->dev, "Failed to read OTG regulator status\n");

	return !!(value & OTG_EN_BIT);
}

static const struct regulator_ops smbchg_otg_ops = {
	.enable = smbchg_otg_enable,
	.disable = smbchg_otg_disable,
	.is_enabled = smbchg_otg_is_enabled,
};

static void smbchg_otg_reset_worker(struct work_struct *work)
{
	struct smbchg_chip *chip = container_of(work, struct smbchg_chip,
						otg_reset_work);
	int ret;

	dev_dbg(chip->dev, "Resetting OTG VBUS regulator\n");

	ret = regmap_update_bits(chip->regmap,
				chip->base + SMBCHG_BAT_IF_CMD_CHG,
				OTG_EN_BIT, 0);
	if (ret) {
		dev_err(chip->dev,
			"Failed to disable OTG regulator for reset: %pe\n", ERR_PTR(ret));
		return;
	}

	msleep(OTG_RESET_DELAY_MS);

	/*
	 * Only re-enable the OTG regulator if OTG is still present
	 * after sleeping
	 */
	if (!smbchg_otg_is_present(chip))
		return;

	ret = regmap_update_bits(chip->regmap,
				chip->base + SMBCHG_BAT_IF_CMD_CHG,
				OTG_EN_BIT, OTG_EN_BIT);
	if (ret)
		dev_err(chip->dev,
			"Failed to re-enable OTG regulator after reset: %pe\n",
			ERR_PTR(ret));
}

/**
 * @brief smbchg_extcon_update() - Update extcon properties and sync cables
 *
 * @param chip Pointer to smbchg_chip
 */
static void smbchg_extcon_update(struct smbchg_chip *chip)
{
	enum power_supply_usb_type usb_type = smbchg_usb_get_type(chip);
	bool usb_present = smbchg_usb_is_present(chip);
	bool otg_present = smbchg_otg_is_present(chip);
	int otg_vbus_present = smbchg_otg_is_enabled(chip->otg_reg);

	extcon_set_state(chip->edev, EXTCON_USB, usb_present);
	extcon_set_state(chip->edev, EXTCON_USB_HOST, otg_present);
	extcon_set_property(chip->edev, EXTCON_USB_HOST, EXTCON_PROP_USB_VBUS,
				(union extcon_property_value)otg_vbus_present);

	if (usb_present) {
		extcon_set_state(chip->edev, EXTCON_CHG_USB_SDP,
				usb_type == POWER_SUPPLY_USB_TYPE_SDP);
		extcon_set_state(chip->edev, EXTCON_CHG_USB_DCP,
				usb_type == POWER_SUPPLY_USB_TYPE_DCP);
		extcon_set_state(chip->edev, EXTCON_CHG_USB_CDP,
				usb_type == POWER_SUPPLY_USB_TYPE_CDP);
		extcon_set_property(chip->edev, EXTCON_USB, EXTCON_PROP_USB_VBUS,
				(union extcon_property_value)
				(usb_type != POWER_SUPPLY_USB_TYPE_UNKNOWN));
	} else {
		/*
		 * Charging extcon cables and VBUS are unavailable when
		 * USB is not present.
		 */
		extcon_set_state(chip->edev, EXTCON_CHG_USB_SDP, false);
		extcon_set_state(chip->edev, EXTCON_CHG_USB_DCP, false);
		extcon_set_state(chip->edev, EXTCON_CHG_USB_CDP, false);
		extcon_set_property(chip->edev, EXTCON_USB, EXTCON_PROP_USB_VBUS,
				(union extcon_property_value)false);
	}

	/* Sync all extcon cables */
	extcon_sync(chip->edev, EXTCON_USB);
	extcon_sync(chip->edev, EXTCON_USB_HOST);
	extcon_sync(chip->edev, EXTCON_CHG_USB_SDP);
	extcon_sync(chip->edev, EXTCON_CHG_USB_DCP);
	extcon_sync(chip->edev, EXTCON_CHG_USB_CDP);
}

static const unsigned int smbchg_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_CHG_USB_SDP,
	EXTCON_CHG_USB_DCP,
	EXTCON_CHG_USB_CDP,
	EXTCON_NONE,
};

static irqreturn_t smbchg_handle_charger_error(int irq, void *data)
{
	struct smbchg_chip *chip = data;
	int ret, reg;

	dev_err(chip->dev, "Charger error");

	/* TODO: Handle errors properly */
	ret = regmap_read(chip->regmap, chip->base + SMBCHG_CHGR_RT_STS, &reg);
	if (ret)
		dev_err(chip->dev,
			"Failed to read charger real-time status: %pe\n", ERR_PTR(ret));

	if (reg & CHG_COMP_SFT_BIT)
		dev_warn(chip->dev, "Safety timer expired");
		/* TODO: probably disable charging here */

	power_supply_changed(chip->usb_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smbchg_handle_p2f(int irq, void *data)
{
	struct smbchg_chip *chip = data;

	dev_dbg(chip->dev, "Switching to fast charging");

	power_supply_changed(chip->usb_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smbchg_handle_rechg(int irq, void *data)
{
	struct smbchg_chip *chip = data;

	dev_dbg(chip->dev, "Recharging");

	/* Auto-recharge is enabled, nothing to do here */
	return IRQ_HANDLED;
}

static irqreturn_t smbchg_handle_taper(int irq, void *data)
{
	struct smbchg_chip *chip = data;

	dev_dbg(chip->dev, "Switching to taper charging");

	power_supply_changed(chip->usb_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smbchg_handle_tcc(int irq, void *data)
{
	struct smbchg_chip *chip = data;

	dev_dbg(chip->dev, "Termination current reached");

	power_supply_changed(chip->usb_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smbchg_handle_batt_temp(int irq, void *data)
{
	struct smbchg_chip *chip = data;

	power_supply_changed(chip->usb_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smbchg_handle_batt_presence(int irq, void *data)
{
	struct smbchg_chip* chip = data;
	bool batt_present = smbchg_batt_is_present(chip);

	dev_dbg(chip->dev,
		"Battery %spresent\n", batt_present ? "" : "not ");

	power_supply_changed(chip->usb_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smbchg_handle_usb_source_detect(int irq, void *data)
{
	struct smbchg_chip *chip = data;
	bool usb_present;
	enum power_supply_usb_type usb_type;
	int ret = 0;

	usb_present = smbchg_usb_is_present(chip);
	dev_dbg(chip->dev, "USB %spresent\n", usb_present ? "" : "not ");

	usb_type = smbchg_usb_get_type(chip);

	if (usb_present) {
		switch (usb_type) {
		case POWER_SUPPLY_USB_TYPE_SDP:
			ret = smbchg_usb_set_ilim_lc(chip, DEFAULT_SDP_ILIM);
			break;
		case POWER_SUPPLY_USB_TYPE_DCP:
			ret = smbchg_usb_set_ilim_hc(chip, DEFAULT_DCP_ILIM);
			break;
		case POWER_SUPPLY_USB_TYPE_CDP:
			ret = smbchg_usb_set_ilim_hc(chip, DEFAULT_CDP_ILIM);
			break;
		default:
			/* Avoid charging from unknown sources */
			ret = smbchg_usb_enable(chip, false);
		}

		if (ret < 0) {
			dev_err(chip->dev,
				"Failed to set USB current limit: %pe\n", ERR_PTR(ret));
			return IRQ_NONE;
		}
	}

	ret = smbchg_usb_enable(chip, usb_present);
	if(ret) {
		dev_dbg(chip->dev, "Failed to %sable USB charge path: %pe\n",
			usb_present ? "en" : "dis", ERR_PTR(ret));
		return IRQ_NONE;
	}

	smbchg_extcon_update(chip);

	power_supply_changed(chip->usb_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smbchg_handle_usbid_change(int irq, void *data)
{
	struct smbchg_chip *chip = data;
	bool otg_present;

	/* 
	 * ADC conversion for USB resistance ID in the fuel gauge can take
	 * up to 15ms to finish after the USB ID change interrupt is fired.
	 * Wait for it to finish before detecting OTG presence. Add an extra
	 * 5ms for good measure.
	 */
	msleep(20);

	otg_present = smbchg_otg_is_present(chip);
	dev_dbg(chip->dev, "OTG %spresent\n", otg_present ? "" : "not ");

	smbchg_extcon_update(chip);

	return IRQ_HANDLED;
}

static irqreturn_t smbchg_handle_otg_fail(int irq, void *data)
{
	struct smbchg_chip *chip = data;

	dev_err(chip->dev, "OTG regulator failure");

	/* Report failure */
	regulator_notifier_call_chain(chip->otg_reg,
					REGULATOR_EVENT_FAIL, NULL);

	return IRQ_HANDLED;
}

static irqreturn_t smbchg_handle_otg_oc(int irq, void *data)
{
	struct smbchg_chip *chip = data;

	/*
	 * Inrush current of some devices can trip the over-current protection
	 * on the PMI8994 and PMI8996 smbchargers due to a hardware bug.
	 * Try resetting the OTG regulator, and only report over-current
	 * if it persists.
	 */
	if (of_device_is_compatible(chip->dev->of_node, "qcom,pmi8994-smbcharger") ||
		of_device_is_compatible(chip->dev->of_node, "qcom,pmi8996-smbcharger")) {
		if (chip->otg_resets < NUM_OTG_RESET_RETRIES) {
			schedule_work(&chip->otg_reset_work);
			chip->otg_resets++;
			return IRQ_HANDLED;
		}

		chip->otg_resets = 0;
	}

	dev_warn(chip->dev, "OTG over-current");

	/* Report over-current */
	regulator_notifier_call_chain(chip->otg_reg,
					REGULATOR_EVENT_OVER_CURRENT, NULL);

	/* Regulator is automatically disabled in hardware on over-current */
	regulator_notifier_call_chain(chip->otg_reg,
					REGULATOR_EVENT_DISABLE, NULL);

	return IRQ_HANDLED;
}

static irqreturn_t smbchg_handle_temp_shutdown(int irq, void *data)
{
	struct smbchg_chip *chip = data;

	hw_protection_shutdown("Charger thermal emergency", 100);

	smbchg_charging_enable(chip, false);
	smbchg_usb_enable(chip, false);

	power_supply_changed(chip->usb_psy);

	return IRQ_HANDLED;
}

/* TODO: Handle all interrupts */
const struct smbchg_irq smbchg_irqs[] = {
	{ "chg-error", smbchg_handle_charger_error },
	{ "chg-inhibit", NULL },
	{ "chg-prechg-sft", NULL },
	{ "chg-complete-chg-sft", NULL },
	{ "chg-p2f-thr", smbchg_handle_p2f },
	{ "chg-rechg-thr", smbchg_handle_rechg },
	{ "chg-taper-thr", smbchg_handle_taper },
	{ "chg-tcc-thr", smbchg_handle_tcc },
	{ "batt-hot", smbchg_handle_batt_temp },
	{ "batt-warm", smbchg_handle_batt_temp },
	{ "batt-cold", smbchg_handle_batt_temp },
	{ "batt-cool", smbchg_handle_batt_temp },
	{ "batt-ov", NULL },
	{ "batt-low", NULL },
	{ "batt-missing", smbchg_handle_batt_presence },
	{ "batt-term-missing", NULL },
	{ "usbin-uv", NULL },
	{ "usbin-ov", NULL },
	{ "usbin-src-det", smbchg_handle_usb_source_detect },
	{ "usbid-change", smbchg_handle_usbid_change },
 	{ "otg-fail", smbchg_handle_otg_fail },
 	{ "otg-oc", smbchg_handle_otg_oc },
	{ "aicl-done", NULL },
	{ "dcin-uv", NULL },
	{ "dcin-ov", NULL },
	{ "power-ok", NULL },
	{ "temp-shutdown", smbchg_handle_temp_shutdown },
	{ "wdog-timeout", NULL },
	{ "flash-fail", NULL },
	{ "otst2", NULL },
	{ "otst3", NULL },
};

/**
 * @brief smbchg_get_charge_type() - Get charge type
 *
 * @param chip Pointer to smbchg_chip
 * @return Charge type, as defined in <linux/power_supply.h>
 */
static int smbchg_get_charge_type(struct smbchg_chip *chip)
{
	int value, ret;

	ret = regmap_read(chip->regmap,
			chip->base + SMBCHG_CHGR_STS, &value);
	if (ret) {
		dev_err(chip->dev, "Failed to read charger status: %pe\n", ERR_PTR(ret));
		return POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
	}

	value = (value & CHG_TYPE_MASK) >> CHG_TYPE_SHIFT;
	dev_vdbg(chip->dev, "Charge type: 0x%x", value);
	switch (value) {
	case BATT_NOT_CHG_VAL:
		return POWER_SUPPLY_CHARGE_TYPE_NONE;
	case BATT_PRE_CHG_VAL:
		/* Low-current precharging */
		return POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
	case BATT_FAST_CHG_VAL:
		/* Constant current fast charging */
	case BATT_TAPER_CHG_VAL:
		/* Constant voltage fast charging */
		return POWER_SUPPLY_CHARGE_TYPE_FAST;
	default:
		dev_err(chip->dev,
			"Invalid charge type value 0x%x read\n", value);
		return POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
	}
}

/**
 * @brief smbchg_get_health() - Get health
 *
 * @param chip Pointer to smbchg_chip
 * @return Battery health, as defined in <linux/power_supply.h>
 */
static int smbchg_get_health(struct smbchg_chip *chip)
{
	int value, ret;
	bool batt_present = smbchg_batt_is_present(chip);

	if (!batt_present)
		return POWER_SUPPLY_HEALTH_NO_BATTERY;

	ret = regmap_read(chip->regmap, chip->base + SMBCHG_BAT_IF_RT_STS,
				&value);
	if (ret) {
		dev_err(chip->dev,
			"Failed to read battery real-time status: %pe\n", ERR_PTR(ret));
		return POWER_SUPPLY_HEALTH_UNKNOWN;
	}

	if (value & HOT_BAT_HARD_BIT)
		return POWER_SUPPLY_HEALTH_OVERHEAT;
	else if (value & HOT_BAT_SOFT_BIT)
		return POWER_SUPPLY_HEALTH_WARM;
	else if (value & COLD_BAT_HARD_BIT)
		return POWER_SUPPLY_HEALTH_COLD;
	else if (value & COLD_BAT_SOFT_BIT)
		return POWER_SUPPLY_HEALTH_COOL;

	return POWER_SUPPLY_HEALTH_GOOD;
}

/**
 * @brief smbchg_get_status() - Get status
 *
 * @param chip Pointer to smbchg_chip
 * @return battery status, as defined in <linux/power_supply.h>
 */
static int smbchg_get_status(struct smbchg_chip *chip)
{
	int value, ret, chg_type;

	/* Check if power input is present */
	/* TODO: Add DC charge path */
	if (!smbchg_usb_is_present(chip))
		return POWER_SUPPLY_STATUS_DISCHARGING;

	ret = regmap_read(chip->regmap,
			chip->base + SMBCHG_CHGR_RT_STS, &value);
	if (ret) {
		dev_err(chip->dev,
			"Failed to read charger real-time status: %pe\n", ERR_PTR(ret));
		return POWER_SUPPLY_STATUS_UNKNOWN;
	}
	dev_vdbg(chip->dev, "Charger real-time status: 0x%x", value);

	/* Check if temination current is reached or if charging is inhibited */
	if (value & BAT_TCC_REACHED_BIT || value & CHG_INHIBIT_BIT)
		return POWER_SUPPLY_STATUS_FULL;

	ret = regmap_read(chip->regmap,
			chip->base + SMBCHG_CHGR_STS, &value);
	if (ret) {
		dev_err(chip->dev, "Failed to read charger status: %pe\n", ERR_PTR(ret));
		return POWER_SUPPLY_STATUS_UNKNOWN;
	}

	dev_vdbg(chip->dev, "Charger status: 0x%x", value);

	/* Check for charger hold-off */
	if (value & CHG_HOLD_OFF_BIT)
		return POWER_SUPPLY_STATUS_NOT_CHARGING;

	chg_type = smbchg_get_charge_type(chip);
	switch (chg_type) {
	case POWER_SUPPLY_CHARGE_TYPE_UNKNOWN:
		return POWER_SUPPLY_STATUS_UNKNOWN;
	case POWER_SUPPLY_CHARGE_TYPE_NONE:
		return POWER_SUPPLY_STATUS_DISCHARGING;
	default:
		return POWER_SUPPLY_STATUS_CHARGING;
	}
}

static int smbchg_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smbchg_chip *chip = power_supply_get_drvdata(psy);
	int ret;

	dev_vdbg(chip->dev, "Getting property: %d", psp);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = smbchg_get_status(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = smbchg_get_charge_type(chip);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = smbchg_get_health(chip);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = smbchg_usb_is_present(chip);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = smbchg_usb_get_ilim(chip);
		if (ret < 0)
			return ret;
		val->intval = ret;
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		val->intval = smbchg_usb_get_type(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		ret = smbchg_charging_get_iterm(chip);
		if (ret < 0)
			return ret;
		val->intval = ret;
		break;
	default:
		dev_err(chip->dev, "Invalid property: %d\n", psp);
		return -EINVAL;
	}

	return 0;
}

static int smbchg_set_property(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct smbchg_chip *chip = power_supply_get_drvdata(psy);
	int ret;

	dev_vdbg(chip->dev, "Setting property: %d", psp);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		ret = smbchg_usb_enable(chip, val->intval);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		if (smbchg_usb_get_type(chip) == POWER_SUPPLY_USB_TYPE_SDP)
			/* Current limit of the SDP must be respected */
			return -EPERM;
		ret = smbchg_usb_set_ilim_hc(chip, val->intval);
		break;
	default:
		dev_err(chip->dev, "Invalid property: %d\n", psp);
		return -EINVAL;
	}

	return ret;
}

static int smbchg_property_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return true;
	default:
		return false;
	}

	return 0;
}

static enum power_supply_property smbchg_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT
};

static enum power_supply_usb_type smbchg_usb_types[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP
};

static const struct power_supply_desc smbchg_usb_psy_desc = {
	.name = "qcom-smbcharger-usb",
	.type = POWER_SUPPLY_TYPE_USB,
	.usb_types = smbchg_usb_types,
	.num_usb_types = ARRAY_SIZE(smbchg_usb_types),
	.properties = smbchg_props,
	.num_properties = ARRAY_SIZE(smbchg_props),
	.get_property = smbchg_get_property,
	.set_property = smbchg_set_property,
	.property_is_writeable = smbchg_property_is_writeable
};

/**
 * @brief smbchg_set_vfloat() - Set float voltage
 *
 * @param chip       Pointer to smbchg_chip
 * @param voltage_uv Float voltage in microvolts
 * @return int 0 on success, -errno on failure
 */
static int smbchg_set_vfloat(struct smbchg_chip *chip, int voltage_uv)
{
	int delta, temp, ret, range, vfloat_mv;

	/* Set float voltage */
	vfloat_mv = voltage_uv / 1000;
	if (vfloat_mv <= smbchg_vfloat_ranges[VFLOAT_HIGH_RANGE].min)
		range = VFLOAT_MID_RANGE;
	else if (vfloat_mv <= smbchg_vfloat_ranges[VFLOAT_VERY_HIGH_RANGE].min)
		range = VFLOAT_HIGH_RANGE;
	else
		range = VFLOAT_VERY_HIGH_RANGE;

	delta = vfloat_mv - smbchg_vfloat_ranges[range].min;
	temp = smbchg_vfloat_ranges[range].value_min + delta
		/ smbchg_vfloat_ranges[range].step;
	ret = smbchg_sec_masked_write(chip, SMBCHG_CHGR_VFLOAT_CFG,
				VFLOAT_MASK, temp);
	if (ret)
		dev_err(chip->dev, "Failed to set float voltage: %pe\n", ERR_PTR(ret));

	vfloat_mv -= delta % smbchg_vfloat_ranges[range].step;

	return ret;
}

/**
 * @brief smbchg_init() - Main initialization routine
 *
 * @param chip Pointer to smbchg_chip
 * @return int 0 on success, -errno on failure
 *
 * @details Initialize charger hardware for USB charging.
 */
static int smbchg_init(struct smbchg_chip *chip)
{
	int ret;

	/* TODO: Figure out what this really means and maybe improve comment */
	/*
	 * Overriding current set by APSD (Auto Power Source Detect) will
	 * prevent AICL from rerunning at 9V for HVDCPs.
	 * Use APSD mA ratings for the initial current values.
	 */
	ret = regmap_update_bits(chip->regmap,
				chip->base + SMBCHG_USB_CHGPTH_CMD_IL,
				ICL_OVERRIDE_BIT, 0);
	if (ret) {
		dev_err(chip->dev, "Failed to disable ICL override: %pe\n", ERR_PTR(ret));
		return ret;
	}

	/* TODO: Confirm initial configuration and correct comment */
	/*
	 * Initial charger configuration: Enable USB SDP negotiation
	 * and charge inhibition
	 */
	ret = smbchg_sec_masked_write(chip, SMBCHG_CHGR_CFG2,
		CHG_EN_SRC_BIT | CHG_EN_POLARITY_BIT | P2F_CHG_TRAN
		| I_TERM_BIT | AUTO_RECHG_BIT | CHARGER_INHIBIT_BIT,
		CHG_EN_POLARITY_BIT | CHARGER_INHIBIT_BIT);
	if (ret) {
		dev_err(chip->dev, "Failed to configure charger: %pe\n", ERR_PTR(ret));
		return ret;
	}

	/* Set termination current */
	if (chip->batt_info->charge_term_current_ua != -EINVAL) {
		ret = smbchg_charging_set_iterm(chip,
					chip->batt_info->charge_term_current_ua);
		if (ret) {
			dev_err(chip->dev,"Failed to set termination current: %pe\n",
				ERR_PTR(ret));
			return ret;
		}
	}

	/* Enable charging */
	ret = smbchg_charging_enable(chip, true);
	if (ret) {
		dev_err(chip->dev, "Failed to enable charging: %pe\n", ERR_PTR(ret));
		return ret;
	}

	/*
	 * Use the sensor from the fuel gauge for reading termination current
	 * and setting the recharge threshold
	 */
	ret = smbchg_sec_masked_write(chip, SMBCHG_CHGR_CFG1,
		TERM_I_SRC_BIT | RECHG_THRESHOLD_SRC_BIT,
		TERM_SRC_FG | RECHG_THRESHOLD_SRC_BIT);
	if (ret) {
		dev_err(chip->dev,
			"Failed to configure charge termination: %pe\n", ERR_PTR(ret));
		return ret;
	}

	/*
	 * Allow controlling USB charge path suspend and SDP mode through
	 * the CMD_IL register
	 */
	ret = smbchg_sec_masked_write(chip, SMBCHG_USB_CHGPTH_CHGPTH_CFG,
		USB51_COMMAND_POL | USB51AC_CTRL, 0);
	if (ret) {
		dev_err(chip->dev,
			"Failed to configure USB charge path: %pe\n", ERR_PTR(ret));
		return ret;
	}

	/* Set float voltage */
	smbchg_set_vfloat(chip, chip->batt_info->voltage_max_design_uv);

	/* Enable recharge */
	ret = smbchg_sec_masked_write(chip, SMBCHG_CHGR_CFG, RCHG_LVL_BIT, 0);
	if (ret) {
		dev_err(chip->dev,
			"Failed to enable recharge threshold: %pe\n", ERR_PTR(ret));
		return ret;
	}

	/*
	 * Call the USB source detect handler once to set USB current limit
	 * and enable the charge path if USB is present.
	 */
	smbchg_handle_usb_source_detect(0, chip);

	/* Same with battery presence detection */
	smbchg_handle_batt_presence(0, chip);

	return ret;
}

static int smbchg_probe(struct platform_device *pdev)
{
	struct smbchg_chip *chip;
	struct regulator_config config = { };
	struct power_supply_config supply_config = {};
	int i, irq, ret;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &pdev->dev;

	chip->regmap = dev_get_regmap(chip->dev->parent, NULL);
	if (!chip->regmap) {
		dev_err(chip->dev, "Failed to get regmap\n");
		return -ENODEV;
	}

	ret = of_property_read_u32(chip->dev->of_node, "reg", &chip->base);
	if (ret) {
		dev_err(chip->dev, "Failed to get base address: %pe\n", ERR_PTR(ret));
		return ret;
	}

	spin_lock_init(&chip->sec_access_lock);
	INIT_WORK(&chip->otg_reset_work, smbchg_otg_reset_worker);

	/* Initialize OTG regulator */
	chip->otg_rdesc.id = -1;
	chip->otg_rdesc.name = "otg-vbus";
	chip->otg_rdesc.ops = &smbchg_otg_ops;
	chip->otg_rdesc.owner = THIS_MODULE;
	chip->otg_rdesc.type = REGULATOR_VOLTAGE;
	chip->otg_rdesc.of_match = "otg-vbus";

	config.dev = chip->dev;
	config.driver_data = chip;

	chip->otg_reg = devm_regulator_register(chip->dev, &chip->otg_rdesc,
						&config);
	if (IS_ERR(chip->otg_reg)) {
		dev_err(chip->dev,
			"Failed to register OTG VBUS regulator: %pe\n", chip->otg_reg);
		return PTR_ERR(chip->otg_reg);
	}

	chip->data = of_device_get_match_data(chip->dev);

	supply_config.drv_data = chip;
	supply_config.of_node = pdev->dev.of_node;
	chip->usb_psy = devm_power_supply_register(chip->dev, &smbchg_usb_psy_desc,
						&supply_config);
	if (IS_ERR(chip->usb_psy)) {
		dev_err(chip->dev,
			"Failed to register USB power supply: %pe\n", chip->usb_psy);
		return PTR_ERR(chip->usb_psy);
	}

	ret = power_supply_get_battery_info(chip->usb_psy, &chip->batt_info);
	if (ret) {
		dev_err(chip->dev, "Failed to get battery info: %pe\n", ERR_PTR(ret));
		return ret;
	}

	if (chip->batt_info->voltage_max_design_uv == -EINVAL) {
		dev_err(chip->dev, "Battery info missing maximum design voltage\n");
		return -EINVAL;
	}

	/* Initialize extcon */
	chip->edev = devm_extcon_dev_allocate(chip->dev, smbchg_extcon_cable);
	if (IS_ERR(chip->edev)) {
		dev_err(chip->dev, "Failed to allocate extcon device: %pe\n", chip->edev);
		return PTR_ERR(chip->edev);
	}

	ret = devm_extcon_dev_register(chip->dev, chip->edev);
	if (ret) {
		dev_err(chip->dev, "Failed to register extcon device: %pe\n", ERR_PTR(ret));
		return ret;
	}

	extcon_set_property_capability(chip->edev, EXTCON_USB,
					EXTCON_PROP_USB_VBUS);
	extcon_set_property_capability(chip->edev, EXTCON_USB_HOST,
					EXTCON_PROP_USB_VBUS);

	/* Initialize charger */
	ret = smbchg_init(chip);
	if (ret)
		return ret;

	/* Request interrupts */
	for (i = 0; i < ARRAY_SIZE(smbchg_irqs); ++i) {
		/* Skip unhandled interrupts for now */
		if (!smbchg_irqs[i].handler)
			continue;

		irq = of_irq_get_byname(pdev->dev.of_node, smbchg_irqs[i].name);
		if (irq < 0) {
			dev_err(chip->dev,
				"Failed to get %s IRQ: %pe\n",
				smbchg_irqs[i].name, ERR_PTR(irq));
			return irq;
		}

		ret = devm_request_threaded_irq(chip->dev, irq, NULL,
						smbchg_irqs[i].handler,
						IRQF_ONESHOT , smbchg_irqs[i].name,
						chip);
		if (ret) {
			dev_err(chip->dev,
				"failed to request %s IRQ: %pe\n",
				smbchg_irqs[i].name, ERR_PTR(irq));
			return ret;
		}
	}

	platform_set_drvdata(pdev, chip);

	return 0;
}

static int smbchg_remove(struct platform_device *pdev)
{
	struct smbchg_chip *chip = platform_get_drvdata(pdev);

	smbchg_usb_enable(chip, false);
	smbchg_charging_enable(chip, false);

	return 0;
}

static const struct of_device_id smbchg_id_table[] = {
	{ .compatible = "qcom,pmi8950-smbcharger", .data = &smbchg_pmi8994_data },
	{ .compatible = "qcom,pmi8994-smbcharger", .data = &smbchg_pmi8994_data },
	{ .compatible = "qcom,pmi8996-smbcharger", .data = &smbchg_pmi8996_data },
	{ }
};
MODULE_DEVICE_TABLE(of, smbchg_id_table);

static struct platform_driver smbchg_driver = {
	.probe = smbchg_probe,
	.remove = smbchg_remove,
	.driver = {
		.name = "qcom-smbcharger",
		.of_match_table = smbchg_id_table,
	},
};
module_platform_driver(smbchg_driver);

MODULE_AUTHOR("Yassine Oudjana <y.oudjana@protonmail.com>");
MODULE_AUTHOR("Alejandro Tafalla <atafalla@dnyon.com>");
MODULE_DESCRIPTION("Qualcomm Switch-Mode Battery Charger");
MODULE_LICENSE("GPL");
