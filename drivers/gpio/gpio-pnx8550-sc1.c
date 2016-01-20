/*
 * PNX8550 Smartcard as GPIO.
 *
 * GPLv2.
 */

#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>

#include <scgpio.h>

static const char *pnx8550_scgpio_names[] = {
	"io", "aux1", "aux2", "clock", "reset",
};
static unsigned char pin_value = 0;
static unsigned char pin_input = (1 << PNX8550_SCGPIO_IO)
	| (1 << PNX8550_SCGPIO_AUX1) | (1 << PNX8550_SCGPIO_AUX2);

static int pnx8550_scgpio_get_presence(void)
{
	return PNX8550_SC1_MSR & PNX8550_SC1_MSR_PRESENCE;
}

static inline void pnx8550_scgpio_update_pin(unsigned gpio) {
	int input = pin_input & (1 << gpio);
	int value = pin_value & (1 << gpio);

	if (!(pin_value & (1 << PNX8550_SCGPIO_VCC_FLAG))) {
		// if VCC is disabled, pull all pins low. the interface chip
		// enforces that anyway.
		input = 0;
		value = 0;
	}

	switch (gpio) {
	case PNX8550_SCGPIO_IO:
		if (input) {
			PNX8550_SC1_UCR1 = PNX8550_SC1_UCR1_ENABLE;
		} else {
			if (value)
				PNX8550_SC1_UTRR = 1;
			else
				PNX8550_SC1_UTRR = 0;
			// only enable output after value has been set to avoid glitches
			PNX8550_SC1_UCR1 = PNX8550_SC1_UCR1_ENABLE
					| PNX8550_SC1_UCR1_IOTX;
		}
		break;

	case PNX8550_SCGPIO_AUX1:
		if (input)
			PNX8550_GPIO_SET_IN(PNX8550_GPIO_SC1_AUX1);
		else if (value)
			PNX8550_GPIO_SET_HIGH(PNX8550_GPIO_SC1_AUX1);
		else
			PNX8550_GPIO_SET_LOW(PNX8550_GPIO_SC1_AUX1);
		break;

	case PNX8550_SCGPIO_AUX2:
		if (input)
			PNX8550_GPIO_SET_IN(PNX8550_GPIO_SC1_AUX2);
		else if (value)
			PNX8550_GPIO_SET_HIGH(PNX8550_GPIO_SC1_AUX2);
		else
			PNX8550_GPIO_SET_LOW(PNX8550_GPIO_SC1_AUX2);
		break;

	case PNX8550_SCGPIO_CLK:
		if (value)
			PNX8550_SC1_CCR = PNX8550_SC1_CCR_CLK;
		else
			PNX8550_SC1_CCR = 0;
		break;

	case PNX8550_SCGPIO_RST:
		if (value)
			PNX8550_SC1_UCR2 |= PNX8550_SC1_UCR2_RST;
		else
			PNX8550_SC1_UCR2 &= ~PNX8550_SC1_UCR2_RST;
		// there is a race condition if the interrupt tries to disable VCC
		// concurrently: because of the read-modify-write above, we might
		// write back a value that has VCC enabled, even though it has been
		// cleared in between.
		// to compensate, turn off VCC (and set RESET low) if the card has
		// been yanked out in the meantime.
		if (!pnx8550_scgpio_get_presence())
			PNX8550_SC1_UCR2 = PNX8550_SC1_UCR2_SYNC;
		break;
	}
}

static inline int pnx8550_scgpio_get_pin(unsigned gpio) {
	switch (gpio) {
	case PNX8550_SCGPIO_IO:
		if (pin_input & (1 << PNX8550_SCGPIO_IO))
			return PNX8550_SC1_UTRR & 1;
		// pin is output. in that case it always reads back as zero,
		// regardless of the actual pin state. return the state we
		// are outputting instead; that's more useful.
		break;
	case PNX8550_SCGPIO_AUX1:
		return PNX8550_GPIO_DATA(PNX8550_GPIO_SC1_AUX1);
	case PNX8550_SCGPIO_AUX2:
		return PNX8550_GPIO_DATA(PNX8550_GPIO_SC1_AUX2);
	}

	// the rest is write-only. we just read back the value we're outputting.
	return pin_value & (1 << gpio);
}

