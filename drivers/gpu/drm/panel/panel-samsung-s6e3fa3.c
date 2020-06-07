// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2020, The Linux Foundation. All rights reserved.

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

struct s6e3fa3 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct backlight_device *backlight;
	struct regulator_bulk_data supplies[2];
	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;

	bool prepared;
	bool enabled;
};

static inline struct s6e3fa3 *to_s6e3fa3(struct drm_panel *panel)
{
	return container_of(panel, struct s6e3fa3, panel);
}

#define dsi_dcs_write_seq(dsi, seq...) do {				\
		static const u8 d[] = { seq };				\
		int ret;						\
		ret = mipi_dsi_dcs_write_buffer(dsi, d, ARRAY_SIZE(d));	\
		if (ret < 0)						\
			return ret;					\
	} while (0)

static void s6e3fa3_reset(struct s6e3fa3 *ctx)
{
	gpiod_set_value_cansleep(ctx->enable_gpio, 1);
	usleep_range(2000, 3000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(2000, 3000);
}

static int s6e3fa3_on(struct s6e3fa3 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	msleep(20);

	if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO) {
		dsi_dcs_write_seq(dsi, 0xf0, 0x5a, 0x5a);
		dsi_dcs_write_seq(dsi, 0xb9, 0x01);
		dsi_dcs_write_seq(dsi, 0xf0, 0xa5, 0xa5);
		dsi_dcs_write_seq(dsi, 0xf0, 0x5a, 0x5a);
		dsi_dcs_write_seq(dsi, 0xfc, 0x5a, 0x5a);
		dsi_dcs_write_seq(dsi, 0xf4, 0x00, 0x01);
		dsi_dcs_write_seq(dsi, 0xc0, 0x32);
		dsi_dcs_write_seq(dsi, 0xf7, 0x03);
		dsi_dcs_write_seq(dsi, 0xf0, 0xa5, 0xa5);
		dsi_dcs_write_seq(dsi, 0xfc, 0xa5, 0xa5);
		dsi_dcs_write_seq(dsi, 0x57, 0x40);
	} else {
		dsi_dcs_write_seq(dsi, 0xf0, 0x5a, 0x5a);
		dsi_dcs_write_seq(dsi, 0xfc, 0x5a, 0x5a);
		dsi_dcs_write_seq(dsi, 0xb0, 0x01);
		dsi_dcs_write_seq(dsi, 0xcc, 0x54, 0x4a);
		dsi_dcs_write_seq(dsi, 0xb0, 0x06);
		dsi_dcs_write_seq(dsi, 0xff, 0x02);
		dsi_dcs_write_seq(dsi, 0xf0, 0xa5, 0xa5);
		dsi_dcs_write_seq(dsi, 0xfc, 0xa5, 0xa5);
		dsi_dcs_write_seq(dsi, 0x35, 0x00);
		dsi_dcs_write_seq(dsi, 0xfc, 0x5a, 0x5a);
		dsi_dcs_write_seq(dsi, 0xf4, 0x00, 0x01);
		dsi_dcs_write_seq(dsi, 0xfc, 0xa5, 0xa5);
		dsi_dcs_write_seq(dsi, 0x57, 0x40);
	}

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}

	return 0;
}

static int s6e3fa3_off(struct s6e3fa3 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display off: %d\n", ret);
		return ret;
	}
	msleep(40);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}
	msleep(160);

	return 0;
}

static int s6e3fa3_prepare(struct drm_panel *panel)
{
	struct s6e3fa3 *ctx = to_s6e3fa3(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (ctx->prepared)
		return 0;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	s6e3fa3_reset(ctx);

	ret = s6e3fa3_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 0);
		return ret;
	}

	ctx->prepared = true;
	return 0;
}

static int s6e3fa3_unprepare(struct drm_panel *panel)
{
	struct s6e3fa3 *ctx = to_s6e3fa3(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (!ctx->prepared)
		return 0;

	ret = s6e3fa3_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 0);

	ret = regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to disable regulators: %d\n", ret);
		return ret;
	}

	ctx->prepared = false;
	return 0;
}

