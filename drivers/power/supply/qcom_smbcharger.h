/* SPDX-License-Identifier: GPL-2.0-only */

/* Registers */
/* CHGR */
#define SMBCHG_CHGR_FV_STS			0x00c
#define SMBCHG_CHGR_STS				0x00e
#define SMBCHG_CHGR_RT_STS			0x010
#define SMBCHG_CHGR_FCC_CFG			0x0f2
#define SMBCHG_CHGR_FCC_CMP_CFG			0x0f3
#define SMBCHG_CHGR_VFLOAT_CFG			0x0f4
#define SMBCHG_CHGR_FV_CMP			0x0f5
#define SMBCHG_CHGR_AFVC_CFG			0x0f6
#define SMBCHG_CHGR_CHG_INHIB_CFG		0x0f7
#define SMBCHG_CHGR_TCC_CFG			0x0f9
#define SMBCHG_CHGR_CCMP_CFG			0x0fa
#define SMBCHG_CHGR_CFG1			0x0fb
#define SMBCHG_CHGR_CFG2			0x0fc
#define SMBCHG_CHGR_SFT_CFG			0x0fd
#define SMBCHG_CHGR_CFG				0x0ff

/* OTG */
#define SMBCHG_OTG_RT_STS			0x110
#define SMBCHG_OTG_OTG_CFG			0x1f1
#define SMBCHG_OTG_TRIM6			0x1f6
#define SMBCHG_OTG_LOW_PWR_OPTIONS		0x1ff

/* BAT-IF */
#define SMBCHG_BAT_IF_BAT_PRES_STS		0x208
#define SMBCHG_BAT_IF_RT_STS			0x210
#define SMBCHG_BAT_IF_CMD_CHG			0x242
#define SMBCHG_BAT_IF_CMD_CHG_LED		0x243
#define SMBCHG_BAT_IF_BM_CFG			0x2f3
#define SMBCHG_BAT_IF_BAT_IF_TRIM7		0x2f7
#define SMBCHG_BAT_IF_BB_CLMP_SEL		0x2f8
#define SMBCHG_BAT_IF_ZIN_ICL_PT		0x2fc
#define SMBCHG_BAT_IF_ZIN_ICL_LV		0x2fd
#define SMBCHG_BAT_IF_ZIN_ICL_HV		0x2fe

/* USB-CHGPTH */
#define SMBCHG_USB_CHGPTH_ICL_STS_1		0x307
#define SMBCHG_USB_CHGPTH_PWR_PATH		0x308
#define SMBCHG_USB_CHGPTH_ICL_STS_2		0x309
#define SMBCHG_USB_CHGPTH_RID_STS		0x30b
#define SMBCHG_USB_CHGPTH_USBIN_HVDCP_STS	0x30c
#define SMBCHG_USB_CHGPTH_INPUT_STS		0x30d
#define SMBCHG_USB_CHGPTH_USBID_MSB		0x30e
#define SMBCHG_USB_CHGPTH_RT_STS		0x310
#define SMBCHG_USB_CHGPTH_CMD_IL		0x340
#define SMBCHG_USB_CHGPTH_CMD_APSD		0x341
#define SMBCHG_USB_CHGPTH_CMD_HVDCP_1		0x342
#define SMBCHG_USB_CHGPTH_CMD_HVDCP_2		0x343
#define SMBCHG_USB_CHGPTH_USBIN_CHGR_CFG	0x3f1
#define SMBCHG_USB_CHGPTH_IL_CFG		0x3f2
#define SMBCHG_USB_CHGPTH_AICL_CFG		0x3f3
#define SMBCHG_USB_CHGPTH_CHGPTH_CFG		0x3f4
#define SMBCHG_USB_CHGPTH_APSD_CFG		0x3f5
#define SMBCHG_USB_CHGPTH_TR_RID		0x3fa
#define SMBCHG_USB_CHGPTH_ICL_BUF_CONFIG	0x3fc
#define SMBCHG_USB_CHGPTH_TR_8OR32B		0x3fe

/* DC-CHGPTH */
#define SMBCHG_DC_CHGPTH_RT_STS			0x410
#define SMBCHG_DC_CHGPTH_IL_CFG			0x4f2
#define SMBCHG_DC_CHGPTH_AICL_CFG		0x4f3
#define SMBCHG_DC_CHGPTH_AICL_WL_SEL_CFG	0x4f5

/* MISC */
#define SMBCHG_MISC_REVISION1			0x600
#define SMBCHG_MISC_IDEV_STS			0x608
#define SMBCHG_MISC_RT_STS			0x610
#define SMBCHG_MISC_TEST			0x6e2
#define SMBCHG_MISC_NTC_VOUT_CFG		0x6f3
#define SMBCHG_MISC_TRIM_OPT_15_8		0x6f5
#define SMBCHG_MISC_TRIM_OPT_7_0		0x6f6
#define SMBCHG_MISC_TRIM_14			0x6fe