static int pnx8550_scgpio_get_vcc(void)
{
	if (pin_value & PNX8550_SCGPIO_VCC_FLAG) {
		if (PNX8550_GPIO_DATA(PNX8550_GPIO_SC1_VCC))
			return 5;
		else
			return 3;
	} else
		return 0;
}

static void pnx8550_scgpio_set_vcc(int on, int volt)
{
	int pin;

	// only turn on VCC if the card is actually present. the interface chip
	// disables VCC anyway unless a card is present.
	if (on && pnx8550_scgpio_get_presence()) {
		// set voltage
		if (volt == 5)
			PNX8550_GPIO_SET_HIGH(PNX8550_GPIO_SC1_VCC);
		else // 3V
			PNX8550_GPIO_SET_LOW(PNX8550_GPIO_SC1_VCC);

		// turn power on (it may have been off before)
		PNX8550_SC1_UCR2 = PNX8550_SC1_UCR2_SYNC | PNX8550_SC1_UCR2_VCC;
		// record new VCC pin state
		pin_value |= 1 << PNX8550_SCGPIO_VCC_FLAG;
		// VCC is high. restore all other output pins to their previous
		// state.
		for (pin = PNX8550_SCGPIO_IO; pin < PNX8550_SCGPIO_RST; pin++)
			pnx8550_scgpio_update_pin(pin);
	} else {
		// cut power to the card
		PNX8550_SC1_UCR2 = PNX8550_SC1_UCR2_SYNC;
		// configure VCC to 3V just in case. 3V will never damage a 5V card;
		// the opposite might not be true.
		PNX8550_GPIO_SET_LOW(PNX8550_GPIO_SC1_VCC);

		// record new VCC pin state
		pin_value &= ~(1 << PNX8550_SCGPIO_VCC_FLAG);
		// VCC is low, so updating pins sets them low as well.
		for (pin = PNX8550_SCGPIO_IO; pin < PNX8550_SCGPIO_CLK; pin++)
			pnx8550_scgpio_update_pin(pin);
	}
}


static void pnx8550_scgpio_set(struct gpio_chip *chip,
			     unsigned gpio, int val)
{
	if (gpio >= chip->ngpio)
		BUG();

	if (pin_input & (1 << gpio))
		// pin not configured for output. ignore call.
		return;

	// record pin value; the physical pins are all set LOW when VCC is
	// removed.
	if (val)
		pin_value |= (1 << gpio);
	else
		pin_value &= ~(1 << gpio);

	// update physical pin state
	pnx8550_scgpio_update_pin(gpio);
}

static int pnx8550_scgpio_get(struct gpio_chip *chip, unsigned gpio)
{
	if (gpio >= chip->ngpio)
		BUG();

	return !!pnx8550_scgpio_get_pin(gpio);
}

static int pnx8550_scgpio_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	if (gpio >= chip->ngpio)
		BUG();

	switch (gpio) {
	case PNX8550_SCGPIO_IO:
	case PNX8550_SCGPIO_AUX1:
	case PNX8550_SCGPIO_AUX2:
		pin_input |= 1 << gpio;
		pnx8550_scgpio_update_pin(gpio);
		return 0;

	default:
		// all other pins are output-only
		return -EINVAL;
	}
}

static int pnx8550_scgpio_direction_output(struct gpio_chip *chip,
					 unsigned gpio, int value)
{
	if (gpio >= chip->ngpio)
		BUG();

	// all pins support output
	pin_input &= ~(1 << gpio);
	pnx8550_scgpio_update_pin(gpio);
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
	.owner            = THIS_MODULE,
};


