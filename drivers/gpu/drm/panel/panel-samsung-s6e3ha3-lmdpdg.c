// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2023 David Wronek <davidwronek@gmail.com>

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

#define RESET 1

#define mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, cmd, seq...)        \
		do {                                                 \
			mipi_dsi_dcs_write_seq(dsi0, cmd, seq);      \
			mipi_dsi_dcs_write_seq(dsi1, cmd, seq);      \
		} while (0)

struct s6e3ha3 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi[2];
#if RESET
	struct gpio_desc *reset_gpio;
#endif
	bool prepared;
};

static inline
struct s6e3ha3 *to_s6e3ha3(struct drm_panel *panel)
{
	return container_of(panel, struct s6e3ha3, panel);
}

#if RESET
static void s6e3ha3_reset(struct s6e3ha3 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(2000, 3000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(5000, 6000);
}
#endif

static int s6e3ha3_on(struct s6e3ha3 *ctx)
{
	struct mipi_dsi_device *dsi0 = ctx->dsi[0];
	struct mipi_dsi_device *dsi1 = ctx->dsi[1];

	ctx->dsi[0]->mode_flags |= MIPI_DSI_MODE_LPM;
	ctx->dsi[1]->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x11);
	usleep_range(5000, 6000);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xc4, 0x03);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xf9, 0x03);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xc2,
			       0x00, 0x08, 0xd8, 0xd8, 0x00, 0x80, 0x2b, 0x05,
			       0x08, 0x0e, 0x07, 0x0b, 0x05, 0x0d, 0x0a, 0x15,
			       0x13, 0x20, 0x1e);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xed, 0x45);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xf6,
			       0x42, 0x57, 0x37, 0x00, 0xaa, 0xcc, 0xd0, 0x00,
			       0x00);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xf0, 0xa5, 0xa5);
	msleep(120);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x12);
	usleep_range(1000, 2000);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x35, 0x00);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x53, 0x20);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x29);
	usleep_range(16000, 17000);

	mipi_dsi_dcs_write_seq(dsi0, 0x51, 0x1);

	return 0;
}

static int s6e3ha3_off(struct s6e3ha3 *ctx)
{
	struct mipi_dsi_device *dsi0 = ctx->dsi[0];
	struct mipi_dsi_device *dsi1 = ctx->dsi[1];

	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x51, 0x0);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x28);
	msleep(50);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x10);
	msleep(120);

	return 0;
}

static int s6e3ha3_prepare(struct drm_panel *panel)
{
	struct s6e3ha3 *ctx = to_s6e3ha3(panel);
	struct mipi_dsi_device *dsi = ctx->dsi[0];
	struct device *dev = &dsi->dev;
	int ret;

	if (ctx->prepared)
		return 0;
#if RESET
	s6e3ha3_reset(ctx);
#endif

	ret = s6e3ha3_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
#if RESET
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
#endif
		return ret;
	}

	ctx->prepared = true;
	return 0;
}

static int s6e3ha3_unprepare(struct drm_panel *panel)
{
	struct s6e3ha3 *ctx = to_s6e3ha3(panel);
	struct device *dev = &ctx->dsi[0]->dev;
	int ret;

	if (!ctx->prepared)
		return 0;

	ret = s6e3ha3_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

#if RESET
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
#endif

	ctx->prepared = false;
	return 0;
}

static const struct drm_display_mode s6e3ha3_mode = {
	.clock = (1440 + 280 + 32 + 132) * (2560 + 16 + 16 + 8) * 60 / 1000,
	.hdisplay = 1440,
	.hsync_start = 1440 + 280,
	.hsync_end = 1440 + 280 + 32,
	.htotal = 1440 + 280 + 32 + 132,
	.vdisplay = 2560,
	.vsync_start = 2560 + 16,
	.vsync_end = 2560 + 16 + 16,
	.vtotal = 2560 + 16 + 16 + 8,
	.width_mm = 68,
	.height_mm = 121,
};

