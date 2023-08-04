// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung S6E3HA3 dual DSI cmd mode panel driver
 *
 * Copyright (c) 2023 David Wronek <davidwronek@gmail.com>
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define DSI_NUM_MIN 1

#define mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, cmd, seq...)        \
		do {                                                 \
			mipi_dsi_dcs_write_seq(dsi0, cmd, seq);      \
			mipi_dsi_dcs_write_seq(dsi1, cmd, seq);      \
		} while (0)

struct panel_info {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi[2];
	const struct panel_desc *desc;
	enum drm_panel_orientation orientation;

	struct gpio_desc *reset_gpio;
	struct backlight_device *backlight;
	struct regulator *vdda, *vcca, *vddio;

	bool prepared;
};

struct panel_desc {
	unsigned int width_mm;
	unsigned int height_mm;

	unsigned int bpc;
	unsigned int lanes;
	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;

	const struct drm_display_mode *modes;
	unsigned int num_modes;
	const struct mipi_dsi_device_info dsi_info;
	int (*init_sequence)(struct panel_info *pinfo);

	bool is_dual_dsi;
	bool has_dcs_backlight;
};

static inline struct panel_info *to_panel_info(struct drm_panel *panel)
{
	return container_of(panel, struct panel_info, panel);
}

static int zte_s6e3ha3_init_sequence(struct panel_info *pinfo)
{
	struct mipi_dsi_device *dsi0 = pinfo->dsi[0];
	struct mipi_dsi_device *dsi1 = pinfo->dsi[1];

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

	return 0;
}

static const struct drm_display_mode zte_s6e3ha3_modes[] = {
	{
		.clock = (1440 + 280 + 32 + 132) * (2560 + 16 + 16 + 8) * 60 / 1000,
		.hdisplay = 1440,
		.hsync_start = 1440 + 280,
		.hsync_end = 1440 + 280 + 32,
		.htotal = 1440 + 280 + 32 + 132,
		.vdisplay = 2560,
		.vsync_start = 2560 + 16,
		.vsync_end = 2560 + 16 + 16,
		.vtotal = 2560 + 16 + 16 + 8,
	},
};

static const struct panel_desc zte_s6e3ha3_desc = {
	.modes = zte_s6e3ha3_modes,
	.num_modes = ARRAY_SIZE(zte_s6e3ha3_modes),
	.dsi_info = {
		.type = "ZTE S6E3HA3",
		.channel = 0,
		.node = NULL,
	},
	.width_mm = 68,
	.height_mm = 121,
	.bpc = 8,
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_NO_EOT_PACKET | MIPI_DSI_CLOCK_NON_CONTINUOUS,
	.init_sequence = zte_s6e3ha3_init_sequence,
	.is_dual_dsi = true,
};

static void s6e3ha3_reset(struct panel_info *pinfo)
{
	gpiod_set_value_cansleep(pinfo->reset_gpio, 0);
	usleep_range(2000, 3000);
	gpiod_set_value_cansleep(pinfo->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(pinfo->reset_gpio, 0);
	usleep_range(5000, 6000);
}

static int s6e3ha3_prepare(struct drm_panel *panel)
{
	struct panel_info *pinfo = to_panel_info(panel);
	int ret;

	if (pinfo->prepared)
		return 0;

	ret = regulator_enable(pinfo->vdda);
	if (ret) {
		dev_err(panel->dev, "failed to enable vdda regulator: %d\n", ret);
		return ret;
	}

	ret = regulator_enable(pinfo->vcca);
	if (ret) {
		dev_err(panel->dev, "failed to enable vcca regulator: %d\n", ret);
		return ret;
	}

	ret = regulator_enable(pinfo->vddio);
	if (ret) {
		dev_err(panel->dev, "failed to enable vddio regulator: %d\n", ret);
		return ret;
	}

	s6e3ha3_reset(pinfo);

	ret = pinfo->desc->init_sequence(pinfo);
	if (ret < 0) {
		regulator_disable(pinfo->vdda);
		regulator_disable(pinfo->vcca);
		regulator_disable(pinfo->vddio);
		dev_err(panel->dev, "failed to initialize panel: %d\n", ret);
		return ret;
	}

	pinfo->prepared = true;

	return 0;
}

static int s6e3ha3_disable(struct drm_panel *panel)
{
	struct panel_info *pinfo = to_panel_info(panel);
	struct mipi_dsi_device *dsi0 = pinfo->dsi[0];
	struct mipi_dsi_device *dsi1 = pinfo->dsi[1];
	int i, ret;

	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x51, 0x00);

	for (i = 0; i < DSI_NUM_MIN + pinfo->desc->is_dual_dsi; i++) {
		ret = mipi_dsi_dcs_set_display_off(pinfo->dsi[i]);
		if (ret < 0)
			dev_err(&pinfo->dsi[i]->dev, "failed to set display off: %d\n", ret);
	}
	msleep(50);

	for (i = 0; i < DSI_NUM_MIN + pinfo->desc->is_dual_dsi; i++) {
		ret = mipi_dsi_dcs_enter_sleep_mode(pinfo->dsi[i]);
		if (ret < 0)
			dev_err(&pinfo->dsi[i]->dev, "failed to enter sleep mode: %d\n", ret);
	}
	msleep(120);

	return 0;
}

