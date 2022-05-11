// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2022 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct zte_samsung_5p5_wqhd_dualmipi {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data supplies[3];
	struct gpio_desc *reset_gpio;
	bool prepared;
};

static inline
struct zte_samsung_5p5_wqhd_dualmipi *to_zte_samsung_5p5_wqhd_dualmipi(struct drm_panel *panel)
{
	return container_of(panel, struct zte_samsung_5p5_wqhd_dualmipi, panel);
}

#define dsi_dcs_write_seq(dsi, seq...) do {				\
		static const u8 d[] = { seq };				\
		int ret;						\
		ret = mipi_dsi_dcs_write_buffer(dsi, d, ARRAY_SIZE(d));	\
		if (ret < 0)						\
			return ret;					\
	} while (0)

static void zte_samsung_5p5_wqhd_dualmipi_reset(struct zte_samsung_5p5_wqhd_dualmipi *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(2000, 3000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(5000, 6000);
}

static int zte_samsung_5p5_wqhd_dualmipi_on(struct zte_samsung_5p5_wqhd_dualmipi *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	usleep_range(5000, 6000);

	dsi_dcs_write_seq(dsi, 0xf0, 0x5a, 0x5a);
	dsi_dcs_write_seq(dsi, 0xc4, 0x03);
	dsi_dcs_write_seq(dsi, 0xf9, 0x03);
	dsi_dcs_write_seq(dsi, 0xc2,
			  0x00, 0x08, 0xd8, 0xd8, 0x00, 0x80, 0x2b, 0x05, 0x08,
			  0x0e, 0x07, 0x0b, 0x05, 0x0d, 0x0a, 0x15, 0x13, 0x20,
			  0x1e);
	dsi_dcs_write_seq(dsi, 0xed, 0x45);
	dsi_dcs_write_seq(dsi, 0xf6,
			  0x42, 0x57, 0x37, 0x00, 0xaa, 0xcc, 0xd0, 0x00, 0x00);
	dsi_dcs_write_seq(dsi, 0xf0, 0xa5, 0xa5);
	msleep(120);
	dsi_dcs_write_seq(dsi, 0x12);
	usleep_range(1000, 2000);
	dsi_dcs_write_seq(dsi, 0x35, 0x00);
	dsi_dcs_write_seq(dsi, 0x53, 0x20);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}
	usleep_range(16000, 17000);

	return 0;
}

static int zte_samsung_5p5_wqhd_dualmipi_off(struct zte_samsung_5p5_wqhd_dualmipi *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi_dcs_write_seq(dsi, 0x51, 0x00);

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display off: %d\n", ret);
		return ret;
	}
	msleep(50);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}
	msleep(120);

	return 0;
}

static int zte_samsung_5p5_wqhd_dualmipi_prepare(struct drm_panel *panel)
{
	struct zte_samsung_5p5_wqhd_dualmipi *ctx = to_zte_samsung_5p5_wqhd_dualmipi(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (ctx->prepared)
		return 0;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	zte_samsung_5p5_wqhd_dualmipi_reset(ctx);

	ret = zte_samsung_5p5_wqhd_dualmipi_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
		return ret;
	}

	ctx->prepared = true;
	return 0;
}

static int zte_samsung_5p5_wqhd_dualmipi_unprepare(struct drm_panel *panel)
{
	struct zte_samsung_5p5_wqhd_dualmipi *ctx = to_zte_samsung_5p5_wqhd_dualmipi(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (!ctx->prepared)
		return 0;

	ret = zte_samsung_5p5_wqhd_dualmipi_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);

	ctx->prepared = false;
	return 0;
}

static const struct drm_display_mode zte_samsung_5p5_wqhd_dualmipi_mode = {
	.clock = (720 + 140 + 16 + 66) * (2560 + 16 + 16 + 8) * 60 / 1000,
	.hdisplay = 720,
	.hsync_start = 720 + 140,
	.hsync_end = 720 + 140 + 16,
	.htotal = 720 + 140 + 16 + 66,
	.vdisplay = 2560,
	.vsync_start = 2560 + 16,
	.vsync_end = 2560 + 16 + 16,
	.vtotal = 2560 + 16 + 16 + 8,
	.width_mm = 68,
	.height_mm = 121,
};

static int zte_samsung_5p5_wqhd_dualmipi_get_modes(struct drm_panel *panel,
						   struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &zte_samsung_5p5_wqhd_dualmipi_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs zte_samsung_5p5_wqhd_dualmipi_panel_funcs = {
	.prepare = zte_samsung_5p5_wqhd_dualmipi_prepare,
	.unprepare = zte_samsung_5p5_wqhd_dualmipi_unprepare,
	.get_modes = zte_samsung_5p5_wqhd_dualmipi_get_modes,
};

static int zte_samsung_5p5_wqhd_dualmipi_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

// TODO: Check if /sys/class/backlight/.../actual_brightness actually returns
// correct values. If not, remove this function.
static int zte_samsung_5p5_wqhd_dualmipi_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness(dsi, &brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return brightness & 0xff;
}

static const struct backlight_ops zte_samsung_5p5_wqhd_dualmipi_bl_ops = {
	.update_status = zte_samsung_5p5_wqhd_dualmipi_bl_update_status,
	.get_brightness = zte_samsung_5p5_wqhd_dualmipi_bl_get_brightness,
};

static struct backlight_device *
zte_samsung_5p5_wqhd_dualmipi_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 255,
		.max_brightness = 255,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &zte_samsung_5p5_wqhd_dualmipi_bl_ops, &props);
}

static int zte_samsung_5p5_wqhd_dualmipi_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct zte_samsung_5p5_wqhd_dualmipi *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->supplies[0].supply = "vddio";
	ctx->supplies[1].supply = "vdda";
	ctx->supplies[2].supply = "vcca";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	drm_panel_init(&ctx->panel, dev, &zte_samsung_5p5_wqhd_dualmipi_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ctx->panel.backlight = zte_samsung_5p5_wqhd_dualmipi_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static int zte_samsung_5p5_wqhd_dualmipi_remove(struct mipi_dsi_device *dsi)
{
	struct zte_samsung_5p5_wqhd_dualmipi *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);

	return 0;
}

static const struct of_device_id zte_samsung_5p5_wqhd_dualmipi_of_match[] = {
	{ .compatible = "samsung,s6e3ha3" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, zte_samsung_5p5_wqhd_dualmipi_of_match);

static struct mipi_dsi_driver zte_samsung_5p5_wqhd_dualmipi_driver = {
	.probe = zte_samsung_5p5_wqhd_dualmipi_probe,
	.remove = zte_samsung_5p5_wqhd_dualmipi_remove,
	.driver = {
		.name = "panel-zte-samsung-5p5-wqhd-dualmipi",
		.of_match_table = zte_samsung_5p5_wqhd_dualmipi_of_match,
	},
};
module_mipi_dsi_driver(zte_samsung_5p5_wqhd_dualmipi_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for zteSAM_S6E3HA3_SAM_25601440_5P5Inch");
MODULE_LICENSE("GPL v2");
