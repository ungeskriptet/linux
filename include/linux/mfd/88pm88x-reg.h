/*
 * Marvell 88PM88X registers
 *
 * Copyright (C) 2014 Marvell International Ltd.
 *  Yi Zhang <yizhang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_MFD_88PM88X_REG_H
#define __LINUX_MFD_88PM88X_REG_H
/*
 * This file is just used for the common registers,
 * which are shared by sub-clients
 */

/*--base page:--------------------------------------------------------------*/
#define PM88X_ID_REG			(0x0)

#define PM88X_STATUS1			(0x1)
#define PM88X_CHG_DET			(1 << 2)
#define PM88X_BAT_DET			(1 << 3)

#define PM88X_MISC_CONFIG1		(0x14)
#define PM88X_LONKEY_RST		(1 << 3)

#define PM88X_WDOG			(0x1d)

#define PM88X_LOWPOWER2			(0x21)
#define PM88X_LOWPOWER4			(0x23)

/* clk control register */
#define PM88X_CLK_CTRL1			(0x25)

/* gpio */
#define PM88X_GPIO_CTRL1		(0x30)
#define PM88X_GPIO0_VAL_MSK		(0x1 << 0)
#define PM88X_GPIO0_MODE_MSK		(0x7 << 1)
#define PM88X_GPIO1_VAL_MSK		(0x1 << 4)
#define PM88X_GPIO1_MODE_MSK		(0x7 << 5)
#define PM88X_GPIO1_SET_DVC		(0x2 << 5)

#define PM88X_GPIO_CTRL2		(0x31)
#define PM88X_GPIO2_VAL_MSK		(0x1 << 0)
#define PM88X_GPIO2_MODE_MSK		(0x7 << 1)

#define PM88X_GPIO_CTRL3		(0x32)

#define PM88X_GPIO_CTRL4		(0x33)
#define PM88X_GPIO5V_1_VAL_MSK		(0x1 << 0)
#define PM88X_GPIO5V_1_MODE_MSK		(0x7 << 1)
#define PM88X_GPIO5V_2_VAL_MSK		(0x1 << 4)
#define PM88X_GPIO5V_2_MODE_MSK		(0x7 << 5)

#define PM88X_BK_OSC_CTRL1		(0x50)
#define PM88X_BK_OSC_CTRL3		(0x52)

#define PM88X_RTC_ALARM_CTRL1		(0xd0)
#define PM88X_ALARM_WAKEUP		(1 << 4)
#define PM88X_USE_XO			(1 << 7)

#define PM88X_AON_CTRL2			(0xe2)
#define PM88X_AON_CTRL3			(0xe3)
#define PM88X_AON_CTRL4			(0xe4)
#define PM88X_AON_CTRL7			(0xe7)

/* 0xea, 0xeb, 0xec, 0xed are reserved by RTC */
#define PM88X_RTC_SPARE5		(0xee)
#define PM88X_RTC_SPARE6		(0xef)
/*-------------------------------------------------------------------------*/

/*--power page:------------------------------------------------------------*/

/*-------------------------------------------------------------------------*/

/*--gpadc page:------------------------------------------------------------*/

#define PM88X_GPADC_CONFIG1		(0x1)

#define PM88X_GPADC_CONFIG2		(0x2)
#define PM88X_GPADC0_MEAS_EN		(1 << 2)
#define PM88X_GPADC1_MEAS_EN		(1 << 3)
#define PM88X_GPADC2_MEAS_EN		(1 << 4)
#define PM88X_GPADC3_MEAS_EN		(1 << 5)

#define PM88X_GPADC_CONFIG3		(0x3)

#define PM88X_GPADC_CONFIG6		(0x6)
#define PM88X_GPADC_CONFIG8		(0x8)

#define PM88X_GPADC0_LOW_TH		(0x20)
#define PM88X_GPADC1_LOW_TH		(0x21)
#define PM88X_GPADC2_LOW_TH		(0x22)
#define PM88X_GPADC3_LOW_TH		(0x23)

#define PM88X_GPADC0_UPP_TH		(0x30)
#define PM88X_GPADC1_UPP_TH		(0x31)
#define PM88X_GPADC2_UPP_TH		(0x32)
#define PM88X_GPADC3_UPP_TH		(0x33)

#define PM88X_VBUS_MEAS1		(0x4A)
#define PM88X_GPADC0_MEAS1		(0x54)
#define PM88X_GPADC1_MEAS1		(0x56)
#define PM88X_GPADC2_MEAS1		(0x58)
#define PM88X_GPADC3_MEAS1		(0x5A)


/*--charger page:------------------------------------------------------------*/
#define PM88X_CHG_CONFIG1		(0x28)
#define PM88X_CHGBK_CONFIG6		(0x50)
/*-------------------------------------------------------------------------*/

/*--test page:-------------------------------------------------------------*/

/*-------------------------------------------------------------------------*/
#endif
