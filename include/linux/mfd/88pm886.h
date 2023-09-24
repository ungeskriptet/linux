/*
 * Marvell 88PM886 Interface
 *
 * Copyright (C) 2015 Marvell International Ltd.
 *  Yi Zhang <yizhang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * 88pm886 specific configuration: at present it's regulators and dvc part
 */

#ifndef __LINUX_MFD_88PM886_H
#define __LINUX_MFD_88PM886_H

#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/atomic.h>
#include <linux/reboot.h>
#include "88pm886-reg.h"

enum {
	PM886_ID_BUCK1 = 0,
	PM886_ID_BUCK2,
	PM886_ID_BUCK3,
	PM886_ID_BUCK4,
	PM886_ID_BUCK5,

	PM886_ID_BUCK_MAX = 5,
};

enum {
	PM886_ID_LDO1 = 0,
	PM886_ID_LDO2,
	PM886_ID_LDO3,
	PM886_ID_LDO4,
	PM886_ID_LDO5,
	PM886_ID_LDO6,
	PM886_ID_LDO7,
	PM886_ID_LDO8,
	PM886_ID_LDO9,
	PM886_ID_LDO10,
	PM886_ID_LDO11,
	PM886_ID_LDO12,
	PM886_ID_LDO13,
	PM886_ID_LDO14,
	PM886_ID_LDO15,
	PM886_ID_LDO16 = 15,

	PM886_ID_LDO_MAX = 16,
};

enum {
	PM886_ID_BUCK1_SLP = 0,
};

#endif /* __LINUX_MFD_88PM886_H */