// sets the IO voltage used for smartcard IO, either 3.0V, 5.0V or off.
// shown as 3.0V and 5.0V respectively to make it clear it isn't 3.3V.
// on store, can be abbreviated down to "3" or "5", or even "0".
static ssize_t voltage_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t size)
{
	int i = size;
	int volt;

	// strip any newline suffix that might be present
	while (i >= 1 && (buf[i - 1] == '\n' || buf[i - 1] == '\r'))
		i--;

	if (strcasecmp(buf, "off") == 0) {
		// special case: accept the word "off" for 0.0V
		volt = 0;
	} else {
		// for voltage "x", we accept "x", "xV", "x.0" and "x.0V".
		// strip the suffix.
		if (i >= 1 && (buf[i - 1] == 'v' || buf[i - 1] == 'V'))
			i--;
		if (i >= 2 && buf[i - 2] == '.' && buf[i - 1] == '0')
			i -= 2;
		// now a single digit must be left, and it must select either 3V, 5V
		// or 0V (off)
		if (i != 1)
			return -EINVAL;

		if (buf[0] == '3')
			volt = 3;
		else if (buf[0] == '5')
			volt = 5;
		else if (buf[0] == '0')
			volt = 0;
		else
			return -EINVAL;
	}

	// set voltage. this may or may not work, but the card might always get
	// yanked out at any time, disabling VCC as well.
	if (volt == 0)
		pnx8550_scgpio_set_vcc(0, 0);
	else
		pnx8550_scgpio_set_vcc(1, volt);
	return size;
}

static ssize_t voltage_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	int volt = pnx8550_scgpio_get_vcc();
	if (volt == 5)
		strcpy(buf, "off 3.0V [5.0V]\n");
	else if (volt == 3)
		strcpy(buf, "off [3.0V] 5.0V\n");
	else
		strcpy(buf, "[off] 3.0V 5.0V\n");
	return strlen(buf);
}

static DEVICE_ATTR(voltage, 0600, voltage_show, voltage_store);

static ssize_t presence_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	if (pnx8550_scgpio_get_presence())
		strcpy(buf, "present\n");
	else
		strcpy(buf, "empty\n");
	return strlen(buf);
}

static DEVICE_ATTR(presence, 0400, presence_show, NULL);


static irqreturn_t pnx8550_scgpio_int(int irq, void *dev_id)
{
	if (!pnx8550_scgpio_get_presence()) {
		printk(KERN_DEBUG "smartcard-gpio: card removed\n");
		// somebody removed the card, or shorted out its power supply; the
		// interface chip has shut down VCC to the card as a precaution.
		// set the pin to match, so that it's visible to userspace. also,
		// the interface IC will not allow pulling pins high again when
		// the card is reinserted, unless they have been pulled low before.
		pnx8550_scgpio_set_vcc(0, 0);
	}

	// always clear the IRQ; it might be reported as stuck otherwise.
	PNX8550_SC1_RER = PNX8550_SC1_RER_PRESENCE;
	PNX8550_SC1_INT_CLEAR = PNX8550_SC1_INT_FLAG;
	return IRQ_HANDLED;
}

// driver shutdown. turns off VCC to the card
static void pnx8550_smartcard_shutdown(struct platform_device *pdev)
{
	// turn VCC off. uses pnx8550_scgpio_set_vcc to get its side-effects,
	// such as setting all other pins to LOW.
	pnx8550_scgpio_set_vcc(0, 0);

	// clear any remaining interrupts
	PNX8550_SC1_RER = PNX8550_SC1_RER_PRESENCE;
	PNX8550_SC1_INT_ENABLE = 0;
	PNX8550_SC1_INT_CLEAR = PNX8550_SC1_INT_FLAG;

	// disable the Smartcard 1 module
	PNX8550_SC1_UCR1 = 0;

	// release our IRQ, disabling it in the process
	free_irq(PNX8550_SC1_IRQ, &pnx8550_scgpio_chip);

	// clear remaining interrupts again (in case there was one in between)
	PNX8550_SC1_INT_ENABLE = 0;
	PNX8550_SC1_INT_CLEAR = PNX8550_SC1_INT_FLAG;
}

