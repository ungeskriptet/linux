// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2022, The Linux Foundation. All rights reserved.
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct otm1911_fhdplus {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data supplies[2];
	struct gpio_desc *reset_gpio;
	bool prepared;
};

static inline struct otm1911_fhdplus *to_otm1911_fhdplus(struct drm_panel *panel)
{
	return container_of(panel, struct otm1911_fhdplus, panel);
}

#define dsi_dcs_write_seq(dsi, seq...) do {				\
		static const u8 d[] = { seq };				\
		int ret;						\
		ret = mipi_dsi_dcs_write_buffer(dsi, d, ARRAY_SIZE(d));	\
		if (ret < 0)						\
			return ret;					\
	} while (0)

static void otm1911_fhdplus_reset(struct otm1911_fhdplus *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(2000, 3000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(15000, 16000);
}

static int otm1911_fhdplus_on(struct otm1911_fhdplus *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi_dcs_write_seq(dsi, 0x00, 0x00);
	dsi_dcs_write_seq(dsi, 0xff, 0x19, 0x11, 0x01);
	dsi_dcs_write_seq(dsi, 0x00, 0x80);
	dsi_dcs_write_seq(dsi, 0xff, 0x19, 0x11);
	dsi_dcs_write_seq(dsi, 0x00, 0xb0);
	dsi_dcs_write_seq(dsi, 0xb3, 0x04, 0x38, 0x08, 0xe8);
	dsi_dcs_write_seq(dsi, 0x00, 0x80);
	dsi_dcs_write_seq(dsi, 0xc9, 0x8e);
	dsi_dcs_write_seq(dsi, 0x00, 0x80);
	dsi_dcs_write_seq(dsi, 0xca,
			  0xf0, 0xd9, 0xc8, 0xba, 0xaf, 0xa6, 0x9e, 0x98, 0x92,
			  0x8d, 0x88, 0x84);
	dsi_dcs_write_seq(dsi, 0x00, 0x90);
	dsi_dcs_write_seq(dsi, 0xca,
			  0xfe, 0xff, 0x66, 0xfb, 0xff, 0x33, 0xf6, 0xff, 0x66);
	dsi_dcs_write_seq(dsi, 0x00, 0xa0);
	dsi_dcs_write_seq(dsi, 0xd6,
			  0x0f, 0x10, 0x0f, 0x11, 0x1b, 0x15, 0x10, 0x0c, 0x0d,
			  0x0f, 0x10, 0x10);
	dsi_dcs_write_seq(dsi, 0x00, 0xb0);
	dsi_dcs_write_seq(dsi, 0xd6,
			  0x99, 0xc8, 0xc7, 0x8f, 0xdc, 0xed, 0xbf, 0x9a, 0x8f,
			  0x89, 0x93, 0x9c);
	dsi_dcs_write_seq(dsi, 0x00, 0xc0);
	dsi_dcs_write_seq(dsi, 0xd6,
			  0x91, 0xb0, 0xaf, 0x8a, 0xbd, 0xc9, 0xaa, 0x91, 0x8a,
			  0x86, 0x8d, 0x93);
	dsi_dcs_write_seq(dsi, 0x00, 0xd0);
	dsi_dcs_write_seq(dsi, 0xd6,
			  0x88, 0x98, 0x98, 0x85, 0x9f, 0xa4, 0x95, 0x89, 0x85,
			  0x83, 0x86, 0x89);
	dsi_dcs_write_seq(dsi, 0x00, 0xb0);
	dsi_dcs_write_seq(dsi, 0xca, 0x00);
	dsi_dcs_write_seq(dsi, 0x00, 0xb2);
	dsi_dcs_write_seq(dsi, 0xca, 0x0a);

	ret = mipi_dsi_dcs_set_display_brightness(dsi, 0x0cff);
	if (ret < 0) {
		dev_err(dev, "Failed to set display brightness: %d\n", ret);
		return ret;
	}

	dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x2c);
	dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_POWER_SAVE, 0x00);

	ret = mipi_dsi_dcs_set_tear_on(dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (ret < 0) {
		dev_err(dev, "Failed to set tear on: %d\n", ret);
		return ret;
	}

	dsi_dcs_write_seq(dsi, 0x11, 0x00);
	msleep(120);
	dsi_dcs_write_seq(dsi, 0x29, 0x00);
	msleep(20);

	return 0;
}

static int otm1911_fhdplus_off(struct otm1911_fhdplus *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;

	dsi_dcs_write_seq(dsi, 0xff, 0x19, 0x11, 0x01);
	dsi_dcs_write_seq(dsi, 0x00, 0x80);
	dsi_dcs_write_seq(dsi, 0xff, 0x19, 0x11);
	dsi_dcs_write_seq(dsi, 0x00, 0x90);
	dsi_dcs_write_seq(dsi, 0xb3, 0x34);
	dsi_dcs_write_seq(dsi, 0x28, 0x00);
	msleep(20);
	dsi_dcs_write_seq(dsi, 0x10, 0x00);
	msleep(120);

	return 0;
}

static int otm1911_fhdplus_prepare(struct drm_panel *panel)
{
	struct otm1911_fhdplus *ctx = to_otm1911_fhdplus(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (ctx->prepared)
		return 0;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	otm1911_fhdplus_reset(ctx);

	ret = otm1911_fhdplus_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
		return ret;
	}

	ctx->prepared = true;
	return 0;
}

static int otm1911_fhdplus_unprepare(struct drm_panel *panel)
{
	struct otm1911_fhdplus *ctx = to_otm1911_fhdplus(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (!ctx->prepared)
		return 0;

	ret = otm1911_fhdplus_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);

	ctx->prepared = false;
	return 0;
}

static const struct drm_display_mode otm1911_fhdplus_mode = {
	.clock = (1080 + 24 + 20 + 24) * (2280 + 47 + 2 + 38) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 24,
	.hsync_end = 1080 + 24 + 20,
	.htotal = 1080 + 24 + 20 + 24,
	.vdisplay = 2280,
	.vsync_start = 2280 + 47,
	.vsync_end = 2280 + 47 + 2,
	.vtotal = 2280 + 47 + 2 + 38,
	.width_mm = 69,
	.height_mm = 122,
};

static int otm1911_fhdplus_get_modes(struct drm_panel *panel,
				 struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &otm1911_fhdplus_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs otm1911_fhdplus_panel_funcs = {
	.prepare = otm1911_fhdplus_prepare,
	.unprepare = otm1911_fhdplus_unprepare,
	.get_modes = otm1911_fhdplus_get_modes,
};

static int otm1911_fhdplus_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct otm1911_fhdplus *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->supplies[0].supply = "vsp";
	ctx->supplies[1].supply = "vsn";
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
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, dev, &otm1911_fhdplus_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static int otm1911_fhdplus_remove(struct mipi_dsi_device *dsi)
{
	struct otm1911_fhdplus *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);

	return 0;
}

static const struct of_device_id otm1911_fhdplus_of_match[] = {
	{ .compatible = "mdss,otm1911-fhdplus" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, otm1911_fhdplus_of_match);

static struct mipi_dsi_driver otm1911_fhdplus_driver = {
	.probe = otm1911_fhdplus_probe,
	.remove = otm1911_fhdplus_remove,
	.driver = {
		.name = "panel-otm1911-fhdplus",
		.of_match_table = otm1911_fhdplus_of_match,
	},
};
module_mipi_dsi_driver(otm1911_fhdplus_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for otm1911 fhdplus video mode dsi panel");
MODULE_LICENSE("GPL v2");
