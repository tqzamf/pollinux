/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Maxime Bizon <mbizon@freebox.fr>
 * Copyright (C) 2008 Florian Fainelli <florian@openwrt.org>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio.h>

#include <gpio.h>

const char *pnx8550_gpio_names[] = {
	"bootscript0","bootscript1","bootscript2",NULL,NULL,      NULL,NULL,NULL, /* 0~7 */
	NULL,         NULL,         NULL,         NULL,"standby", NULL,NULL,NULL, /* 8~15 */
	"reserved1",  "reserved2",  NULL,         NULL,NULL,      NULL,NULL,NULL, /* 16~23 */
	NULL,         NULL,         NULL,         NULL,NULL,      NULL,NULL,NULL, /* 24~31 */
	NULL,         NULL,         NULL,         NULL,NULL,      NULL,NULL,NULL, /* 32~39 */
	NULL,         NULL,         NULL,         NULL,"cpuled",  NULL,NULL,NULL, /* 40~47 */
	NULL,         NULL,         NULL,         NULL,"bootmode",NULL,NULL,NULL, /* 48~55 */
	NULL,         NULL,         NULL,         NULL,NULL,      NULL,NULL,NULL, /* 56~63 */
};

#define IN 1
#define OUT 2
#define INOUT 3
#define SYS 0
const char pnx8550_gpio_config[] = {
	IN,   IN,   IN,   INOUT,INOUT,INOUT,INOUT,INOUT, /* 0~7 */
	INOUT,INOUT,INOUT,INOUT,SYS,  INOUT,INOUT,INOUT, /* 8~15 */
	IN,   IN,   INOUT,INOUT,INOUT,INOUT,INOUT,INOUT, /* 16~23 */
	INOUT,INOUT,INOUT,INOUT,INOUT,INOUT,INOUT,INOUT, /* 24~31 */
	INOUT,INOUT,INOUT,INOUT,INOUT,INOUT,INOUT,INOUT, /* 32~39 */
	INOUT,INOUT,INOUT,INOUT,OUT,  INOUT,INOUT,INOUT, /* 40~47 */
	INOUT,INOUT,INOUT,INOUT,IN,   INOUT,INOUT,INOUT, /* 48~55 */
	INOUT,INOUT,INOUT,INOUT,INOUT,INOUT,INOUT,INOUT, /* 56~63 */
};

static void pnx8550_gpio_set(struct gpio_chip *chip,
			     unsigned gpio, int val)
{
	if (gpio >= chip->ngpio)
		BUG();
	
	if ((PNX8550_GPIO_DATA(gpio) & PNX8550_GPIO_DIR_MASK(gpio)) == 0)
		// pin not configured for output. ignore call.
		return;
	if (!(pnx8550_gpio_config[gpio] & OUT))
		// board configuration doesn't like output to that pin. ignore.
		// this avoids setting the STANDBY pin, which is output but system use only
		return;

	if (val)
		PNX8550_GPIO_DATA(gpio) = PNX8550_GPIO_SET_HIGH(gpio);
	else
		PNX8550_GPIO_DATA(gpio) = PNX8550_GPIO_SET_LOW(gpio);
}

static int pnx8550_gpio_get(struct gpio_chip *chip, unsigned gpio)
{
	return !!(PNX8550_GPIO_DATA(gpio) & PNX8550_GPIO_DATA_MASK(gpio));
}

static int pnx8550_gpio_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	if (gpio >= chip->ngpio)
		BUG();

	if (!(pnx8550_gpio_config[gpio] & IN))
		// cannot configure that pin to input
		return -EINVAL;

	PNX8550_GPIO_DATA(gpio) = PNX8550_GPIO_SET_IN(gpio);
	return 0;
}

static int pnx8550_gpio_direction_output(struct gpio_chip *chip,
					 unsigned gpio, int value)
{
	if (gpio >= chip->ngpio)
		BUG();

	if (!(pnx8550_gpio_config[gpio] & OUT))
		// cannot configure that pin to output
		return -EINVAL;

	if (value)
		PNX8550_GPIO_DATA(gpio) = PNX8550_GPIO_SET_HIGH(gpio);
	else
		PNX8550_GPIO_DATA(gpio) = PNX8550_GPIO_SET_LOW(gpio);
	return 0;
}

static struct gpio_chip pnx8550_gpio_chip = {
	.label            = "pnx8550-gpio",
	.direction_input  = pnx8550_gpio_direction_input,
	.direction_output = pnx8550_gpio_direction_output,
	.get              = pnx8550_gpio_get,
	.set              = pnx8550_gpio_set,
	.base             = 0,
	.ngpio            = PNX8550_GPIO_COUNT,
	.names            = pnx8550_gpio_names,
};

int __init pnx8550_gpio_init(void)
{
	int res, pin;
	
	// force input-only pins to input in case they were misconfigured. doesn't go through
	// GPIOLIB because we want that configuration right even if GPIOLOB fails to register
	// the chip. GPIOLOB's default for pins is input anyway, so no need to tell it.
	for (pin = 0; pin < PNX8550_GPIO_COUNT; pin++)
		if (pnx8550_gpio_config[pin] == IN)
			PNX8550_GPIO_DATA(pin) = PNX8550_GPIO_SET_IN(pin);
	
	res = gpiochip_add(&pnx8550_gpio_chip);
	if (res == 0) {
		// set the CPU LED GPIO low, thus enabling the LED. that way it lights up
		// as soon as Linux is running. go through GPIOLIB so that it remembers the
		// pin direction.
		gpio_request(pnx8550_gpio_chip.base + PNX8550_GPIO_CPULED, "cpu led boot indicator");
		gpio_direction_output(pnx8550_gpio_chip.base + PNX8550_GPIO_CPULED, 0);
		gpio_free(pnx8550_gpio_chip.base + PNX8550_GPIO_CPULED);
	}
	return res;
}
