// SPDX-License-Identifier: GPL-2.0
/*
 * Sony IMX318 CMOS Image Sensor Driver
 *
 * Copyright (c) 2021, Yassine Oudjana <y.oudjana@protonmail.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <media/media-entity.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define IMX318_EXTCLK_RATE		24000000
#define IMX318_DEFAULT_BPP		10

#define IMX318_REG_ID			0x0016
#define IMX318_REG_MODE_SELECT		0x0100
#define IMX318_REG_HOLD			0x0104
#define IMX318_REG_EXPOSURE		0x0202
#define IMX318_REG_ANALOG_GAIN		0x0204

#define IMX318_MODE_STREAMING		0x01
#define IMX318_MODE_STANDBY		0x01

#define IMX318_ID			0x318

static struct {
	const char *name;
	int load;
} imx318_supply_data[] = {
	{ "vdig", 105000 },
	{ "vio", 0 },
	{ "vmipi", 105000 },
	{ "vana", 80000 }
};

#define IMX318_NUM_SUPPLIES ARRAY_SIZE(imx318_supply_data)

struct imx318_reg {
	u16 addr;
	u8 val;
};

struct imx318_reg_list {
	const struct imx318_reg *regs;
	size_t len;
};

struct imx318_mode {
	u32 width;
	u32 height;
	u32 code;
	u32 framerate;
	u32 link_freq_idx;

	const struct reg_sequence *regs;
	size_t num_regs;
};

struct imx318 {
	struct device *dev;
	struct regmap *regmap;

	struct regulator_bulk_data supplies[IMX318_NUM_SUPPLIES];
	struct gpio_desc *reset_gpio;
	struct clk *extclk;
	u32 extclk_rate;
	u8 nlanes;

	struct v4l2_subdev sd;
	struct media_pad pad;
	const struct imx318_mode *current_mode;
	struct v4l2_fract frame_interval;
	bool streaming;

	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *analog_gain;

	struct mutex lock;
};

static const struct regmap_config imx318_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
};

static const struct reg_sequence imx318_common_init_regs[] = {
	/* EXTCLK Settings */
	{0x0136, 0x18},
	{0x0137, 0x00},

	/* Global Settings */
	{0x3067, 0x00},
	{0x4600, 0x1b},
	{0x46c2, 0x00},
	{0x4877, 0x11},
	{0x487b, 0x4d},
	{0x487f, 0x3b},
	{0x4883, 0xb4},
	{0x4c6f, 0x5e},
	{0x5113, 0xf4},
	{0x5115, 0xf6},
	{0x5125, 0xf4},
	{0x5127, 0xf8},
	{0x51cf, 0xf4},
	{0x51e9, 0xf4},
	{0x5483, 0x7a},
	{0x5485, 0x7c},
	{0x5495, 0x7a},
	{0x5497, 0x7f},
	{0x5515, 0xc3},
	{0x5517, 0xc7},
	{0x552b, 0x7a},
	{0x5535, 0x7a},
	{0x5a35, 0x1b},
	{0x5c13, 0x00},
	{0x5d89, 0xb1},
	{0x5d8b, 0x2c},
	{0x5d8d, 0x61},
	{0x5d8f, 0xe1},
	{0x5d91, 0x4d},
	{0x5d93, 0xb4},
	{0x5d95, 0x41},
	{0x5d97, 0x96},
	{0x5d99, 0x37},
	{0x5d9b, 0x81},
	{0x5d9d, 0x31},
	{0x5d9f, 0x71},
	{0x5da1, 0x2b},
	{0x5da3, 0x64},
	{0x5da5, 0x27},
	{0x5da7, 0x5a},
	{0x6009, 0x03},
	{0x613a, 0x05},
	{0x613c, 0x23},
	{0x6142, 0x02},
	{0x6143, 0x62},
	{0x6144, 0x89},
	{0x6145, 0x0a},
	{0x6146, 0x24},
	{0x6147, 0x28},
	{0x6148, 0x90},
	{0x6149, 0xa2},
	{0x614a, 0x40},
	{0x614b, 0x8a},
	{0x614c, 0x01},
	{0x614d, 0x12},
	{0x614e, 0x2c},
	{0x614f, 0x98},
	{0x6150, 0xa2},
	{0x615d, 0x37},
	{0x615e, 0xe6},
	{0x615f, 0x4b},
	{0x616c, 0x41},
	{0x616d, 0x05},
	{0x616e, 0x48},
	{0x616f, 0xc5},
	{0x6174, 0xb9},
	{0x6175, 0x42},
	{0x6176, 0x44},
	{0x6177, 0xc3},
	{0x6178, 0x81},
	{0x6179, 0x78},
	{0x6182, 0x15},
	{0x6a5f, 0x03},
	{0x9302, 0xff},

	/* IQ Quality */
	{0x7468, 0x03},
	{0x7b65, 0x8c},
	{0x7b67, 0x4b},
	{0x7b69, 0x8c},
	{0x7b6b, 0x4b},
	{0x7b6d, 0x8c},
	{0x7b6f, 0x4b},
	{0x7b70, 0x40},
	{0x9805, 0x04},
	{0x9822, 0x03},
	{0x9843, 0x01},
	{0x9902, 0x00},
	{0x9903, 0x01},
	{0x9904, 0x00},
	{0x9905, 0x01},
	{0x990e, 0x00},
	{0x9944, 0x3c},
	{0x9947, 0x3c},
	{0x994a, 0x8c},
	{0x994b, 0x1b},
	{0x994c, 0x0a},
	{0x994d, 0x8c},
	{0x994e, 0x1b},
	{0x994f, 0x0a},
	{0x9950, 0x8c},
	{0x9951, 0x50},
	{0x9952, 0x1b},
	{0x9953, 0x8c},
	{0x9954, 0x50},
	{0x9955, 0x1b},
	{0x996e, 0x50},
	{0x996f, 0x3c},
	{0x9970, 0x1b},
	{0x9a08, 0x04},
	{0x9a09, 0x05},
	{0x9a0a, 0x04},
	{0x9a0b, 0x05},
	{0x9a0c, 0x04},
	{0x9a0d, 0x05},
	{0x9a0e, 0x06},
	{0x9a0f, 0x07},
	{0x9a10, 0x06},
	{0x9a11, 0x07},
	{0x9a12, 0x07},
	{0x9a13, 0x07},
	{0x9a14, 0x07},
	{0x9a2b, 0x0f},
	{0x9a2c, 0x0f},
	{0x9a2d, 0x0f},
	{0x9a2e, 0x0f},
	{0x9a2f, 0x0f},
	{0x9a36, 0x02},
	{0x9a37, 0x02},
	{0x9a3f, 0x0e},
	{0x9a40, 0x0e},
	{0x9a41, 0x0e},
	{0x9a42, 0x0e},
	{0x9a43, 0x0f},
	{0x9a44, 0x0f},
	{0x9a4c, 0x0f},
	{0x9a4d, 0x0f},
	{0x9a4e, 0x0f},
	{0x9a4f, 0x0f},
	{0x9a50, 0x0f},
	{0x9a54, 0x0f},
	{0x9a55, 0x0f},
	{0x9a5c, 0x03},
	{0x9a5e, 0x03},
	{0x9a64, 0x0e},
	{0x9a65, 0x0e},
	{0x9a66, 0x0e},
	{0x9a67, 0x0e},
	{0x9a6f, 0x0f},
	{0x9a70, 0x0f},
	{0x9a71, 0x0f},
	{0x9a72, 0x0f},
	{0x9a73, 0x0f},
	{0x9aac, 0x06},
	{0x9aad, 0x06},
	{0x9aae, 0x06},
	{0x9aaf, 0x06},
	{0x9ab0, 0x06},
	{0x9ab1, 0x06},
	{0x9ab2, 0x06},
	{0x9ab3, 0x07},
	{0x9ab4, 0x07},
	{0x9ab5, 0x07},
	{0x9ab6, 0x07},
	{0x9ab7, 0x07},
	{0x9ab8, 0x06},
	{0x9ab9, 0x06},
	{0x9aba, 0x06},
	{0x9abb, 0x06},
	{0x9abc, 0x06},
	{0x9abd, 0x07},
	{0x9abe, 0x07},
	{0x9abf, 0x07},
	{0x9ac0, 0x07},
	{0x9ac1, 0x07},
	{0xa000, 0x00},
	{0xa001, 0x00},
	{0xa002, 0x00},
	{0xa003, 0x00},
	{0xa004, 0x00},
	{0xa005, 0x00},
	{0xa017, 0x10},
	{0xa019, 0x10},
	{0xa01b, 0x10},
	{0xa01d, 0x35},
	{0xa023, 0x31},
	{0xa02f, 0x50},
	{0xa041, 0x6b},
	{0xa047, 0x40},
	{0xa068, 0x00},
	{0xa069, 0x00},
	{0xa06a, 0x00},
	{0xa06b, 0x00},
	{0xa06c, 0x00},
	{0xa06d, 0x00},
	{0xa06e, 0x00},
	{0xa06f, 0x00},
	{0xa070, 0x00},
	{0xa075, 0x50},
	{0xa077, 0x50},
	{0xa079, 0x50},
	{0xa07b, 0x40},
	{0xa07d, 0x40},
	{0xa07f, 0x40},
	{0xa0ad, 0x18},
	{0xa0ae, 0x18},
	{0xa0af, 0x18},
	{0xa0b6, 0x00},
	{0xa0b7, 0x00},
	{0xa0b8, 0x00},
	{0xa0b9, 0x00},
	{0xa0ba, 0x00},
	{0xa0bb, 0x00},
	{0xa0bd, 0x2d},
	{0xa0c3, 0x2d},
	{0xa0c9, 0x40},
	{0xa0d5, 0x2f},
	{0xa100, 0x00},
	{0xa101, 0x00},
	{0xa102, 0x00},
	{0xa103, 0x00},
	{0xa104, 0x00},
	{0xa105, 0x00},
	{0xa117, 0x10},
	{0xa119, 0x10},
	{0xa11b, 0x10},
	{0xa11d, 0x35},
	{0xa123, 0x31},
	{0xa12f, 0x50},
	{0xa13b, 0x35},
	{0xa13d, 0x35},
	{0xa13f, 0x35},
	{0xa141, 0x6b},
	{0xa147, 0x5a},
	{0xa168, 0x3f},
	{0xa169, 0x3f},
	{0xa16a, 0x3f},
	{0xa16b, 0x00},
	{0xa16c, 0x00},
	{0xa16d, 0x00},
	{0xa16e, 0x3f},
	{0xa16f, 0x3f},
	{0xa170, 0x3f},
	{0xa1b6, 0x00},
	{0xa1b7, 0x00},
	{0xa1b8, 0x00},
	{0xa1b9, 0x00},
	{0xa1ba, 0x00},
	{0xa1bb, 0x00},
	{0xa1bd, 0x42},
	{0xa1c3, 0x42},
	{0xa1c9, 0x5a},
	{0xa1d5, 0x2f},
	{0xa200, 0x00},
	{0xa201, 0x00},
	{0xa202, 0x00},
	{0xa203, 0x00},
	{IMX318_REG_ANALOG_GAIN, 0x00},
	{IMX318_REG_ANALOG_GAIN + 1, 0x00},
	{0xa217, 0x10},
	{0xa219, 0x10},
	{0xa21b, 0x10},
	{0xa21d, 0x35},
	{0xa223, 0x31},
	{0xa22f, 0x50},
	{0xa241, 0x6b},
	{0xa247, 0x40},
	{0xa268, 0x00},
	{0xa269, 0x00},
	{0xa26a, 0x00},
	{0xa26b, 0x00},
	{0xa26c, 0x00},
	{0xa26d, 0x00},
	{0xa26e, 0x00},
	{0xa26f, 0x00},
	{0xa270, 0x00},
	{0xa271, 0x00},
	{0xa272, 0x00},
	{0xa273, 0x00},
	{0xa275, 0x50},
	{0xa277, 0x50},
	{0xa279, 0x50},
	{0xa27b, 0x40},
	{0xa27d, 0x40},
	{0xa27f, 0x40},
	{0xa2b6, 0x00},
	{0xa2b7, 0x00},
	{0xa2b8, 0x00},
	{0xa2b9, 0x00},
	{0xa2ba, 0x00},
	{0xa2bb, 0x00},
	{0xa2bd, 0x2d},
	{0xa2d5, 0x2f},
	{0xa300, 0x00},
	{0xa301, 0x00},
	{0xa302, 0x00},
	{0xa303, 0x00},
	{0xa304, 0x00},
	{0xa305, 0x00},
	{0xa317, 0x10},
	{0xa319, 0x10},
	{0xa31b, 0x10},
	{0xa31d, 0x35},
	{0xa323, 0x31},
	{0xa32f, 0x50},
	{0xa341, 0x6b},
	{0xa347, 0x5a},
	{0xa368, 0x0f},
	{0xa369, 0x0f},
	{0xa36a, 0x0f},
	{0xa36b, 0x30},
	{0xa36c, 0x00},
	{0xa36d, 0x00},
	{0xa36e, 0x3f},
	{0xa36f, 0x3f},
	{0xa370, 0x2f},
	{0xa371, 0x30},
	{0xa372, 0x00},
	{0xa373, 0x00},
	{0xa3b6, 0x00},
	{0xa3b7, 0x00},
	{0xa3b8, 0x00},
	{0xa3b9, 0x00},
	{0xa3ba, 0x00},
	{0xa3bb, 0x00},
	{0xa3bd, 0x42},
	{0xa3d5, 0x2f},
	{0xa400, 0x00},
	{0xa401, 0x00},
	{0xa402, 0x00},
	{0xa403, 0x00},
	{0xa404, 0x00},
	{0xa405, 0x00},
	{0xa407, 0x10},
	{0xa409, 0x10},
	{0xa40b, 0x10},
	{0xa40d, 0x35},
	{0xa413, 0x31},
	{0xa41f, 0x50},
	{0xa431, 0x6b},
	{0xa437, 0x40},
	{0xa454, 0x00},
	{0xa455, 0x00},
	{0xa456, 0x00},
	{0xa457, 0x00},
	{0xa458, 0x00},
	{0xa459, 0x00},
	{0xa45a, 0x00},
	{0xa45b, 0x00},
	{0xa45c, 0x00},
	{0xa45d, 0x00},
	{0xa45e, 0x00},
	{0xa45f, 0x00},
	{0xa48d, 0x18},
	{0xa48e, 0x18},
	{0xa48f, 0x18},
	{0xa496, 0x00},
	{0xa497, 0x00},
	{0xa498, 0x00},
	{0xa499, 0x00},
	{0xa49a, 0x00},
	{0xa49b, 0x00},
	{0xa49d, 0x2d},
	{0xa4a3, 0x2d},
	{0xa4a9, 0x40},
	{0xa4b5, 0x2f},
	{0xa500, 0x00},
	{0xa501, 0x00},
	{0xa502, 0x00},
	{0xa503, 0x00},
	{0xa504, 0x00},
	{0xa505, 0x00},
	{0xa507, 0x10},
	{0xa509, 0x10},
	{0xa50b, 0x10},
	{0xa50d, 0x35},
	{0xa513, 0x31},
	{0xa51f, 0x50},
	{0xa52b, 0x35},
	{0xa52d, 0x35},
	{0xa52f, 0x35},
	{0xa531, 0x6b},
	{0xa537, 0x5a},
	{0xa554, 0x3f},
	{0xa555, 0x3f},
	{0xa556, 0x3f},
	{0xa557, 0x00},
	{0xa558, 0x00},
	{0xa559, 0x00},
	{0xa55a, 0x3f},
	{0xa55b, 0x3f},
	{0xa55c, 0x3f},
	{0xa55d, 0x00},
	{0xa55e, 0x00},
	{0xa55f, 0x00},
	{0xa596, 0x00},
	{0xa597, 0x00},
	{0xa598, 0x00},
	{0xa599, 0x00},
	{0xa59a, 0x00},
	{0xa59b, 0x00},
	{0xa59d, 0x42},
	{0xa5a3, 0x42},
	{0xa5a9, 0x5a},
	{0xa5b5, 0x2f},
	{0xa653, 0x84},
	{0xa65f, 0x00},
	{0xa6b5, 0xff},
	{0xa6c1, 0x00},
	{0xa74f, 0xa0},
	{0xa753, 0xfe},
	{0xa75d, 0x00},
	{0xa75f, 0x00},
	{0xa7b5, 0xff},
	{0xa7c1, 0x00},
	{0xca00, 0x01},
	{0xca12, 0x2c},
	{0xca13, 0x2c},
	{0xca14, 0x1c},
	{0xca15, 0x1c},
	{0xca16, 0x06},
	{0xca17, 0x06},
	{0xca1a, 0x0c},
	{0xca1b, 0x0c},
	{0xca1c, 0x06},
	{0xca1d, 0x06},
	{0xca1e, 0x00},
	{0xca1f, 0x00},
	{0xca21, 0x04},
	{0xca23, 0x04},
	{0xca2c, 0x00},
	{0xca2d, 0x10},
	{0xca2f, 0x10},
	{0xca30, 0x00},
	{0xca32, 0x10},
	{0xca35, 0x28},
	{0xca37, 0x80},
	{0xca39, 0x10},
	{0xca3b, 0x10},
	{0xca3c, 0x20},
	{0xca3d, 0x20},
	{0xca43, 0x02},
	{0xca45, 0x02},
	{0xca46, 0x01},
	{0xca47, 0x99},
	{0xca48, 0x01},
	{0xca49, 0x99},
	{0xca6c, 0x20},
	{0xca6d, 0x20},
	{0xca6e, 0x20},
	{0xca6f, 0x20},
	{0xca72, 0x00},
	{0xca73, 0x00},
	{0xca75, 0x04},
	{0xca77, 0x04},
	{0xca80, 0x00},
	{0xca81, 0x30},
	{0xca84, 0x00},
	{0xca86, 0x10},
	{0xca89, 0x28},
	{0xca8b, 0x80},
	{0xca8d, 0x10},
	{0xca8f, 0x10},
	{0xca90, 0x20},
	{0xca91, 0x20},
	{0xca97, 0x02},
	{0xca99, 0x02},
	{0xca9a, 0x01},
	{0xca9b, 0x99},
	{0xca9c, 0x01},
	{0xca9d, 0x99},
	{0xcaab, 0x28},
	{0xcaad, 0x39},
	{0xcaae, 0x53},
	{0xcaaf, 0x67},
	{0xcab0, 0x45},
	{0xcab1, 0x47},
	{0xcab2, 0x01},
	{0xcab3, 0x6b},
	{0xcab4, 0x06},
	{0xcab5, 0x8c},
	{0xcab7, 0xa6},
	{0xcab8, 0x06},
	{0xcab9, 0x0a},
	{0xcaba, 0x08},
	{0xcabb, 0x05},
	{0xcabc, 0x33},
	{0xcabd, 0x73},
	{0xcabe, 0x02},
	{0xcabf, 0x17},
	{0xcac0, 0x28},
	{0xcac1, 0xc5},
	{0xcac2, 0x08},
	{0xcac4, 0x25},
	{0xcac5, 0x0a},
	{0xcac6, 0x00},
	{0xcac8, 0x15},
	{0xcac9, 0x78},
	{0xcaca, 0x1e},
	{0xcacf, 0x60},
	{0xcad1, 0x28},
	{0xd01a, 0x00},
	{0xd080, 0x0a},
	{0xd081, 0x10}
};

