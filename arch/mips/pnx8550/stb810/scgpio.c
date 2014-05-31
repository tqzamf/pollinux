/*
 * PNX8550 Smartcard as GPIO.
 * 
 * Public domain.
 */

#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/leds.h>

#include <scgpio.h>

static const char *pnx8550_scgpio_names[] = {
	"sc-io", "sc-aux1", "sc-aux2", "sc-clock", "sc-reset", "sc-power", "sc-presence",
};
static unsigned char pin_value;
static unsigned char pin_dir = (1 << PNX8550_SCGPIO_CLK)
		| (1 << PNX8550_SCGPIO_RST) | (1 << PNX8550_SCGPIO_VCC);

static inline void pnx8550_scgpio_set_value(unsigned gpio, int val) {
	if (val) {
		switch (gpio) {
		case PNX8550_SCGPIO_IO:
			PNX8550_SC1_UTRR = 1;
			break;
		case PNX8550_SCGPIO_AUX1:
			PNX8550_GPIO_DATA(PNX8550_GPIO_SC1_AUX1) = PNX8550_GPIO_SET_HIGH(PNX8550_GPIO_SC1_AUX1);
			break;
		case PNX8550_SCGPIO_AUX2:
			PNX8550_GPIO_DATA(PNX8550_GPIO_SC1_AUX2) = PNX8550_GPIO_SET_HIGH(PNX8550_GPIO_SC1_AUX2);
			break;
		case PNX8550_SCGPIO_CLK:
			PNX8550_SC1_CCR = PNX8550_SC1_CCR_CLK;
			break;
		case PNX8550_SCGPIO_RST:
			PNX8550_SC1_UCR2 = PNX8550_SC1_UCR2_ASYNC | PNX8550_SC1_UCR2_VCC | PNX8550_SC1_UCR2_RST;
			break;
		}	
	} else {
		switch (gpio) {
		case PNX8550_SCGPIO_IO:
			PNX8550_SC1_UTRR = 0;
			break;
		case PNX8550_SCGPIO_AUX1:
			PNX8550_GPIO_DATA(PNX8550_GPIO_SC1_AUX1) = PNX8550_GPIO_SET_LOW(PNX8550_GPIO_SC1_AUX1);
			break;
		case PNX8550_SCGPIO_AUX2:
			PNX8550_GPIO_DATA(PNX8550_GPIO_SC1_AUX2) = PNX8550_GPIO_SET_LOW(PNX8550_GPIO_SC1_AUX2);
			break;
		case PNX8550_SCGPIO_CLK:
			PNX8550_SC1_CCR = 0;
			break;
		case PNX8550_SCGPIO_RST:
			// this isn't used to set RST when VCC is low, so at this point VCC must be enabled.
			PNX8550_SC1_UCR2 = PNX8550_SC1_UCR2_ASYNC | PNX8550_SC1_UCR2_VCC;
			break;
		}	
	}
}

static inline int pnx8550_scgpio_get_value(unsigned gpio) {
	switch (gpio) {
	case PNX8550_SCGPIO_IO:
		if (pin_value & (1 << PNX8550_SCGPIO_IO))
			// pin is output. in that case it always reads back as zero,
			// regardless of the actual pin state. return the state we
			// are outputting instead; that's more useful.
			break;
		return PNX8550_SC1_UTRR & 1;
	case PNX8550_SCGPIO_AUX1:
		return PNX8550_GPIO_DATA(PNX8550_GPIO_SC1_AUX1) & PNX8550_GPIO_DATA_MASK(PNX8550_GPIO_SC1_AUX1);
	case PNX8550_SCGPIO_AUX2:
		return PNX8550_GPIO_DATA(PNX8550_GPIO_SC1_AUX2) & PNX8550_GPIO_DATA_MASK(PNX8550_GPIO_SC1_AUX2);
	case PNX8550_SCGPIO_PRESENCE:
		return PNX8550_SC1_MSR & PNX8550_SC1_MSR_PRESENCE;
	}
	
	// the rest is write-only. we just read back the value we're outputting.
	return pin_value & (1 << gpio);
}