static int s6e3ha3_unprepare(struct drm_panel *panel)
{
	struct panel_info *pinfo = to_panel_info(panel);

	if (!pinfo->prepared)
		return 0;

	gpiod_set_value_cansleep(pinfo->reset_gpio, 1);
	regulator_disable(pinfo->vdda);
	regulator_disable(pinfo->vddio);
	regulator_disable(pinfo->vcca);

	pinfo->prepared = false;

	return 0;
}

static void s6e3ha3_remove(struct mipi_dsi_device *dsi)
{
	struct panel_info *pinfo = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(pinfo->dsi[0]);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to detach from DSI0 host: %d\n", ret);

	if (pinfo->desc->is_dual_dsi) {
		ret = mipi_dsi_detach(pinfo->dsi[1]);
		if (ret < 0)
			dev_err(&pinfo->dsi[1]->dev, "failed to detach from DSI1 host: %d\n", ret);
		mipi_dsi_device_unregister(pinfo->dsi[1]);
	}

	drm_panel_remove(&pinfo->panel);
}

static int s6e3ha3_get_modes(struct drm_panel *panel,
			       struct drm_connector *connector)
{
	struct panel_info *pinfo = to_panel_info(panel);
	int i;

	for (i = 0; i < pinfo->desc->num_modes; i++) {
		const struct drm_display_mode *m = &pinfo->desc->modes[i];
		struct drm_display_mode *mode;

		mode = drm_mode_duplicate(connector->dev, m);
		if (!mode) {
			dev_err(panel->dev, "failed to add mode %ux%u@%u\n",
				m->hdisplay, m->vdisplay, drm_mode_vrefresh(m));
			return -ENOMEM;
		}

		mode->type = DRM_MODE_TYPE_DRIVER;
		if (i == 0)
			mode->type |= DRM_MODE_TYPE_PREFERRED;

		drm_mode_set_name(mode);
		drm_mode_probed_add(connector, mode);
	}

	connector->display_info.width_mm = pinfo->desc->width_mm;
	connector->display_info.height_mm = pinfo->desc->height_mm;
	connector->display_info.bpc = pinfo->desc->bpc;

	return pinfo->desc->num_modes;
}

static enum drm_panel_orientation s6e3ha3_get_orientation(struct drm_panel *panel)
{
	struct panel_info *pinfo = to_panel_info(panel);

	return pinfo->orientation;
}

static const struct drm_panel_funcs s6e3ha3_panel_funcs = {
	.disable = s6e3ha3_disable,
	.prepare = s6e3ha3_prepare,
	.unprepare = s6e3ha3_unprepare,
	.get_modes = s6e3ha3_get_modes,
	.get_orientation = s6e3ha3_get_orientation,
};

/*static int s6e3ha3_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static int s6e3ha3_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness_large(dsi, &brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return brightness;
}

static const struct backlight_ops s6e3ha3_bl_ops = {
	.update_status = s6e3ha3_bl_update_status,
	.get_brightness = s6e3ha3_bl_get_brightness,
};

static struct backlight_device *s6e3ha3_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 512,
		.max_brightness = 4095,
		.scale = BACKLIGHT_SCALE_NON_LINEAR,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &nt36523_bl_ops, &props);
}*/