static int s6e3fa3_enable(struct drm_panel *panel)
{
	struct s6e3fa3 *ctx = to_s6e3fa3(panel);
	int ret;

	if (ctx->enabled)
		return 0;

	ret = backlight_enable(ctx->backlight);
	if (ret < 0) {
		dev_err(&ctx->dsi->dev, "Failed to enable backlight: %d\n", ret);
		return ret;
	}

	ctx->enabled = true;
	return 0;
}

static int s6e3fa3_disable(struct drm_panel *panel)
{
	struct s6e3fa3 *ctx = to_s6e3fa3(panel);
	int ret;

	if (!ctx->enabled)
		return 0;

	ret = backlight_disable(ctx->backlight);
	if (ret < 0) {
		dev_err(&ctx->dsi->dev, "Failed to disable backlight: %d\n", ret);
		return ret;
	}

	ctx->enabled = false;
	return 0;
}

static const struct drm_display_mode s6e3fa3_mode = {
	.clock = (1080 + 120 + 19 + 70) * (1920 + 18 + 2 + 4) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 120,
	.hsync_end = 1080 + 120 + 19,
	.htotal = 1080 + 120 + 19 + 70,
	.vdisplay = 1920,
	.vsync_start = 1920 + 18,
	.vsync_end = 1920 + 18 + 2,
	.vtotal = 1920 + 18 + 2 + 4,
	.width_mm = 68,
	.height_mm = 122,
};

static int s6e3fa3_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &s6e3fa3_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

	return 1;
}

static const struct drm_panel_funcs s6e3fa3_panel_funcs = {
	.disable = s6e3fa3_disable,
	.unprepare = s6e3fa3_unprepare,
	.prepare = s6e3fa3_prepare,
	.enable = s6e3fa3_enable,
	.get_modes = s6e3fa3_get_modes,
};

static int s6e3fa3_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	int err;
	u16 brightness = bl->props.brightness;

	err = mipi_dsi_dcs_get_display_brightness(dsi, &brightness);
	if (err < 0)
		 return err;

	return brightness & 0xff;
}

static int s6e3fa3_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	int err;

	err = mipi_dsi_dcs_set_display_brightness(dsi, bl->props.brightness);
	if (err < 0)
		 return err;

	return 0;
}

static const struct backlight_ops s6e3fa3_bl_ops = {
	.update_status = s6e3fa3_bl_update_status,
	.get_brightness = s6e3fa3_bl_get_brightness,
};

static struct backlight_device *
s6e3fa3_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 255,
		.max_brightness = 255,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &s6e3fa3_bl_ops, &props);
}

static int s6e3fa3_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct s6e3fa3 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->supplies[0].supply = "vddio";
	ctx->supplies[1].supply = "vdda";

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to get regulators: %d\n", ret);
		return ret;
	}

	ctx->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->enable_gpio)) {
		ret = PTR_ERR(ctx->enable_gpio);
		dev_err(dev, "Failed to get enable-gpios: %d\n", ret);
		return ret;
	}

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		ret = PTR_ERR(ctx->reset_gpio);
		dev_err(dev, "Failed to get reset-gpios: %d\n", ret);
		return ret;
	}

	ctx->backlight = s6e3fa3_create_backlight(dsi);
	if (IS_ERR(ctx->backlight)) {
		ret = PTR_ERR(ctx->backlight);
		dev_err(dev, "Failed to create backlight: %d\n", ret);
		return ret;
	}

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;

	drm_panel_init(&ctx->panel, dev, &s6e3fa3_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		return ret;
	}

	return 0;
}

static int s6e3fa3_remove(struct mipi_dsi_device *dsi)
{
	struct s6e3fa3 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);

	return 0;
}

static const struct of_device_id s6e3fa3_of_match[] = {
	{ .compatible = "samsung,s6e3fa3" },
	{ }
};
MODULE_DEVICE_TABLE(of, s6e3fa3_of_match);

static struct mipi_dsi_driver s6e3fa3_driver = {
	.probe = s6e3fa3_probe,
	.remove = s6e3fa3_remove,
	.driver = {
		.name = "panel-samsung-s6e3fa3",
		.of_match_table = s6e3fa3_of_match,
	},
};
module_mipi_dsi_driver(s6e3fa3_driver);

MODULE_DESCRIPTION("DRM driver for Samsung S6E3FA3 1080p cmd mode DSI panel");
MODULE_LICENSE("GPL v2");