static const struct reg_sequence imx318_mode_5488x4112_30_regs[] = {
	{0x0111, 0x02},
	{0x0112, 0x0a},
	{0x0113, 0x0a},
	{0x0114, 0x03},
	{0x0342, 0x18},
	{0x0343, 0x50},
	{0x0340, 0x10},
	{0x0341, 0xb8},
	{0x0344, 0x00},
	{0x0345, 0x00},
	{0x0346, 0x00},
	{0x0347, 0x00},
	{0x0348, 0x15},
	{0x0349, 0x6f},
	{0x034a, 0x10},
	{0x034b, 0x0f},
	{0x31a2, 0x00},
	{0x0220, 0x00},
	{0x0221, 0x11},
	{0x0222, 0x01},
	{0x0381, 0x01},
	{0x0383, 0x01},
	{0x0385, 0x01},
	{0x0387, 0x01},
	{0x0900, 0x00},
	{0x0901, 0x11},
	{0x0902, 0x00},
	{0x3010, 0x65},
	{0x3011, 0x11},
	{0x3194, 0x01},
	{0x31a0, 0x00},
	{0x31a1, 0x00},
	{0x0401, 0x00},
	{0x0404, 0x00},
	{0x0405, 0x10},
	{0x0408, 0x00},
	{0x0409, 0x00},
	{0x040a, 0x00},
	{0x040b, 0x00},
	{0x040c, 0x15},
	{0x040d, 0x70},
	{0x040e, 0x10},
	{0x040f, 0x10},
	{0x034c, 0x15},
	{0x034d, 0x70},
	{0x034e, 0x10},
	{0x034f, 0x10},
	{0x0301, 0x05},
	{0x0303, 0x02},
	{0x0305, 0x04},
	{0x0306, 0x01},
	{0x0307, 0x4d},
	{0x0309, 0x0a},
	{0x030b, 0x01},
	{0x030d, 0x04},
	{0x030e, 0x01},
	{0x030f, 0x40},
	{0x0820, 0x1e},
	{0x0821, 0x00},
	{0x0822, 0x00},
	{0x0823, 0x00},
	{0x3031, 0x00},
	{0x3033, 0x00},
	{0x3035, 0x00},
	{0x3037, 0x00},
	{0x3039, 0x00},
	{0x303b, 0x00},
	{0x306c, 0x00},
	{0x306e, 0x0d},
	{0x306f, 0x56},
	{0x6636, 0x00},
	{0x6637, 0x14},
	{0x3066, 0x00},
	{0x7b63, 0x00},
	{IMX318_REG_EXPOSURE, 0x10},
	{IMX318_REG_EXPOSURE + 1, 0xa4},
	{0x0224, 0x01},
	{0x0225, 0xf4},
	{IMX318_REG_ANALOG_GAIN, 0x00},
	{IMX318_REG_ANALOG_GAIN + 1, 0x00},
	{0x020e, 0x01},
	{0x020f, 0x00},
	{0x0210, 0x01},
	{0x0211, 0x00},
	{0x0212, 0x01},
	{0x0213, 0x00},
	{0x0214, 0x01},
	{0x0215, 0x00},
	{0x0216, 0x00},
	{0x0217, 0x00},
	{0x0218, 0x01},
	{0x0219, 0x00},
	{0x56fa, 0x00},
	{0x56fb, 0x50},
	{0x56fe, 0x00},
	{0x56ff, 0x50},
	{0x9323, 0x10},
};

