/*
 * Definitions for PNX8550 Smartcard as GPIO.
 * 
 * Public domain.
 */

#ifndef __PNX8550_SCGPIO_H
#define __PNX8550_SCGPIO_H

#include <gpio.h>

#define PNX8550_SCGPIO_IO 0
#define PNX8550_SCGPIO_AUX1 1
#define PNX8550_SCGPIO_AUX2 2
#define PNX8550_SCGPIO_CLK 3
#define PNX8550_SCGPIO_RST 4
#define PNX8550_SCGPIO_VCC 5
#define PNX8550_SCGPIO_PRESENCE 6
#define PNX8550_SCGPIO_COUNT 7

#define PNX8550_SC1_BASE 0xBBE43000

#define PNX8550_SC1_RER  *(volatile unsigned long *)(PNX8550_SC1_BASE + 0x00)
#define PNX8550_SC1_RER_PRESENCE_CHANGE 0x2000
#define PNX8550_SC1_CCR  *(volatile unsigned long *)(PNX8550_SC1_BASE + 0x04)
#define PNX8550_SC1_CCR_CLK 0x08
#define PNX8550_SC1_UCR2 *(volatile unsigned long *)(PNX8550_SC1_BASE + 0x0C)
#define PNX8550_SC1_UCR2_RST   0x80
#define PNX8550_SC1_UCR2_VCC   0x20
#define PNX8550_SC1_UCR2_ASYNC 0x08
#define PNX8550_SC1_UCR1 *(volatile unsigned long *)(PNX8550_SC1_BASE + 0x18)
#define PNX8550_SC1_UCR1_ENABLE 0x80
#define PNX8550_SC1_UCR1_IOTX   0x08
#define PNX8550_SC1_MSR  *(volatile unsigned long *)(PNX8550_SC1_BASE + 0x30)
#define PNX8550_SC1_MSR_PRESENCE 0x10
#define PNX8550_SC1_MSR_PCHANGE  0x08
#define PNX8550_SC1_UTRR *(volatile unsigned long *)(PNX8550_SC1_BASE + 0x34)
#define PNX8550_SC1_INT_STATUS *(volatile unsigned long *)(PNX8550_SC1_BASE + 0xFE0)
#define PNX8550_SC1_INT_ENABLE *(volatile unsigned long *)(PNX8550_SC1_BASE + 0xFE4)
#define PNX8550_SC1_INT_CLEAR  *(volatile unsigned long *)(PNX8550_SC1_BASE + 0xFE8)

#endif