static inline void pnx8550_scgpio_set_direction(unsigned gpio, int dir) {
	if (dir) {
		switch (gpio) {
		case PNX8550_SCGPIO_IO:
			PNX8550_SC1_UCR1 = PNX8550_SC1_UCR1_ENABLE | PNX8550_SC1_UCR1_IOTX;
			break;
		case PNX8550_SCGPIO_AUX1:
		case PNX8550_SCGPIO_AUX2:
			// nothing to do; these have already been set to output by setting their value
			break;
		}	
	} else {
		switch (gpio) {
		case PNX8550_SCGPIO_IO:
			PNX8550_SC1_UCR1 = PNX8550_SC1_UCR1_ENABLE;
			break;
		case PNX8550_SCGPIO_AUX1:
			PNX8550_GPIO_DATA(PNX8550_GPIO_SC1_AUX1) = PNX8550_GPIO_SET_IN(PNX8550_GPIO_SC1_AUX1);
			break;
		case PNX8550_SCGPIO_AUX2:
			PNX8550_GPIO_DATA(PNX8550_GPIO_SC1_AUX2) = PNX8550_GPIO_SET_IN(PNX8550_GPIO_SC1_AUX2);
			break;
		}	
	}
}

static void pnx8550_scgpio_set(struct gpio_chip *chip,
			     unsigned gpio, int val)
{
	int pin;
	
	if (gpio >= chip->ngpio)
		BUG();
	
	if (!(pin_dir & (1 << gpio)))
		// pin not configured for output. ignore call.
		return;
	
	// record pin value; they are all set LOW when VCC is cleared
	if (val)
		pin_value |= (1 << gpio);
	else
		pin_value &= ~(1 << gpio);

	if (gpio == PNX8550_SCGPIO_VCC) {
		if (val) {
			PNX8550_SC1_UCR2 = PNX8550_SC1_UCR2_ASYNC | PNX8550_SC1_UCR2_VCC;
			// VCC is high. restore all other output pins to their previous state.
			for (pin = PNX8550_SCGPIO_IO; pin < PNX8550_SCGPIO_RST; pin++)
				if (pin_dir & (1 << pin))
					pnx8550_scgpio_set_value(pin, pin_value & (1 << pin));
		} else {
			PNX8550_SC1_UCR2 = PNX8550_SC1_UCR2_ASYNC;
			// VCC is low. pull all other putput pins low, too.
			for (pin = PNX8550_SCGPIO_IO; pin < PNX8550_SCGPIO_CLK; pin++)
				if (pin_dir & (1 << pin))
					pnx8550_scgpio_set_value(pin, 0);
		}
	}

	// if VCC is low, that's it; the value is only recorded for later use.
	if (pin_value & (1 << PNX8550_SCGPIO_VCC))
		if (gpio != PNX8550_SCGPIO_VCC)
			pnx8550_scgpio_set_value(gpio, val);
}

static int pnx8550_scgpio_get(struct gpio_chip *chip, unsigned gpio)
{
	return !!pnx8550_scgpio_get_value(gpio);
}

static int pnx8550_scgpio_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	if (gpio >= chip->ngpio)
		BUG();

	if ((gpio >= PNX8550_SCGPIO_CLK) && (gpio < PNX8550_SCGPIO_PRESENCE))
		// cannot configure that pin to input
		return -EINVAL;

	// record pin direction for use in _set()
	pin_dir &= ~(1 << gpio);

	pnx8550_scgpio_set_direction(gpio, 0);
	return 0;
}