static const struct reg_sequence imx318_mode_3840x2160_30_regs[] = {
	{0x0111, 0x02},
	{0x0112, 0x0a},
	{0x0113, 0x0a},
	{0x0114, 0x03},
	{0x0342, 0x1c},
	{0x0343, 0xb8},
	{0x0340, 0x0c},
	{0x0341, 0x40},
	{0x0344, 0x00},
	{0x0345, 0x00},
	{0x0346, 0x02},
	{0x0347, 0x00},
	{0x0348, 0x15},
	{0x0349, 0x6f},
	{0x034a, 0x0e},
	{0x034b, 0x0f},
	{0x31a2, 0x00},
	{0x0220, 0x00},
	{0x0221, 0x11},
	{0x0222, 0x01},
	{0x0381, 0x01},
	{0x0383, 0x01},
	{0x0385, 0x01},
	{0x0387, 0x01},
	{0x0900, 0x00},
	{0x0901, 0x11},
	{0x0902, 0x00},
	{0x3010, 0x65},
	{0x3011, 0x11},
	{0x301c, 0x00},
	{0x3045, 0x01},
	{0x3194, 0x00},
	{0x31a0, 0x00},
	{0x31a1, 0x00},
	{0xd5ec, 0x3a},
	{0xd5ed, 0x00},
	{0x0401, 0x02},
	{0x0404, 0x00},
	{0x0405, 0x16},
	{0x0408, 0x00},
	{0x0409, 0x68},
	{0x040a, 0x00},
	{0x040b, 0x3a},
	{0x040c, 0x14},
	{0x040d, 0xa0},
	{0x040e, 0x0b},
	{0x040f, 0x9a},
	{0x034c, 0x0f},
	{0x034d, 0x00},
	{0x034e, 0x08},
	{0x034f, 0x70},
	{0x0301, 0x05},
	{0x0303, 0x02},
	{0x0305, 0x04},
	{0x0306, 0x01},
	{0x0307, 0x21},
	{0x0309, 0x0a},
	{0x030b, 0x02},
	{0x030d, 0x04},
	{0x030e, 0x01},
	{0x030f, 0x50},
	{0x0820, 0x0f},
	{0x0821, 0xc0},
	{0x0822, 0x00},
	{0x0823, 0x00},
	{0x422f, 0x01},
	{0x4230, 0x00},
	{0x3031, 0x01},
	{0x3033, 0x00},
	{0x3039, 0x00},
	{0x303b, 0x00},
	{0x306c, 0x00},
	{0x306e, 0x0d},
	{0x306f, 0x56},
	{0x6636, 0x00},
	{0x6637, 0x14},
	{0xca66, 0x39},
	{0xca67, 0x39},
	{0xca68, 0x39},
	{0xca69, 0x39},
	{0xca6a, 0x13},
	{0xca6b, 0x13},
	{0xca6c, 0x20},
	{0xca6d, 0x20},
	{0xca6e, 0x20},
	{0xca6f, 0x20},
	{0xca70, 0x10},
	{0xca71, 0x10},
	{0x3066, 0x00},
	{0x7b63, 0x00},
	{0x4024, 0x0a},
	{0x4025, 0xb8},
	{0x56fb, 0x33},
	{0x56ff, 0x33},
	{0x6174, 0x29},
	{0x6175, 0x02},
	{0x910a, 0x00},
	{0x9323, 0x15},
	{0xbc60, 0x01},
	{IMX318_REG_EXPOSURE, 0x0c},
	{IMX318_REG_EXPOSURE + 1, 0x36},
	{0x0224, 0x01},
	{0x0225, 0xf4},
	{IMX318_REG_ANALOG_GAIN, 0x00},
	{IMX318_REG_ANALOG_GAIN + 1, 0x00},
	{0x020e, 0x01},
	{0x020f, 0x00},
	{0x0210, 0x01},
	{0x0211, 0x00},
	{0x0212, 0x01},
	{0x0213, 0x00},
	{0x0214, 0x01},
	{0x0215, 0x00},
	{0x0216, 0x00},
	{0x0217, 0x00},
	{0x0218, 0x01},
	{0x0219, 0x00}
};