/* Bits */
/* CHGR_STS bits */
#define CHG_TYPE_MASK			GENMASK(2, 1)
#define CHG_HOLD_OFF_BIT		BIT(3)

/* CHGR_RT_STS bits */
#define CHG_INHIBIT_BIT			BIT(1)
#define CHG_COMP_SFT_BIT		BIT(3)
#define BAT_TCC_REACHED_BIT		BIT(7)

/* CHGR_VFLOAT_CFG bits */
#define VFLOAT_MASK			GENMASK(5, 0)

/* CHGR_TCC_CFG bits */
#define CHG_ITERM_MASK			GENMASK(2, 0)

/* CHGR_CFG1 bits */
#define RECHG_THRESHOLD_SRC_BIT		BIT(1)
#define TERM_I_SRC_BIT			BIT(2)

/* CHGR_CFG2 bits */
#define CHARGER_INHIBIT_BIT		BIT(0)
#define AUTO_RECHG_BIT			BIT(2)
#define I_TERM_BIT			BIT(3)
#define P2F_CHG_TRAN			BIT(5)
#define CHG_EN_POLARITY_BIT		BIT(6)
#define CHG_EN_SRC_BIT			BIT(7)

/* CHGR_CFG bits */
#define RCHG_LVL_BIT			BIT(0)

/* BAT_IF_CMD_CHG bits */
#define OTG_EN_BIT			BIT(0)
#define CHG_EN_BIT			BIT(1)

/* BAT_IF_RT_STS bits */
#define HOT_BAT_HARD_BIT		BIT(0)
#define HOT_BAT_SOFT_BIT		BIT(1)
#define COLD_BAT_HARD_BIT		BIT(2)
#define COLD_BAT_SOFT_BIT		BIT(3)
#define BAT_OV_BIT			BIT(4)
#define BAT_LOW_BIT			BIT(5)
#define BAT_MISSING_BIT			BIT(6)
#define BAT_TERM_MISSING_BIT		BIT(7)

/* USB_CHGPTH_CHGPTH_CFG bits */
#define USB51AC_CTRL			BIT(1)
#define USB51_COMMAND_POL		BIT(2)
#define USB_2_3_SEL_BIT			BIT(7)

/* USB_CHGPTH_RT_STS bits */
#define USBIN_OV_BIT			BIT(1)
#define USBIN_SRC_DET_BIT		BIT(2)

/* USB_CHGPTH_RID_STS bits */
#define RID_MASK			GENMASK(3, 0)

/* USB_CHGPTH_CMD_IL bits */
#define USBIN_MODE_HC_BIT		BIT(0)
#define USB51_MODE_BIT			BIT(1)
#define ICL_OVERRIDE_BIT		BIT(2)
#define USBIN_SUSPEND_BIT		BIT(4)

/* SMBCHG_USB_CHGPTH_IL_CFG bits */
#define USBIN_INPUT_MASK		GENMASK(4, 0)

/* MISC_IDEV_STS bits */
#define FMB_STS_MASK			GENMASK(3, 0)

/* Values */
/* CHGR_STS values */
#define CHG_TYPE_SHIFT			1
#define BATT_NOT_CHG_VAL		0x0
#define BATT_PRE_CHG_VAL		0x1
#define BATT_FAST_CHG_VAL		0x2
#define BATT_TAPER_CHG_VAL		0x3

/* CHGR_CFG1 values */
#define TERM_SRC_FG			BIT(2)

/* USB_CHGPTH_INPUT_STS values */
#define USBIN_9V			BIT(5)
#define USBIN_UNREG			BIT(4)
#define USBIN_LV			BIT(3)

/* USB_CHGPTH_USBID_MSB values */
#define USBID_GND_THRESHOLD 		0x495

/* USB_CHGPTH_CHGPTH_CFG values */
#define USB_2				0
#define USB_3				BIT(7)

/* USB_CHGPTH_CMD_IL values */
#define USBIN_LIMITED_MODE		0
#define	USBIN_HC_MODE			USBIN_MODE_HC_BIT
#define USB51_100MA			0
#define USB51_500MA			USB51_MODE_BIT

/* MISC_IDEV_STS values */
#define USB_TYPE_SDP_BIT		BIT(4)
#define USB_TYPE_OTHER_BIT		BIT(5)
#define USB_TYPE_DCP_BIT		BIT(6)
#define USB_TYPE_CDP_BIT		BIT(7)

/* Constant parameters */
#define SUSPEND_CURRENT_MA		2
#define DEFAULT_SDP_ILIM		500000
#define DEFAULT_DCP_ILIM		1000000
#define DEFAULT_CDP_ILIM		1500000
#define NUM_OTG_RESET_RETRIES		5
#define OTG_RESET_DELAY_MS		20

