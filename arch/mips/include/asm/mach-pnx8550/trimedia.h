/*
 *  STB810 trimedia-specific definitions
 *
 *  Public domain.
 *
 */

#ifndef __PNX8550_TRIMEDIA_H
#define __PNX8550_TRIMEDIA_H

#define PNX8550_TM0_BASE 0xBBF40000
#define PNX8550_TM1_BASE 0xBBF60000
#define PNX8550_TM_CTL_OFFSET 0x30
#define PNX8550_TM0_CTL *(volatile unsigned long *)(PNX8550_TM0_BASE + PNX8550_TM_CTL_OFFSET)
#define PNX8550_TM1_CTL *(volatile unsigned long *)(PNX8550_TM1_BASE + PNX8550_TM_CTL_OFFSET)
#define PNX8550_TM_CTL_STOP_AND_RESET 0x40000000

#define PNX8550_TM0_POWER_BASE 0xBBE4D700
#define PNX8550_TM0_POWER_CTL    *(volatile unsigned long *)(PNX8550_TM0_POWER_BASE + 0)
#define PNX8550_TM0_POWER_STATUS *(volatile unsigned long *)(PNX8550_TM0_POWER_BASE + 4)
#define PNX8550_TM1_POWER_CTL    *(volatile unsigned long *)(PNX8550_TM0_POWER_BASE + 8)
#define PNX8550_TM1_POWER_STATUS *(volatile unsigned long *)(PNX8550_TM0_POWER_BASE + 12)
#define PNX8550_TM_POWER_CTL_REQ_POWERDOWN 1
#define PNX8550_TM_POWER_CTL_REQ_RESTART 6

#endif