static const struct reg_sequence imx318_mode_2744x2056_30_regs[] = {
	{0x0111, 0x02},
	{0x0112, 0x0a},
	{0x0113, 0x0a},
	{0x0114, 0x03},
	{0x0342, 0x1c},
	{0x0343, 0xb8},
	{0x0340, 0x08},
	{0x0341, 0x80},
	{0x0344, 0x00},
	{0x0345, 0x00},
	{0x0346, 0x00},
	{0x0347, 0x00},
	{0x0348, 0x15},
	{0x0349, 0x6f},
	{0x034a, 0x10},
	{0x034b, 0x0f},
	{0x31a2, 0x00},
	{0x0220, 0x00},
	{0x0221, 0x11},
	{0x0222, 0x01},
	{0x0381, 0x01},
	{0x0383, 0x01},
	{0x0385, 0x01},
	{0x0387, 0x01},
	{0x0900, 0x01},
	{0x0901, 0x22},
	{0x0902, 0x02},
	{0x3010, 0x65},
	{0x3011, 0x11},
	{0x301c, 0x00},
	{0x3045, 0x01},
	{0x3194, 0x00},
	{0x31a0, 0x00},
	{0x31a1, 0x00},
	{0xd5ec, 0x3a},
	{0xd5ed, 0x00},
	{0x0401, 0x00},
	{0x0404, 0x00},
	{0x0405, 0x10},
	{0x0408, 0x00},
	{0x0409, 0x00},
	{0x040a, 0x00},
	{0x040b, 0x00},
	{0x040c, 0x0a},
	{0x040d, 0xb8},
	{0x040e, 0x08},
	{0x040f, 0x08},
	{0x034c, 0x0a},
	{0x034d, 0xb8},
	{0x034e, 0x08},
	{0x034f, 0x08},
	{0x0301, 0x05},
	{0x0303, 0x02},
	{0x0305, 0x04},
	{0x0306, 0x00},
	{0x0307, 0xc8},
	{0x0309, 0x0a},
	{0x030b, 0x02},
	{0x030d, 0x04},
	{0x030e, 0x00},
	{0x030f, 0xfa},
	{0x0820, 0x0b},
	{0x0821, 0xb8},
	{0x0822, 0x00},
	{0x0823, 0x00},
	{0x3031, 0x01},
	{0x3033, 0x01},
	{0x3039, 0x00},
	{0x303b, 0x00},
	{0x306c, 0x00},
	{0x306e, 0x0d},
	{0x306f, 0x56},
	{0x6636, 0x00},
	{0x6637, 0x14},
	{0x3066, 0x00},
	{0x7b63, 0x00},
	{0x4024, 0x0a},
	{0x4025, 0xb8},
	{0x56fb, 0x33},
	{0x56ff, 0x33},
	{0x6174, 0x29},
	{0x6175, 0x29},
	{0x910a, 0x00},
	{0x9323, 0x15},
	{0xbc60, 0x01},
	{IMX318_REG_EXPOSURE, 0x08},
	{IMX318_REG_EXPOSURE + 1, 0x76},
	{0x0224, 0x01},
	{0x0225, 0xf4},
	{IMX318_REG_ANALOG_GAIN, 0x00},
	{IMX318_REG_ANALOG_GAIN + 1, 0x00},
	{0x020e, 0x01},
	{0x020f, 0x00},
	{0x0210, 0x01},
	{0x0211, 0x00},
	{0x0212, 0x01},
	{0x0213, 0x00},
	{0x0214, 0x01},
	{0x0215, 0x00},
	{0x0216, 0x00},
	{0x0217, 0x00},
	{0x0218, 0x01},
	{0x0219, 0x00}
};

