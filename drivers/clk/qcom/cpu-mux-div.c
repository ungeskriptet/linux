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

#include "clk-regmap-mux-div.h"

enum {
	VARPLL_INDEX,
	SAFE_INDEX,
	MAX_PARENTS,
};

struct cpu_mux_div {
	struct clk_regmap_mux_div md;
	struct notifier_block pll_nb;
	struct notifier_block cmd_nb;
	struct clk_hw *variable_pll;
	bool safe_parent_enabled;
	u32 parent_map[MAX_PARENTS];
	u32 safe_div;
};

const struct clk_parent_data mux_div_parent_data[MAX_PARENTS] = {
	[VARPLL_INDEX] = { .index = VARPLL_INDEX },
	[SAFE_INDEX] = { .index = SAFE_INDEX },
};

static int variable_pll_notifier(struct notifier_block *nb,
				unsigned long event,
				void *data)
{
	struct cpu_mux_div *cmd = container_of(nb, struct cpu_mux_div, pll_nb);
	struct clk_hw *hw = &cmd->md.clkr.hw;
	struct clk_hw *safe_parent = clk_hw_get_parent_by_index(hw, SAFE_INDEX);

	if (event == PRE_RATE_CHANGE) {
		if (cmd->md.src == cmd->parent_map[SAFE_INDEX] ||
		    cmd->safe_parent_enabled)
			return NOTIFY_OK;

		cmd->safe_parent_enabled = true;
		clk_prepare_enable(safe_parent->clk);
		mux_div_set_src_div(&cmd->md, cmd->parent_map[SAFE_INDEX], cmd->safe_div);
	}

	return NOTIFY_OK;
}

static int cpu_mux_div_notifier(struct notifier_block *nb,
				unsigned long event,
				void *data)
{
	struct cpu_mux_div *cmd = container_of(nb, struct cpu_mux_div, cmd_nb);
	struct clk_hw *hw = &cmd->md.clkr.hw;
	struct clk_hw *safe_parent = clk_hw_get_parent_by_index(hw, SAFE_INDEX);

	if (event == POST_RATE_CHANGE) {
		if (!cmd->safe_parent_enabled)
			return NOTIFY_OK;

		cmd->safe_parent_enabled = false;
		clk_disable_unprepare(safe_parent->clk);
		mux_div_set_src_div(&cmd->md, cmd->md.src, cmd->md.div);
	}

	return NOTIFY_OK;
}

static const struct regmap_config mux_div_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x10,
	.fast_io	= true,
};

static int qcom_cpu_mux_div_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct device *dev = &pdev->dev;
	void __iomem *base;
	struct regmap *regmap;
	struct cpu_mux_div *cmd;
	struct clk_init_data init = { 0 };
	int ret, nparents;

	nparents = of_property_count_u32_elems(dev->of_node, "qcom,mux-values");
	if (nparents < 1 || nparents > MAX_PARENTS)
		return -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap(dev, res->start, resource_size(res));
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = devm_regmap_init_mmio(&pdev->dev, base, &mux_div_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	cmd = devm_kzalloc(dev, sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	init = (struct clk_init_data) {
		.num_parents = nparents,
		.parent_data = mux_div_parent_data,
		.ops = &clk_regmap_mux_div_ops,
		.flags = CLK_IS_CRITICAL | CLK_SET_RATE_PARENT
	},

	*cmd = (struct cpu_mux_div) {
		.md = {
			.hid_width = 5,
			.src_width = 3,
			.src_shift = 8,
			.parent_map = cmd->parent_map,
			.clkr = {
				.enable_reg = 0x08,
				.enable_mask = BIT(0),
				.hw.init = &init,
			}
		}
	};

	if (of_property_read_string_index(dev->of_node, "clock-output-names",
					  0, &init.name))
		return -ENODEV;

	ret = of_property_read_u32_array(dev->of_node, "qcom,mux-values",
				         cmd->parent_map, nparents);
	if (ret)
		return ret;

	ret = of_property_read_u32(dev->of_node, "qcom,safe-div", &cmd->safe_div);
	if (ret)
		return ret;

	ret = devm_clk_register_regmap(dev, &cmd->md.clkr);
	if (ret) {
		dev_err(dev, "failed to register regmap clock: %d\n", ret);
		return ret;
	}

	if (nparents > SAFE_INDEX) {
		struct clk_hw *hw;

		hw = clk_hw_get_parent_by_index(&cmd->md.clkr.hw, VARPLL_INDEX);
		if (!hw)
			return -EPROBE_DEFER;

		cmd->pll_nb.notifier_call = variable_pll_notifier;
		cmd->cmd_nb.notifier_call = cpu_mux_div_notifier;
		clk_notifier_register(hw->clk, &cmd->pll_nb);
		clk_notifier_register(cmd->md.clkr.hw.clk, &cmd->cmd_nb);

		if (clk_hw_get_parent_index(hw) == SAFE_INDEX) {
			mux_div_set_src_div(&cmd->md, cmd->parent_map[SAFE_INDEX], cmd->safe_div);
			cmd->md.div = cmd->safe_div;
		}
	}

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get,
					   &cmd->md.clkr.hw);
}

static const struct of_device_id qcom_cpu_mux_div_match_table[] = {
	{ .compatible = "qcom,cpu-mux-div-clock" },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_cpu_mux_div_match_table);

static struct platform_driver qcom_cpu_mux_div_driver = {
	.probe		= qcom_cpu_mux_div_probe,
	.driver		= {
		.name	= "qcom-cpu-mux-div",
		.of_match_table = qcom_cpu_mux_div_match_table,
	},
};
module_platform_driver(qcom_cpu_mux_div_driver);

MODULE_DESCRIPTION("QCOM CPU Mux-Divider Clock Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:qcom-cpu-mux-div");