static int s6e3ha3_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *dsi1;
	struct mipi_dsi_host *dsi1_host;
	struct panel_info *pinfo;
	const struct mipi_dsi_device_info *info;
	int i, ret;

	pinfo = devm_kzalloc(dev, sizeof(*pinfo), GFP_KERNEL);
	if (!pinfo)
		return -ENOMEM;

	pinfo->panel.prepare_prev_first = true;

	pinfo->vddio = devm_regulator_get(dev, "vdda");
	if (IS_ERR(pinfo->vddio))
		return dev_err_probe(dev, PTR_ERR(pinfo->vddio), "failed to get vddio regulator\n");

	pinfo->vddio = devm_regulator_get(dev, "vddio");
	if (IS_ERR(pinfo->vddio))
		return dev_err_probe(dev, PTR_ERR(pinfo->vddio), "failed to get vddio regulator\n");

	pinfo->vddio = devm_regulator_get(dev, "vcca");
	if (IS_ERR(pinfo->vddio))
		return dev_err_probe(dev, PTR_ERR(pinfo->vddio), "failed to get vddio regulator\n");

	pinfo->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(pinfo->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(pinfo->reset_gpio), "failed to get reset gpio\n");

	pinfo->desc = of_device_get_match_data(dev);
	if (!pinfo->desc)
		return -ENODEV;

	/* If the panel is dual dsi, register DSI1 */
	if (pinfo->desc->is_dual_dsi) {
		info = &pinfo->desc->dsi_info;

		dsi1 = of_graph_get_remote_node(dsi->dev.of_node, 1, -1);
		if (!dsi1) {
			dev_err(dev, "cannot get secondary DSI node.\n");
			return -ENODEV;
		}

		dsi1_host = of_find_mipi_dsi_host_by_node(dsi1);
		of_node_put(dsi1);
		if (!dsi1_host)
			return dev_err_probe(dev, -EPROBE_DEFER, "cannot get secondary DSI host\n");

		pinfo->dsi[1] = mipi_dsi_device_register_full(dsi1_host, info);
		if (!pinfo->dsi[1]) {
			dev_err(dev, "cannot get secondary DSI device\n");
			return -ENODEV;
		}
	}

	pinfo->dsi[0] = dsi;
	mipi_dsi_set_drvdata(dsi, pinfo);

	drm_panel_init(&pinfo->panel, dev, &s6e3ha3_panel_funcs, DRM_MODE_CONNECTOR_DSI);

	ret = of_drm_get_panel_orientation(dev->of_node, &pinfo->orientation);
	if (ret < 0) {
		dev_err(dev, "%pOF: failed to get orientation %d\n", dev->of_node, ret);
		return ret;
	}

	/*if (pinfo->desc->has_dcs_backlight) {
		pinfo->panel.backlight = s6e3ha3_create_backlight(dsi);
		if (IS_ERR(pinfo->panel.backlight))
			return dev_err_probe(dev, PTR_ERR(pinfo->panel.backlight),
					     "Failed to create backlight\n");
	} else {
		ret = drm_panel_of_backlight(&pinfo->panel);
		if (ret)
			return dev_err_probe(dev, ret, "Failed to get backlight\n");
	}*/

	drm_panel_add(&pinfo->panel);

	for (i = 0; i < DSI_NUM_MIN + pinfo->desc->is_dual_dsi; i++) {
		pinfo->dsi[i]->lanes = pinfo->desc->lanes;
		pinfo->dsi[i]->format = pinfo->desc->format;
		pinfo->dsi[i]->mode_flags = pinfo->desc->mode_flags;

		ret = mipi_dsi_attach(pinfo->dsi[i]);
		if (ret < 0)
			return dev_err_probe(dev, ret, "cannot attach to DSI%d host.\n", i);
	}

	return 0;
}

static const struct of_device_id s6e3ha3_of_match[] = {
	{
		.compatible = "samsung,s6e3ha3",
		.data = &zte_s6e3ha3_desc,
	},
	{},
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
MODULE_DESCRIPTION("DRM driver for Samsung S6E3HA3 dual DSI cmd mode panels");
MODULE_LICENSE("GPL");