static const struct imx318_mode imx318_modes[] = {
	{
		.width = 5488,
		.height = 4112,
		.code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.framerate = 30,
		.link_freq_idx = 0,
		.regs = imx318_mode_5488x4112_30_regs,
		.num_regs = ARRAY_SIZE(imx318_mode_5488x4112_30_regs)
	},
	{
		.width = 3840,
		.height = 2160,
		.code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.framerate = 30,
		.link_freq_idx = 1,
		.regs = imx318_mode_3840x2160_30_regs,
		.num_regs = ARRAY_SIZE(imx318_mode_3840x2160_30_regs)
	},
	{
		.width = 2744,
		.height = 2056,
		.code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.framerate = 30,
		.link_freq_idx = 2,
		.regs = imx318_mode_2744x2056_30_regs,
		.num_regs = ARRAY_SIZE(imx318_mode_2744x2056_30_regs)
	},
};

static const s64 imx318_link_freqs[] = {
	846249600,
	693600000,
	211562400
};

static inline struct imx318 *to_imx318(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct imx318, sd);
}

static u64 imx318_calc_pixel_rate(struct imx318 *imx318)
{
	s64 link_freq = imx318_link_freqs[imx318->current_mode->link_freq_idx];
	u64 pixel_rate;

	/* pixel rate = link_freq * 2 * nr_of_lanes / bits_per_sample */
	pixel_rate = link_freq * 2 * imx318->nlanes;
	do_div(pixel_rate, IMX318_DEFAULT_BPP);
	return pixel_rate;
}