static void mark_output_only(int pin) {
	// if this fails, the pins will default to input. that doesn't cause any
	// problems, except that userspace has to set them to output itself.
	if (gpio_request(pnx8550_scgpio_chip.base + pin,
			"mark pin as output-only") == 0) {
		gpio_direction_output(pnx8550_scgpio_chip.base + pin, 0);
		gpio_free(pnx8550_scgpio_chip.base + pin);
	}
}

// driver initialization. uses GPIOLIB to reserve the GPIOs, but doesn't
// otherwise use it and accesses the hardware registers directly.
static int __devinit pnx8550_smartcard_probe(struct platform_device *pdev)
{
	int res = 0;
	int *base = pdev->dev.platform_data;

	// reserve the pins. the normal GPIO driver refuses to mess with the
	// AUX1/2 pins to avoid upsetting (non-GPIO) smartcard operations
	res = gpio_request(*base + 0, "smartcard GPIO");
	if (res) goto err0;
	res = gpio_request(*base + 1, "smartcard GPIO");
	if (res) goto err1;
	res = gpio_request(*base + 2, "smartcard GPIO");
	if (res) goto err2;
	// make AUX1/2 pins actively driven both ways when output, to match IO.
	// set VCC selection to push-pull too because it is output-only anyway.
	PNX8550_GPIO_MODE_PUSHPULL(*base + 0);
	PNX8550_GPIO_MODE_PUSHPULL(*base + 1);
	PNX8550_GPIO_MODE_PUSHPULL(*base + 2);

	// enable the Smartcard 1 module
	PNX8550_SC1_UCR1 = PNX8550_SC1_UCR1_ENABLE;
	// disable VCC. this guarantees that everything is low, and the call
	// also sets operation mode to synchronous
	pnx8550_scgpio_set_vcc(0, 0);

	// enable interrupts on card insertion / removal
	res = request_irq(PNX8550_SC1_IRQ, pnx8550_scgpio_int, 0,
			     pnx8550_scgpio_chip.label, &pnx8550_scgpio_chip);
	if (res) goto err3;
	PNX8550_SC1_RER = PNX8550_SC1_RER_PRESENCE;
	PNX8550_SC1_INT_CLEAR = PNX8550_SC1_INT_FLAG;
	PNX8550_SC1_INT_ENABLE = PNX8550_SC1_INT_FLAG;

	res = device_create_file(&pdev->dev, &dev_attr_voltage);
	if (res) goto err4;
	res = device_create_file(&pdev->dev, &dev_attr_presence);
	if (res) goto err5;

	res = gpiochip_add(&pnx8550_scgpio_chip);
	if (res)
		goto err6;

	// configure output-only GPIOs for output so that GPIOLIB knows about that
	mark_output_only(PNX8550_SCGPIO_CLK);
	mark_output_only(PNX8550_SCGPIO_RST);
	return 0;

err6:
	device_remove_file(&pdev->dev, &dev_attr_presence);
err5:
	device_remove_file(&pdev->dev, &dev_attr_voltage);
err4:
	free_irq(PNX8550_SC1_IRQ, &pnx8550_scgpio_chip);
err3:
	gpio_free(*base + 2);
err2:
	gpio_free(*base + 1);
err1:
	gpio_free(*base + 0);
err0:
	return res;
}

// driver shutdown. shuts down VCC to the card and formally releases the GPIOs
static int __devexit pnx8550_smartcard_remove(struct platform_device *pdev)
{
	int *base = pdev->dev.platform_data;
	int res;
	int pin;
	
	// de-register the chip. fails if it is still in use; the module cannot
	// be unloaded in that case.
	res = gpiochip_remove(&pnx8550_scgpio_chip);
	if (res)
		return res;
	
	// remove sysfs files
	device_remove_file(&pdev->dev, &dev_attr_voltage);
	device_remove_file(&pdev->dev, &dev_attr_presence);

	// disable VCC and release IRQ
	pnx8550_smartcard_shutdown(pdev);

	// release GPIOs
	gpio_free(*base + 0);
	gpio_free(*base + 1);
	gpio_free(*base + 2);

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