/* Secure access values */
#define SEC_ACCESS_OFFSET		0x00d0
#define SEC_ACCESS_VALUE		0x00a5
#define PERIPHERAL_MASK			0xff00

/*
 * These values can be indexed using a bitmask:
 * 0: Limited/full current
 * 1: USB 2.0/3.0
 */
static const int smbchg_lc_ilim_options[] = {
	[0b00] = 100000,
	[0b01] = 150000,
	[0b10] = 500000,
	[0b11] = 900000
};
#define LC_ILIM_USB3_BIT		BIT(1)
#define LC_ILIM_FULL_CURRENT_BIT	BIT(0)
#define smbchg_lc_ilim(usb_3, full_current) smbchg_lc_ilim_options[usb_3 << 1 | full_current]

struct smbchg_chip {
	unsigned int base;
	struct device *dev;
	struct regmap *regmap;

	struct power_supply_battery_info *batt_info;
	struct power_supply *usb_psy;

	struct regulator_desc otg_rdesc;
	struct regulator_dev *otg_reg;
	int otg_resets;

	struct extcon_dev *edev;

	spinlock_t sec_access_lock;
	struct work_struct otg_reset_work;

	const struct smbchg_data *data;
};

struct smbchg_irq {
	const char *name;
	irq_handler_t handler;
};

enum vfloat_range {
	VFLOAT_VERY_HIGH_RANGE = 0,
	VFLOAT_HIGH_RANGE,
	VFLOAT_MID_RANGE,
};

struct smbchg_vfloat_range {
	int min, step, value_min;
} smbchg_vfloat_ranges[] = {
	[VFLOAT_VERY_HIGH_RANGE] = {
		.min = 4360,
		.step = 0,
		.value_min = 0x2c
	},
	[VFLOAT_HIGH_RANGE] = {
		.min = 4340,
		.step = 10,
		.value_min = 0x2a
	},
	[VFLOAT_MID_RANGE] = {
		.min = 3600,
		.step = 20,
		.value_min = 0x05
	}
};

struct smbchg_data {
	const int *iterm_table;
	unsigned int iterm_table_len;
	const int *ilim_usb_table;
	unsigned int ilim_usb_table_len;
};

static const int smbchg_iterm_table_pmi8994[] = {
	50000,
	100000,
	150000,
	200000,
	250000,
	300000,
	500000,
	600000
};

static const int smbchg_ilim_usb_table_pmi8994[] = {
	300000,
	400000,
	450000,
	475000,
	500000,
	550000,
	600000,
	650000,
	700000,
	900000,
	950000,
	1000000,
	1100000,
	1200000,
	1400000,
	1450000,
	1500000,
	1600000,
	1800000,
	1850000,
	1880000,
	1910000,
	1930000,
	1950000,
	1970000,
	2000000,
	2050000,
	2100000,
	2300000,
	2400000,
	2500000,
	3000000
};

static const int smbchg_iterm_table_pmi8996[] = {
	50000,
	100000,
	150000,
	200000,
	250000,
	300000,
	400000,
	500000
};

static const int smbchg_ilim_usb_table_pmi8996[] = {
	300000,
	400000,
	500000,
	600000,
	700000,
	800000,
	900000,
	1000000,
	1100000,
	1200000,
	1300000,
	1400000,
	1450000,
	1500000,
	1550000,
	1600000,
	1700000,
	1800000,
	1900000,
	1950000,
	2000000,
	2050000,
	2100000,
	2200000,
	2300000,
	2400000,
	2500000,
	2600000,
	2700000,
	2800000,
	2900000,
	3000000
};

static const struct smbchg_data smbchg_pmi8994_data = {
	.iterm_table = smbchg_iterm_table_pmi8994,
	.iterm_table_len = ARRAY_SIZE(smbchg_iterm_table_pmi8994),
	.ilim_usb_table = smbchg_ilim_usb_table_pmi8994,
	.ilim_usb_table_len = ARRAY_SIZE(smbchg_ilim_usb_table_pmi8994)
};

static const struct smbchg_data smbchg_pmi8996_data = {
	.iterm_table = smbchg_iterm_table_pmi8996,
	.iterm_table_len = ARRAY_SIZE(smbchg_iterm_table_pmi8996),
	.ilim_usb_table = smbchg_ilim_usb_table_pmi8996,
	.ilim_usb_table_len = ARRAY_SIZE(smbchg_ilim_usb_table_pmi8996)
};

/* TODO: Replace with find_closest or add find_smaller to util_macros */
static inline int find_smaller_in_array(const int *table, int len,
				int val)
{
	int i;

	for (i = len - 1; i >= 0; i--) {
		if (val >= table[i])
			break;
	}

	return i;
}