static int imx318_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx318 *imx318 =
		container_of(ctrl->handler, struct imx318, ctrl_handler);
	int ret = 0;

	/* Set controls only if sensor is in power on state */
	if (!pm_runtime_get_if_in_use(imx318->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = regmap_write(imx318->regmap, IMX318_REG_HOLD, 1);
		if (ret)
			return ret;

		ret = regmap_write(imx318->regmap, IMX318_REG_ANALOG_GAIN,
					ctrl->val >> 8 & 0xff);
		if (ret)
			return ret;

		ret = regmap_write(imx318->regmap, IMX318_REG_ANALOG_GAIN + 1,
					ctrl->val & 0xff);
		if (ret)
			return ret;

		ret = regmap_write(imx318->regmap, IMX318_REG_HOLD, 0);
		if (ret)
			return ret;

		break;
	}

	return ret;
}

static const struct v4l2_ctrl_ops imx318_ctrl_ops = {
	.s_ctrl = imx318_set_ctrl,
};

static int imx318_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx318 *imx318 = to_imx318(sd);

	if (code->index >= ARRAY_SIZE(imx318_modes)) {
		dev_err(imx318->dev, "Code index out of range\n");
		return -EINVAL;
	}

	code->code = imx318_modes[code->index].code;

	return 0;
}

static int imx318_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fsize)
{
	struct imx318 *imx318 = to_imx318(sd);

	if (fsize->index >= ARRAY_SIZE(imx318_modes)) {
		dev_err(imx318->dev, "Frame size index out of range\n");
		return -EINVAL;
	}

	if (fsize->code != imx318_modes[fsize->index].code) {
		dev_err(imx318->dev, "Invalid frame size mbus code\n");
		return -EINVAL;
	}


	fsize->min_width = fsize->max_width = imx318_modes[fsize->index].width;
	fsize->min_height = fsize->max_height = imx318_modes[fsize->index].height;

	return 0;
}

static void imx318_fill_pad_format(struct imx318 *imx318,
				   const struct imx318_mode *mode,
				   struct v4l2_subdev_format *fmt)
{
	/* Resolution */
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;

	/* Pixel format */
	fmt->format.code = mode->code;

	/* Scan mode */
	fmt->format.field = V4L2_FIELD_NONE;

	/* Color */
	fmt->format.colorspace = V4L2_COLORSPACE_RAW;
	fmt->format.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;

	/* Quantization */
	fmt->format.quantization = V4L2_QUANTIZATION_DEFAULT;

	/* Transfer function */
	fmt->format.xfer_func = V4L2_XFER_FUNC_NONE;
}

static int imx318_get_format(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state,
			     struct v4l2_subdev_format *fmt)
{
	struct imx318 *imx318 = to_imx318(sd);

	mutex_lock(&imx318->lock);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		fmt->format = *v4l2_subdev_get_try_format(&imx318->sd,
							sd_state, fmt->pad);
	} else {
		imx318_fill_pad_format(imx318, imx318->current_mode, fmt);
	}

	mutex_unlock(&imx318->lock);

	return 0;
}

static int imx318_set_format(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state,
			     struct v4l2_subdev_format *fmt)
{
	struct imx318 *imx318 = to_imx318(sd);
	const struct imx318_mode *mode;

	mutex_lock(&imx318->lock);

	mode = v4l2_find_nearest_size(imx318_modes, ARRAY_SIZE(imx318_modes),
					width, height,
					fmt->format.width, fmt->format.height);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		fmt->format = *v4l2_subdev_get_try_format(&imx318->sd,
							sd_state, fmt->pad);
	} else {
		imx318->current_mode = mode;
		imx318_fill_pad_format(imx318, imx318->current_mode, fmt);

		dev_dbg(imx318->dev, "Format set to %dx%d\n",
		fmt->format.width, fmt->format.height);

		__v4l2_ctrl_s_ctrl(imx318->link_freq, mode->link_freq_idx);

		__v4l2_ctrl_s_ctrl_int64(imx318->pixel_rate,
					imx318_calc_pixel_rate(imx318));

		dev_dbg(imx318->dev, "Link frequency set to %lld\n",
			imx318_link_freqs[mode->link_freq_idx]);
	}

	mutex_unlock(&imx318->lock);

	return 0;
}

static int imx318_get_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct imx318 *imx318 = to_imx318(sd);

	dev_dbg(imx318->dev, "getting frame interval\n");

	mutex_lock(&imx318->lock);
	fi->interval = imx318->frame_interval;
	mutex_unlock(&imx318->lock);

	return 0;
}

static int imx318_set_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct imx318 *imx318 = to_imx318(sd);
	const struct imx318_mode *mode;
	int ret = 0;

	if (fi->pad != 0)
		return -EINVAL;

	mutex_lock(&imx318->lock);

	if (imx318->streaming) {
		dev_err(imx318->dev,
			"Cannot set frame interval while streaming\n");
		ret = -EBUSY;
		goto out;
	}

	mode = imx318->current_mode;

	fi->interval.denominator = mode->framerate;
	fi->interval.numerator = 1;

	imx318->frame_interval = fi->interval;
out:
	mutex_unlock(&imx318->lock);
	return ret;
}

static int imx318_start_streaming(struct imx318 *imx318)
{
	int ret;

	mutex_lock(&imx318->lock);

	ret = regmap_multi_reg_write(imx318->regmap,
				imx318_common_init_regs,
				ARRAY_SIZE(imx318_common_init_regs));
	if (ret)
		goto error;

	/* Configue mode */
	ret = regmap_multi_reg_write(imx318->regmap,
				imx318->current_mode->regs,
				imx318->current_mode->num_regs);
	if (ret) {
		dev_err(imx318->dev, "Failed to configure mode: %d\n", ret);
		goto error;
	}

	/* Wait for sensor to become ready after configuration */
	usleep_range(10000, 11000);

	/* Start streaming */
	ret = regmap_write(imx318->regmap,
			IMX318_REG_MODE_SELECT, IMX318_MODE_STREAMING);
	if (ret) {
		dev_err(imx318->dev, "Failed to start streaming: %d\n", ret);
		goto error;
	}

	mutex_unlock(&imx318->lock);

	dev_dbg(imx318->dev, "Started streaming\n");

	return 0;
error:
	mutex_unlock(&imx318->lock);

	return ret;
}

