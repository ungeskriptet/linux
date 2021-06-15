// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2021, The Linux Foundation. All rights reserved.

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>

#include "clk-regmap.h"
#include "clk-alpha-pll.h"

enum alpha_pll_type {
	TYPE_DEFAULT,
	TYPE_MSM8953_APCC,
	TYPE_SDM632_CCI,
	NUM_PLL_TYPES,
};

static const u8 alpha_pll_msm8953_regs[PLL_OFF_MAX_REGS] = {
	[PLL_OFF_L_VAL]		= 0x08,
	[PLL_OFF_ALPHA_VAL]	= 0x10,
	[PLL_OFF_USER_CTL]	= 0x18,
	[PLL_OFF_CONFIG_CTL]	= 0x20,
	[PLL_OFF_CONFIG_CTL_U]	= 0x24,
	[PLL_OFF_STATUS]	= 0x28,
	[PLL_OFF_TEST_CTL]	= 0x30,
	[PLL_OFF_TEST_CTL_U]	= 0x34,
};

static const struct clk_alpha_pll alpha_pll_types[NUM_PLL_TYPES] = {
	[TYPE_DEFAULT] = {
		.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	},
	[TYPE_MSM8953_APCC] = {
		.regs = alpha_pll_msm8953_regs,
	},
	[TYPE_SDM632_CCI] = {
		.flags = SUPPORTS_DYNAMIC_UPDATE,
		.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	}
};

static const struct alpha_pll_config *alpha_pll_conf[NUM_PLL_TYPES] = {
	[TYPE_MSM8953_APCC] = &(struct alpha_pll_config) {
		.config_ctl_val		= 0x200d4828,
		.config_ctl_hi_val	= 0x6,
		.test_ctl_val		= 0x1c000000,
		.test_ctl_hi_val	= 0x4000,
		.main_output_mask	= BIT(0),
		.early_output_mask	= BIT(3),
	},
	[TYPE_SDM632_CCI] = &(struct alpha_pll_config) {
		.config_ctl_val		= 0x4001055b,
		.early_output_mask	= BIT(3),
	}
};

static const struct of_device_id qcom_alphapll_match_table[] = {
	{ .compatible = "qcom,alpha-pll", (void*) TYPE_DEFAULT },
	{ .compatible = "qcom,msm8953-apcc-alpha-pll", (void*) TYPE_MSM8953_APCC },
	{ .compatible = "qcom,sdm632-cci-alpha-pll", (void*) TYPE_SDM632_CCI },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_alphapll_match_table);

static const struct regmap_config alphapll_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x34,
	.fast_io	= true,
};

static int qcom_alpha_pll_get_vco_ranges(struct device *dev,
					struct clk_alpha_pll *apll)
{
	struct pll_vco *ranges;
	u32 data[4*3];
	int elems, i;

	if (!of_property_read_bool(dev->of_node, "qcom,vco-ranges"))
		return 0;

	elems = of_property_read_variable_u32_array(dev->of_node,
						    "qcom,vco-ranges", data,
						    3, ARRAY_SIZE(data));
	if (elems < 0)
		return elems;
	if (elems % 3)
		return -EINVAL;

	ranges = devm_kzalloc(dev, elems / 3 * sizeof(*ranges), GFP_KERNEL);
	if (!ranges)
		return -ENOMEM;

	for (i = 0; i < elems / 3; i++) {
		u32 *item = &data[3 * i];

		if (item[0] > 3 || item[1] > item[2])
			return -EINVAL;

		ranges[i].val = item[0];
		ranges[i].min_freq = item[1];
		ranges[i].max_freq = item[2];
	}

	apll->vco_table = ranges;
	apll->num_vco = elems / 3;

	return 0;
}

struct clk_ops msm8953_alpha_pll_ops;

static int qcom_alpha_pll_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct device *dev = &pdev->dev;
	void __iomem *base;
	struct regmap *regmap;
	struct clk_alpha_pll *apll;
	enum alpha_pll_type type;
	struct clk_init_data init = {
		.num_parents = 1,
		.ops = &clk_alpha_pll_ops,
		/*
		 * rather than marking the clock critical and forcing the clock
		 * to be always enabled, we make sure that the clock is not
		 * disabled: the firmware remains responsible of enabling this
		 * clock (for more info check the commit log)
		 */
		.flags = CLK_IGNORE_UNUSED,
	};
	int ret;
	struct clk_parent_data pdata = { .index = 0 };

	type = (enum alpha_pll_type) of_device_get_match_data(dev);
	if (type >= NUM_PLL_TYPES)
		return -EINVAL;

	apll = devm_kzalloc(dev, sizeof(*apll), GFP_KERNEL);
	if (!apll)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap(dev, res->start, resource_size(res));
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = devm_regmap_init_mmio(&pdev->dev, base, &alphapll_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	if (of_property_read_string_index(dev->of_node, "clock-output-names",
					  0, &init.name))
		return -ENODEV;

	init.parent_data = &pdata;

	*apll = alpha_pll_types[type];
	apll->clkr.hw.init = &init;

	ret = qcom_alpha_pll_get_vco_ranges(dev, apll);
	if (ret < 0) {
		dev_err(dev, "failed to parse vco ranges: %d\n", ret);
		return ret;
	};

	if (alpha_pll_conf[type])
		clk_alpha_pll_configure(apll, regmap, alpha_pll_conf[type]);

	ret = devm_clk_register_regmap(dev, &apll->clkr);
	if (ret) {
		dev_err(dev, "failed to register regmap clock: %d\n", ret);
		return ret;
	}

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get,
					   &apll->clkr.hw);
}

static struct platform_driver qcom_alphapll_driver = {
	.probe		= qcom_alpha_pll_probe,
	.driver		= {
		.name	= "qcom-alpha-pll",
		.of_match_table = qcom_alphapll_match_table,
	},
};
module_platform_driver(qcom_alphapll_driver);

MODULE_DESCRIPTION("QCOM Alpha PLL Clock Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:qcom-alphapll");
