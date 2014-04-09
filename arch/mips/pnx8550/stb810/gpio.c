/*
 * GPIO <-> IRQ mapping stub for PNX8550.
 * 
 * Public domain.
 * 
 */

#include <linux/module.h>
#include <linux/gpio.h>

// GPIOLIB can handle this. forward to it.
extern int __gpio_to_irq(unsigned gpio);
int gpio_to_irq(unsigned gpio) {
	return __gpio_to_irq(gpio);
}

// GPIOLIB doesn't provide this so let's hope it isn't needed.
int irq_to_gpio(unsigned irq) {
	return -1;
}

EXPORT_SYMBOL(gpio_to_irq);
EXPORT_SYMBOL(irq_to_gpio);
