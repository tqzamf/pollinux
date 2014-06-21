/*
 *  STB810 GPIO pin definitions
 *
 *  Public domain.
 *
 */

#include <asm-generic/gpio.h>

#ifndef __PNX8550_GPIO_H
#define __PNX8550_GPIO_H

// redirect though GPIOLIB
#define gpio_to_irq __gpio_to_irq
#define gpio_set_value __gpio_set_value
#define gpio_get_value __gpio_get_value
#define gpio_cansleep __gpio_cansleep


#define PNX8550_GPIO_BASE(pin) (0xBBF04000 | ((pin) >> 4) << 2)
#define PNX8550_GPIO_MODE_OFFSET  0x00
#define PNX8550_GPIO_DATA_OFFSET  0x10
#define PNX8550_GPIO_COUNT 64

#define _PNX8550_GPIO_MODE(pin) (*(volatile unsigned long *)(PNX8550_GPIO_BASE(pin) + PNX8550_GPIO_MODE_OFFSET))
#define _PNX8550_GPIO_DATA(pin) (*(volatile unsigned long *)(PNX8550_GPIO_BASE(pin) + PNX8550_GPIO_DATA_OFFSET))

#define PNX8550_GPIO_MODE_PRIMARY(pin)   _PNX8550_GPIO_MODE(pin) = (0x1 << (((pin) & 15) << 1))
#define PNX8550_GPIO_MODE_PUSHPULL(pin)  _PNX8550_GPIO_MODE(pin) = (0x2 << (((pin) & 15) << 1))
#define PNX8550_GPIO_MODE_OPENDRAIN(pin) _PNX8550_GPIO_MODE(pin) = (0x3 << (((pin) & 15) << 1))

#define PNX8550_GPIO_SET_IN(pin)    _PNX8550_GPIO_DATA(pin) = (0x00001 << ((pin) & 15))
#define PNX8550_GPIO_SET_LOW(pin)   _PNX8550_GPIO_DATA(pin) = (0x10000 << ((pin) & 15))
#define PNX8550_GPIO_SET_HIGH(pin)  _PNX8550_GPIO_DATA(pin) = (0x10001 << ((pin) & 15))
// note: sets pin according to LSB (only)
#define PNX8550_GPIO_SET_VALUE(pin, value) _PNX8550_GPIO_DATA(pin) = ((0x10000 | ((value) & 1)) << ((pin) & 15))
#define _PNX8550_GPIO_DATA_MASK(pin) (0x00001 << ((pin) & 15))
#define _PNX8550_GPIO_DIR_MASK(pin)  (0x10000 << ((pin) & 15))
#define PNX8550_GPIO_DATA(pin)       (_PNX8550_GPIO_DATA(pin) & (0x00001 << ((pin) & 15)))
#define PNX8550_GPIO_DIR(pin)        (_PNX8550_GPIO_DATA(pin) & (0x10000 << ((pin) & 15)))

#define PNX8550_GPIO_IRQSSTAT_CIMAX 8
#define PNX8550_GPIO_STANDBY 12
#define PNX8550_GPIO_CPU_BLUE 44
#define PNX8550_GPIO_CPU_RED 56
#define PNX8550_GPIO_CPU_GREEN 60
#define PNX8550_GPIO_PT6955_DATA 20
#define PNX8550_GPIO_PT6955_CLOCK 21
#define PNX8550_GPIO_PT6955_STROBE 22
#define PNX8550_GPIO_SC1_VCC 45
#define PNX8550_GPIO_SC1_AUX1 46
#define PNX8550_GPIO_SC1_AUX2 47

//this is the default configuration after boot, but why restore it?
//#define PNX8550_GPIO_MODE_PORT0 0xffffffff
//#define PNX8550_GPIO_MODE_PORT1 0xfffffff5
//#define PNX8550_GPIO_MODE_PORT2 0xffffffff
//#define PNX8550_GPIO_MODE_PORT3 0xfefeffff

#endif
