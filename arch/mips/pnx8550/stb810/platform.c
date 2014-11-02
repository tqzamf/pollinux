/*
 * Platform device support for NXP PNX8550 STB810
 *
 * Copyright 2005, Embedded Alley Solutions, Inc
 *
 * Based on arch/mips/au1000/common/platform.c
 * Platform device support for Au1x00 SoCs.
 *
 * Copyright 2004, Matt Porter <mporter@kernel.crashing.org>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/resource.h>
#include <linux/serial.h>
#include <linux/serial_pnx8xxx.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>

#include <int.h>
#include <usb.h>
#include <uart.h>
#include <i2c.h>

static struct platform_device cimax_device = {
	.name          = "cimax",
	.id            = -1,
};

static struct platform_device pnx8550_ao1_device = {
	.name		= "pnx8550ao1",
	.id		= -1,
};

static struct platform_device pnx8550_spdo_device = {
	.name		= "pnx8550spdo",
	.id		= -1,
};

static struct platform_device pnx8550fb_device = {
	.name          = "pnx8550fb",
	.id            = -1,
};

static struct platform_device *pnx8550_stb810_platform_devices[] __initdata = {
	&cimax_device,
	&pnx8550_ao1_device,
	&pnx8550_spdo_device,
	&pnx8550fb_device,
};

static struct i2c_board_info pnx8550_ak4705_device __initconst = {
	I2C_BOARD_INFO("ak4705", 0x11) /* AK4705 SCART switch */
};

static int __init pnx8550_stb810_platform_init(void)
{
	int err;

	err = i2c_register_board_info(PNX8550_I2C_IP0105_BUS1,
			&pnx8550_ak4705_device, 1);
	if (err < 0)
		// cannot register AK4705
		return err;

	return platform_add_devices(pnx8550_stb810_platform_devices,
			            ARRAY_SIZE(pnx8550_stb810_platform_devices));
}

arch_initcall(pnx8550_stb810_platform_init);
