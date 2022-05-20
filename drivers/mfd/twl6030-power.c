
/*
 * TWL6030 power
 *
 * Copyright (C) 2016 Paul Kocialkowski <contact@paulk.fr>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/pm.h>
#include <linux/mfd/twl.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>

#define TWL6030_PHOENIX_DEV_ON		0x25

#define TWL6030_PHOENIX_APP_DEVOFF	(1 << 0)
#define TWL6030_PHOENIX_CON_DEVOFF	(1 << 1)
#define TWL6030_PHOENIX_MOD_DEVOFF	(1 << 2)

void twl6030_power_off(void)
{
	int err;

	err = twl_i2c_write_u8(TWL6030_MODULE_ID0, TWL6030_PHOENIX_APP_DEVOFF |
		TWL6030_PHOENIX_CON_DEVOFF | TWL6030_PHOENIX_MOD_DEVOFF,
		TWL6030_PHOENIX_DEV_ON);
	if (err)
		pr_err("TWL6030 Unable to power off\n");
}

static bool twl6030_power_use_poweroff(const struct twl4030_power_data *pdata,
	struct device_node *node)
{
	if (pdata && pdata->use_poweroff)
		return true;

	if (of_property_read_bool(node, "ti,system-power-controller"))
		return true;

	return false;
}

#ifdef CONFIG_OF
static const struct of_device_id twl6030_power_of_match[] = {
	{
		.compatible = "ti,twl6030-power",
	},
	{ },
};

MODULE_DEVICE_TABLE(of, twl6030_power_of_match);
#endif	/* CONFIG_OF */

static int twl6030_power_probe(struct platform_device *pdev)
{
	const struct twl4030_power_data *pdata = dev_get_platdata(&pdev->dev);
	struct device_node *node = pdev->dev.of_node;

	if (!pdata && !node) {
		dev_err(&pdev->dev, "Platform data is missing\n");
		return -EINVAL;
	}

	/* Board has to be wired properly to use this feature */
	if (twl6030_power_use_poweroff(pdata, node) && !pm_power_off)
		pm_power_off = twl6030_power_off;

	return 0;
}

static int twl6030_power_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver twl6030_power_driver = {
	.driver = {
		.name	= "twl6030_power",
		.of_match_table = of_match_ptr(twl6030_power_of_match),
	},
	.probe		= twl6030_power_probe,
	.remove		= twl6030_power_remove,
};

module_platform_driver(twl6030_power_driver);

MODULE_AUTHOR("Paul Kocialkowski <contact@paulk.fr>");
MODULE_DESCRIPTION("Power management for TWL6030");
MODULE_LICENSE("GPL");
