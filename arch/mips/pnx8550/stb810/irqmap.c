/*
 *  NXP STB810 board irqmap.
 *
 *  Author: MontaVista Software, Inc.
 *          source@mvista.com
 *
 *  Copyright 2005 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 */

#include <linux/init.h>
#include <int.h>
#include <gpio.h>

char pnx8550_irq_tab[][5] __initdata = {
  [8] = { -1, PNX8550_GPIO_INT_SATA, 0xff, 0xff, 0xff}, /* SATA Controller */
  [9] = { -1, PNX8550_GPIO_INT_USB, 0xff, 0xff, 0xff}, /* USB COntroller */
  [10] = { -1, PNX8550_GPIO_INT_ETHER, 0xff, 0xff, 0xff}, /* Ethernet Controller */
  [12] = { -1, PNX8550_GPIO_INT_MPCI, 0xff, 0xff, 0xff}, /* Mini PCI Slot */
};

static int gpio_set_irq_map(void)
{
	PNX8550_GPIO_IRQ_SOURCE(PNX8550_GPIO_INT_SATA) = PNX8550_GPIO_IRQ_SATA;
	PNX8550_GPIO_IRQ_SOURCE(PNX8550_GPIO_INT_USB) = PNX8550_GPIO_IRQ_USB;
	PNX8550_GPIO_IRQ_SOURCE(PNX8550_GPIO_INT_ETHER) = PNX8550_GPIO_IRQ_ETHER;
	PNX8550_GPIO_IRQ_SOURCE(PNX8550_GPIO_INT_MPCI) = PNX8550_GPIO_IRQ_MPCI;
	PNX8550_GPIO_IRQ_SOURCE(PNX8550_GPIO_INT_CIMAX) = PNX8550_GPIO_IRQ_CIMAX;
	return 0;
}

arch_initcall(gpio_set_irq_map);