static int pnx8550_scgpio_direction_output(struct gpio_chip *chip,
					 unsigned gpio, int value)
{
	if (gpio >= chip->ngpio)
		BUG();

	if (gpio == PNX8550_SCGPIO_PRESENCE)
		// cannot configure that pin to output
		return -EINVAL;

	// record pin direction for use in _set()
	pin_dir |= 1 << gpio;

	pnx8550_scgpio_set_value(gpio, value);
	pnx8550_scgpio_set_direction(gpio, 1);
	return 0;
}

static struct gpio_chip pnx8550_scgpio_chip = {
	.label            = "smartcard-gpio",
	.direction_input  = pnx8550_scgpio_direction_input,
	.direction_output = pnx8550_scgpio_direction_output,
	.get              = pnx8550_scgpio_get,
	.set              = pnx8550_scgpio_set,
	.base             = -1,
	.ngpio            = PNX8550_SCGPIO_COUNT,
	.names            = pnx8550_scgpio_names,
};


// driver shutdown. turns off VCC to the card
static void pnx8550_smartcard_shutdown(struct platform_device *pdev)
{
	// turn VCC off. uses pnx8550_scgpio_set to get all its side-effects,
	// such as setting all other pins to LOW.
	pnx8550_scgpio_set(&pnx8550_scgpio_chip, PNX8550_SCGPIO_VCC, 0);
	
	// disable the Smartcard 1 module
	PNX8550_SC1_UCR1 = 0;
}

static void mark_output_only(int pin) {
	gpio_request(pnx8550_scgpio_chip.base + pin, "mark that pin as output only");
	gpio_direction_output(pnx8550_scgpio_chip.base + pin, 0);
	gpio_free(pnx8550_scgpio_chip.base + pin);
}

// driver initialization. uses GPIOLIB to reserve the GPIOs, but doesn't
// otherwise use it and accesses the hardware registers directly
static int __devinit pnx8550_smartcard_probe(struct platform_device *pdev)
{
	int res = 0;
	int *base = pdev->dev.platform_data;

	// reserve the pins. the normal GPIO driver refuses to mess with the
	// AUX* pins to avoid upsetting (non-GPIO) smartcard operations
	gpio_request(*base + 0, "smartcard GPIO");
	gpio_request(*base + 1, "smartcard GPIO");
	
	// enable the Smartcard 1 module
	PNX8550_SC1_UCR1 = PNX8550_SC1_UCR1_ENABLE;
	// disable VCC. this guarantees that everything is low.
	pnx8550_scgpio_set(&pnx8550_scgpio_chip, PNX8550_SCGPIO_VCC, 0);
	
	res = gpiochip_add(&pnx8550_scgpio_chip);
	if (res) {
		printk(KERN_ERR "PNX8550 smartcard failed to register: %d", res);
		return res;
	} else {
		// configure output-only GPIOs for output so that GPIOLIB knows about that
		mark_output_only(PNX8550_SCGPIO_CLK);
		mark_output_only(PNX8550_SCGPIO_RST);
		mark_output_only(PNX8550_SCGPIO_VCC);
	}
	return 0;
}

// driver shutdown. shuts down VCC to the card and formally releases the GPIOs
static int __devexit pnx8550_smartcard_remove(struct platform_device *pdev)
{
	int *base = pdev->dev.platform_data;

	// disable VCC
	pnx8550_smartcard_shutdown(pdev);

	// release GPIOs
	gpio_free(*base + 0);
	gpio_free(*base + 1);

	return 0;
}

static struct platform_driver pnx8550_smartcard_driver = {
	.probe		= pnx8550_smartcard_probe,
	.remove		= __devexit_p(pnx8550_smartcard_remove),
	.shutdown   = pnx8550_smartcard_shutdown,
	.driver		= {
		.name	= "smartcard",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(pnx8550_smartcard_driver);

MODULE_AUTHOR("Matthias <tqzamf@gmail.com>");
MODULE_DESCRIPTION("PNX8550 smartcard GPIO driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:smartcard");