static int s6e3ha3_get_modes(struct drm_panel *panel,
						   struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &s6e3ha3_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs s6e3ha3_panel_funcs = {
	.prepare = s6e3ha3_prepare,
	.unprepare = s6e3ha3_unprepare,
	.get_modes = s6e3ha3_get_modes,
};

static int s6e3ha3_bl_update_status(struct backlight_device *bl)
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
static int s6e3ha3_bl_get_brightness(struct backlight_device *bl)
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

static const struct backlight_ops s6e3ha3_bl_ops = {
	.update_status = s6e3ha3_bl_update_status,
	.get_brightness = s6e3ha3_bl_get_brightness,
};

static struct backlight_device *
s6e3ha3_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 255,
		.max_brightness = 255,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &s6e3ha3_bl_ops, &props);
}

static int s6e3ha3_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct s6e3ha3 *ctx;
	struct mipi_dsi_host *dsi_sec_host;
	struct device_node *dsi_sec;
	int i, ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

#if RESET
	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");
#endif

	dsi_sec = of_graph_get_remote_node(dsi->dev.of_node, 1, -1);

	if (dsi_sec) {
		dev_notice(dev, "Using Dual-DSI\n");

		const struct mipi_dsi_device_info info = { "S6E3HA3", 0,
							   dsi_sec };

		dev_notice(dev, "Found second DSI `%s`\n", dsi_sec->name);

		dsi_sec_host = of_find_mipi_dsi_host_by_node(dsi_sec);
		of_node_put(dsi_sec);
		if (!dsi_sec_host) {
			return dev_err_probe(dev, -EPROBE_DEFER,
					     "Cannot get secondary DSI host\n");
		}

		ctx->dsi[1] =
			mipi_dsi_device_register_full(dsi_sec_host, &info);
		if (IS_ERR(ctx->dsi[1])) {
			return dev_err_probe(dev, PTR_ERR(ctx->dsi[1]),
					     "Cannot get secondary DSI node\n");
		}

		dev_notice(dev, "Second DSI name `%s`\n", ctx->dsi[1]->name);
		mipi_dsi_set_drvdata(ctx->dsi[1], ctx);
	} else {
		dev_notice(dev, "Using Single-DSI\n");
	}

	ctx->dsi[0] = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	drm_panel_init(&ctx->panel, dev, &s6e3ha3_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = s6e3ha3_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	for (i = 0; i < ARRAY_SIZE(ctx->dsi); i++) {
		if (!ctx->dsi[i])
			continue;

		dev_notice(&ctx->dsi[i]->dev, "Binding DSI %d\n", i);

		ctx->dsi[i]->lanes = 4;
		ctx->dsi[i]->format = MIPI_DSI_FMT_RGB888;
		ctx->dsi[i]->mode_flags = MIPI_DSI_MODE_NO_EOT_PACKET |
						MIPI_DSI_CLOCK_NON_CONTINUOUS;

		ret = mipi_dsi_attach(ctx->dsi[i]);
		if (ret < 0) {
			drm_panel_remove(&ctx->panel);
			return dev_err_probe(dev, ret,
					     "Failed to attach to DSI%d\n", i);
		}
	}

	dev_notice(dev, "Panel probed successfully!\n");

	return 0;
}

static void s6e3ha3_remove(struct mipi_dsi_device *dsi)
{
	struct s6e3ha3 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	mipi_dsi_detach(ctx->dsi[0]);
	mipi_dsi_detach(ctx->dsi[1]);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id s6e3ha3_of_match[] = {
	{ .compatible = "samsung,s6e3ha3" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, s6e3ha3_of_match);

static struct mipi_dsi_driver s6e3ha3_driver = {
	.probe = s6e3ha3_probe,
	.remove = s6e3ha3_remove,
	.driver = {
		.name = "panel-samsung-s6e3ha3",
		.of_match_table = s6e3ha3_of_match,
	},
};
module_mipi_dsi_driver(s6e3ha3_driver);

MODULE_AUTHOR("David Wronek <davidwronek@gmail.com>");
MODULE_DESCRIPTION("DRM driver for Samsung S6E3HA3 dual DSI cmd mode panel");
MODULE_LICENSE("GPL");
