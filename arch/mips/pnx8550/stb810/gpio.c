/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Maxime Bizon <mbizon@freebox.fr>
 * Copyright (C) 2008 Florian Fainelli <florian@openwrt.org>
 */

#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/leds.h>

#include <gpio.h>

// GPIO functions:
// 0~2: used for bootscript select and pulled low externally.
// 3, 6~11: setting low causes IRQ55 oops, ie. kernel panic.
// 12: standby. it's output, but userspace shouldn't mess with it.
// 16~17: configured as primary function, not GPIO.
// 20~22: PT6955 frontpanel control.
// 44: blue CPU LED.
// 46~47: Smartcard 1 AUX1~2. GPIO, but used by smartcard stuff.
// 45: Smartcard 1 VCC level (0=3V, 1=5V). used by smartcard stuff.
// 52: SW1.1 boot mode selection, driven externally.
// 56, 60: red & green CPU LED.
// 43: CON10.1 (3.3V bidirectional)
// 57: junction of R1024/5 near smartcard slot (3.3V bidirectional)
static const char *pnx8550_gpio_names[] = {
	"boot0", "boot1","boot2",NULL, NULL,      NULL,   NULL,    NULL,    /* 0~7 */
	 NULL,    NULL,   NULL,  NULL,"standby",  NULL,   NULL,    NULL,    /* 8~15 */
	"res1",  "res2",  NULL,  NULL,"fpdata",  "fpclk","fpstb",  NULL,    /* 16~23 */
	 NULL,    NULL,   NULL,  NULL, NULL,      NULL,   NULL,    NULL,    /* 24~31 */
	 NULL,    NULL,   NULL,  NULL, NULL,      NULL,   NULL,    NULL,    /* 32~39 */
	 NULL,    NULL,   NULL,  NULL,"cpu-blue","scvcc","scaux1","scaux2", /* 40~47 */
	 NULL,    NULL,   NULL,  NULL,"bootmode", NULL,   NULL,    NULL,    /* 48~55 */
	"cpu-red",NULL,   NULL,  NULL,"cpu-green",NULL,   NULL,    NULL,    /* 56~63 */
};

#define IN 1
#define OUT 2
#define INOUT 3
#define SYS 0
static const char pnx8550_gpio_config[] = {
	IN,   IN,   IN,   IN,   INOUT,INOUT,IN,   IN,    /* 0~7 */
	IN,   IN,   IN,   IN,   SYS,  INOUT,INOUT,INOUT, /* 8~15 */
	IN,   IN,   INOUT,INOUT,OUT,  OUT,  OUT,  INOUT, /* 16~23 */
	INOUT,INOUT,INOUT,INOUT,INOUT,INOUT,INOUT,INOUT, /* 24~31 */
	INOUT,INOUT,INOUT,INOUT,INOUT,INOUT,INOUT,INOUT, /* 32~39 */
	INOUT,INOUT,INOUT,INOUT,OUT,  SYS,  SYS,  SYS,   /* 40~47 */
	INOUT,INOUT,INOUT,INOUT,IN,   INOUT,INOUT,INOUT, /* 48~55 */
	OUT,  INOUT,INOUT,INOUT,OUT,  INOUT,INOUT,INOUT, /* 56~63 */
};

static void pnx8550_gpio_set(struct gpio_chip *chip,
			     unsigned gpio, int val)
{
	if (gpio >= chip->ngpio)
		BUG();
	
	if (PNX8550_GPIO_DIR(gpio) == 0)
		// pin not configured for output. ignore call.
		return;
	if (!(pnx8550_gpio_config[gpio] & OUT))
		// board configuration doesn't like output to that pin. ignore.
		// this also avoids setting the STANDBY pin, which is output
		// but system use only.
		return;

	if (val)
		PNX8550_GPIO_SET_HIGH(gpio);
	else
		PNX8550_GPIO_SET_LOW(gpio);
}

static int pnx8550_gpio_get(struct gpio_chip *chip, unsigned gpio)
{
	return !!PNX8550_GPIO_DATA(gpio);
}

static int pnx8550_gpio_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	if (gpio >= chip->ngpio)
		BUG();

	if (!(pnx8550_gpio_config[gpio] & IN))
		// cannot configure that pin to input
		return -EINVAL;

	PNX8550_GPIO_SET_IN(gpio);
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
		PNX8550_GPIO_SET_HIGH(gpio);
	else
		PNX8550_GPIO_SET_LOW(gpio);
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

static struct gpio_led leds[] = {
	{
		.name		= "heartbeat",
		.active_low	= 1,
		.default_trigger	= "heartbeat",
		.gpio		= PNX8550_GPIO_CPU_BLUE,
	}, {
		.name		= "wlan-assoc",
		.active_low	= 1,
		.default_trigger	= "phy0assoc",
		.gpio		= PNX8550_GPIO_CPU_GREEN,
	}, {
		.name		= "wlan-tx",
		.active_low	= 1,
		.default_trigger	= "phy0tx",
		.gpio		= PNX8550_GPIO_CPU_RED,
	}
};

static struct gpio_led_platform_data led_data = {
	.num_leds	= ARRAY_SIZE(leds),
	.leds		= leds,
};

static struct platform_device led_device = {
	.name		= "leds-gpio",
	.id			= -1,
	.dev.platform_data	= &led_data,
};

static int pnx8550_frontpanel_base = PNX8550_GPIO_PT6955_DATA;

static struct platform_device frontpanel_device = {
	.name		= "frontpanel",
	.id			= -1,
	.dev.platform_data	= &pnx8550_frontpanel_base,
};

int __init pnx8550_gpio_init(void)
{
	int res, pin;
	
	// force input-only pins to input in case they have been misconfigured. doesn't go
	// through GPIOLIB because we want that configuration right even if GPIOLIB fails
	// to register the chip. GPIOLIB's default for pins is input anyway, so no need to
	// tell it.
	for (pin = 0; pin < PNX8550_GPIO_COUNT; pin++)
		if (pnx8550_gpio_config[pin] == IN)
			PNX8550_GPIO_SET_IN(pin);
	// switch off the red CPU led to signal that linux has booted
	PNX8550_GPIO_SET_HIGH(PNX8550_GPIO_CPU_RED);
	
	res = gpiochip_add(&pnx8550_gpio_chip);
	if (res == 0) {
		// try to register LED triggers
		for (pin = 0; pin < ARRAY_SIZE(leds); pin++)
			leds[pin].gpio += pnx8550_gpio_chip.base;
		platform_device_register(&led_device);
		// register frontpanel display
		pnx8550_frontpanel_base += pnx8550_gpio_chip.base;
		platform_device_register(&frontpanel_device);
	}
	return res;
}
subsys_initcall(pnx8550_gpio_init);