static int imx318_stop_streaming(struct imx318 *imx318)
{
	int ret;

	/* Stop streaming */
	ret = regmap_write(imx318->regmap,
			IMX318_REG_MODE_SELECT, IMX318_MODE_STANDBY);
	if (ret)
		dev_err(imx318->dev, "Failed to stop streaming: %d\n", ret);
	else
		dev_dbg(imx318->dev, "Stopped streaming\n");

	return ret;
}

static int imx318_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx318 *imx318 = to_imx318(sd);
	int ret;

	dev_dbg(imx318->dev, "%sabling streaming\n", enable ? "En" : "Dis");

	/* Do nothing if streaming state didn't change */
	if (imx318->streaming == enable) {
		dev_dbg(imx318->dev, "stream is already %sabled\n",
			enable ? "en" : "dis");
		return 0;
	}

	if (enable) {
		ret = pm_runtime_get_sync(imx318->dev);
		if (ret) {
			pm_runtime_put_noidle(imx318->dev);
			return ret;
		}

		ret = imx318_start_streaming(imx318);
		if (ret)
			goto err_rpm_put;
	} else {
		ret = imx318_stop_streaming(imx318);
		if (ret)
			goto err_rpm_put;
		pm_runtime_put(imx318->dev);
	}

	imx318->streaming = enable;

	return 0;

err_rpm_put:
	pm_runtime_put(imx318->dev);
	return ret;
}

static const struct v4l2_subdev_video_ops imx318_video_ops = {
	.g_frame_interval = imx318_get_frame_interval,
	.s_frame_interval = imx318_set_frame_interval,
	.s_stream = imx318_set_stream,
};

static const struct v4l2_subdev_pad_ops imx318_pad_ops = {
	.enum_mbus_code = imx318_enum_mbus_code,
	.enum_frame_size = imx318_enum_frame_size,
	.get_fmt = imx318_get_format,
	.set_fmt = imx318_set_format,
};

static const struct v4l2_subdev_ops imx318_subdev_ops = {
	.video = &imx318_video_ops,
	.pad = &imx318_pad_ops,
};

static int imx318_init_regulators(struct imx318 *imx318)
{
	unsigned int i;
	int ret;

	for (i = 0; i < IMX318_NUM_SUPPLIES; ++i)
		imx318->supplies[i].supply = imx318_supply_data[i].name;

	ret = devm_regulator_bulk_get(imx318->dev, IMX318_NUM_SUPPLIES,
				       imx318->supplies);
	if (ret)
		return ret;

	/* Configure loads */
	for (i = 0; i < IMX318_NUM_SUPPLIES; ++i) {
		if (!imx318_supply_data[i].load)
			continue;

		ret = regulator_set_load(imx318->supplies[i].consumer,
					imx318_supply_data[i].load);
		if (ret) {
			dev_err(imx318->dev, "Failed to set load for %s supply: %d\n",
				imx318_supply_data[i].name, ret);
			return ret;
		}
	}

	return 0;
}

static int imx318_power_on(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx318 *imx318 = to_imx318(sd);
	int ret;

	gpiod_set_value_cansleep(imx318->reset_gpio, 1);

	ret = regulator_bulk_enable(IMX318_NUM_SUPPLIES, imx318->supplies);
	if (ret) {
		dev_err(imx318->dev, "Failed to enable regulators: %d\n", ret);
		regulator_bulk_disable(IMX318_NUM_SUPPLIES, imx318->supplies);
		return ret;
	}

	ret = clk_prepare_enable(imx318->extclk);
	if (ret < 0) {
		dev_err(imx318->dev, "Failed to enable external clock: %d\n", ret);
		return ret;
	}

	gpiod_set_value_cansleep(imx318->reset_gpio, 0);

	/* Wait for sensor to power on */
	usleep_range(19000, 20000);

	dev_dbg(imx318->dev, "Powered on\n");

	return ret;
}

static int imx318_power_off(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx318 *imx318 = to_imx318(sd);
	int ret;

	gpiod_set_value_cansleep(imx318->reset_gpio, 1);

	clk_disable_unprepare(imx318->extclk);

	ret = regulator_bulk_disable(IMX318_NUM_SUPPLIES, imx318->supplies);
	if (ret)
		return ret;

	dev_dbg(imx318->dev, "Powered off\n");

	return 0;
}

static const struct dev_pm_ops imx318_pm_ops = {
	SET_RUNTIME_PM_OPS(imx318_power_off, imx318_power_on, NULL)
};

