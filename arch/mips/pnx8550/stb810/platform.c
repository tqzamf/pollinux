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

#include <int.h>
#include <usb.h>
#include <uart.h>

static struct platform_device cimax_device = {
	.name          = "cimax",
	.id            = -1,
};

static struct platform_device *pnx8550_stb810_platform_devices[] __initdata = {
	&cimax_device,
};

static int __init pnx8550_stb810_platform_init(void)
{
	return platform_add_devices(pnx8550_stb810_platform_devices,
			            ARRAY_SIZE(pnx8550_stb810_platform_devices));
}

arch_initcall(pnx8550_stb810_platform_init);
