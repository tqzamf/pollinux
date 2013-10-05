#ifndef PNX8550_GPIO_H
#define PNX8550_GPIO_H

#include <asm/mach-pnx8550/glb.h>

/* Mode Control register addresses */
#define MMIO_MC(pin) (0x104000 + (((pin) >> 4) << 2))

/* Obtain the current mode for the specified pin from the provided MC register */
#define PIN_MODE_READ(reg, pin) ( ((reg) >> (((pin) % 16) * 2)) & 0x3 )

/* Set the mode for the specified pin */
#define PIN_MODE_WRITE(pin, mode) ( ((mode) & 0x3) << (((pin) % 16) * 2) )

/* IO Data register addresses */
#define MMIO_IOD(pin) (0x104010 + (((pin) >> 4) << 2))

/* Read the current data on the specified pin from the provided IOD register */
#define PIN_DATA_READ(reg, pin) ( ((reg) >> ((pin) % 16)) & 1 )

/* Read the current data on multiple pins from the provided IOD
 * register. You'll get strange results if all the pins aren't in the
 * same IOD register. */
#define PIN_MULTI_READ(reg, first_pin, count) (((reg) >> ((first_pin) % 16)) & ((1<<(count))-1))

/* Write out the provided data on the specified pin */
#define PIN_DATA_WRITE(pin, val) ( (1 << (16 + ((pin) % 16))) + ((val) << ((pin) % 16)) )

/* Generate the value necessary to write out the provided data on
 * multiple pins to the IOD register. You'll get strange results if
 * all the pins aren't in the same IOD register. */
#define PIN_MULTI_WRITE(first_pin, count, val) ((((1<<(count))-1) << (16 + (((first_pin) % 16)))) | ((val) << ((first_pin) % 16)))

/* Generate the value necessary to set the given pin to tristate. */
#define PIN_TRISTATE(pin) (1 << ((pin) % 16))

/* Generate the value necessary to set the given pins to
 * tristate. You'll get strange results if all the pins aren't in the
 * same IOD register. */
#define PIN_MULTI_TRISTATE(first_pin, count) (((1<<(count))-1) << ((first_pin) % 16))

#define PIN_MODE_PRIMARY 1
#define PIN_MODE_GPIO 2
#define PIN_MODE_GPIO_DRAIN 3

#define TSU_MODE_DISABLED 0
#define TSU_MODE_FALLING 1
#define TSU_MODE_RISING 2
#define TSU_MODE_BOTH 3

#define TSU_CTL(chan) (0x104400 + ((chan) << 4))
#define TSU_DATA(chan) (0x104404 + ((chan) << 4))

#define INT_STATUS(index) (0x104f30 + ((index) << 4))
#define INT_ENABLE(index) (0x104f34 + ((index) << 4))
#define INT_CLEAR(index) (0x104f38 + ((index) << 4))
#define INT_SET(index) (0x104f3c + ((index) << 4))

#define TSU_INT_STATUS(chan) INT_STATUS(((chan) < 8) ? 7 : 9)
#define TSU_INT_ENABLE(chan) INT_ENABLE(((chan) < 8) ? 7 : 9)
#define TSU_INT_CLEAR(chan) INT_CLEAR(((chan) < 8) ? 7 : 9)
#define TSU_INT_SET(chan) INT_SET(((chan) < 8) ? 7 : 9)

#endif // PNX8550_GPIO_H