static int imx318_parse_fwnode(struct imx318 *imx318)
{
	struct fwnode_handle *fwnode = dev_fwnode(imx318->dev);
	struct fwnode_handle *endpoint;
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_CPHY,
	};
	int ret;

	ret = fwnode_property_read_u32(fwnode, "clock-frequency",
					&imx318->extclk_rate);
	if (ret) {
		dev_err(imx318->dev, "Failed to read external clock frequency\n");
		return -EINVAL;
	}

	endpoint = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!endpoint) {
		dev_err(imx318->dev, "Failed to get endpoint node:\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_alloc_parse(endpoint, &bus_cfg);
	if (ret)
		dev_err(imx318->dev, "Failed to parse endpoint node: %d\n", ret);

	imx318->nlanes = bus_cfg.bus.mipi_csi2.num_data_lanes;
	/* TODO: Find minimum nlanes */
	if (imx318->nlanes != 4) {
		dev_err(imx318->dev, "Invalid data lanes: %d\n", imx318->nlanes);
		ret = -EINVAL;
	}

	v4l2_fwnode_endpoint_free(&bus_cfg);
	fwnode_handle_put(endpoint);

	return ret;
}

static int imx318_probe(struct i2c_client *client)
{
	struct imx318 *imx318;
	u32 id, val;
	int ret;

	imx318 = devm_kzalloc(&client->dev, sizeof(*imx318), GFP_KERNEL);
	if (!imx318)
		return -ENOMEM;

	imx318->dev = &client->dev;

	ret = imx318_parse_fwnode(imx318);
	if (ret)
		return ret;

	imx318->extclk = devm_clk_get(imx318->dev, NULL);
	if (IS_ERR(imx318->extclk)) {
		ret = PTR_ERR(imx318->extclk);
		dev_err(imx318->dev, "Failed to get external clock: %d\n", ret);
		return ret;
	}

	ret = clk_set_rate(imx318->extclk, imx318->extclk_rate);
	if (ret) {
		dev_err(imx318->dev, "Failed to set external clock rate: %d\n", ret);
		return ret;
	}

	imx318->extclk_rate = clk_get_rate(imx318->extclk);
	/*
	 * Sensor might still work with a slightly mismatched input clock rate.
	 * Warn and proceed.
	 */
	if (imx318->extclk_rate != IMX318_EXTCLK_RATE)
		dev_warn(imx318->dev, "External clock rate mismatch\n");

	ret = imx318_init_regulators(imx318);
	if (ret) {
		dev_err(imx318->dev, "Failed to initialize regulators: %d\n", ret);
		return ret;
	}

	imx318->reset_gpio = devm_gpiod_get_optional(imx318->dev, "reset",
						   GPIOD_OUT_LOW);
	if (IS_ERR(imx318->reset_gpio)) {
		ret = PTR_ERR(imx318->reset_gpio);
		dev_err(imx318->dev, "Failed to get reset gpio: %d\n", ret);
		return ret;
	}

	imx318->regmap = devm_regmap_init_i2c(client, &imx318_regmap_config);
	if (IS_ERR(imx318->regmap)) {
		ret = PTR_ERR(imx318->regmap);
		dev_err(imx318->dev, "Failed to initialize regmap: %d\n", ret);
		return ret;
	}

	/* Default mode */
	imx318->current_mode = &imx318_modes[0];
	imx318->frame_interval.denominator = imx318->current_mode->framerate;
	imx318->frame_interval.numerator = 1;
	imx318->streaming = false;

	v4l2_i2c_subdev_init(&imx318->sd, client, &imx318_subdev_ops);

	/* Sensor must be powered on to read the ID register */
	ret = imx318_power_on(imx318->dev);
	if (ret) {
		dev_err(imx318->dev, "Failed to power on sensor: %d\n", ret);
		return ret;
	}

	ret = regmap_read(imx318->regmap, IMX318_REG_ID, &val);
	if (!ret)
		ret = regmap_read(imx318->regmap, IMX318_REG_ID + 1, &id);

	if (ret) {
		dev_err(imx318->dev, "Failed to read ID register: %d\n", ret);
		goto power_off;
	}
	else {
		id |= val << 8;
		dev_dbg(imx318->dev, "Sensor ID: 0x%x\n", id);
		if(id != IMX318_ID) {
			dev_err(imx318->dev, "Sensor ID mismatch\n");
			ret = -EINVAL;
			goto power_off;
		}
	}

	/* Add V4L2 controls */
	v4l2_ctrl_handler_init(&imx318->ctrl_handler, 3);
	imx318->pixel_rate = v4l2_ctrl_new_std(&imx318->ctrl_handler,
						NULL,
						V4L2_CID_PIXEL_RATE, 0,
						INT_MAX, 1,
						imx318_calc_pixel_rate(imx318));
	imx318->link_freq = v4l2_ctrl_new_int_menu(&imx318->ctrl_handler,
						NULL,
						V4L2_CID_LINK_FREQ,
						ARRAY_SIZE(imx318_link_freqs) - 1,
						imx318->current_mode->link_freq_idx,
						imx318_link_freqs);
	imx318->analog_gain = v4l2_ctrl_new_std(&imx318->ctrl_handler,
						&imx318_ctrl_ops,
						V4L2_CID_ANALOGUE_GAIN,
						0,
						511,
						1,
						0);
	ret = imx318->ctrl_handler.error;
	if (ret) {
		dev_err(imx318->dev, "Failed to initialize controls: %d\n", ret);
		goto free_ctrl;
	}
	imx318->pixel_rate->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	imx318->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx318->sd.ctrl_handler = &imx318->ctrl_handler;
	mutex_init(&imx318->lock);
	imx318->ctrl_handler.lock = &imx318->lock;

	imx318->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	imx318->pad.flags = MEDIA_PAD_FL_SOURCE;
	imx318->sd.dev = imx318->dev;
	imx318->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	ret = media_entity_pads_init(&imx318->sd.entity, 1, &imx318->pad);
	if (ret) {
		dev_err(imx318->dev,
			"Failed to register media entity pads: %d\n", ret);
		goto free_ctrl;
	}

	ret = v4l2_async_register_subdev_sensor(&imx318->sd);
	if (ret) {
		dev_err(imx318->dev,
			"Failed to register V4L2 sensor: %d\n", ret);
		goto free_entity;
	}

	/* Enable runtime PM and turn off the device */
	pm_runtime_set_active(imx318->dev);
	pm_runtime_enable(imx318->dev);
	pm_runtime_idle(imx318->dev);

	return 0;

free_entity:
	media_entity_cleanup(&imx318->sd.entity);
free_ctrl:
	v4l2_ctrl_handler_free(&imx318->ctrl_handler);
	mutex_destroy(&imx318->lock);

	pm_runtime_disable(imx318->dev);
power_off:
	imx318_power_off(imx318->dev);

	return ret;
}

static int imx318_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx318 *imx318 = to_imx318(sd);

	v4l2_async_unregister_subdev(&imx318->sd);
	media_entity_cleanup(&imx318->sd.entity);
	v4l2_ctrl_handler_free(&imx318->ctrl_handler);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(imx318->dev))
		imx318_power_off(imx318->dev);
	pm_runtime_set_suspended(&client->dev);

	mutex_destroy(&imx318->lock);

	return 0;
}

static const struct of_device_id imx318_of_match[] = {
	{ .compatible = "sony,imx318" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx318_of_match);

static struct i2c_driver imx318_i2c_driver = {
	.probe_new  = imx318_probe,
	.remove = imx318_remove,
	.driver = {
		.name  = "imx318",
		.pm = &imx318_pm_ops,
		.of_match_table = of_match_ptr(imx318_of_match),
	},
};

module_i2c_driver(imx318_i2c_driver);

MODULE_AUTHOR("Yassine Oudjana <y.oudjana@protonmail.com>");
MODULE_DESCRIPTION("Sony IMX318 CMOS Image Sensor Driver");
MODULE_LICENSE("GPL v2");
