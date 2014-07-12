/*
 *  STB810 DCSN controller definitions
 *
 *  Public domain.
 *
 */

#ifndef __PNX8550_DCSN_H
#define __PNX8550_DCSN_H

#define _PNX8550_DCSNC_MIPS_BASE 0xBBE4E000
#define _PNX8550_DCSNC_TM_BASE 0xBBF03000
#define PNX8550_DCSNC_MIPS_TIMEOUT    *(volatile unsigned long *)(_PNX8550_DCSNC_MIPS_BASE + 0x000)
#define PNX8550_DCSNC_MIPS_FAULT_ADDR *(volatile unsigned long *)(_PNX8550_DCSNC_MIPS_BASE + 0x00C)
#define PNX8550_DCSNC_MIPS_STATUS     *(volatile unsigned long *)(_PNX8550_DCSNC_MIPS_BASE + 0x010)
#define PNX8550_DCSNC_TM_TIMEOUT   *(volatile unsigned long *)(_PNX8550_DCSNC_TM_BASE + 0x000)
#define PNX8550_DCSNC_TM_ADDR      *(volatile unsigned long *)(_PNX8550_DCSNC_TM_BASE + 0x00C)
#define PNX8550_DCSNC_TM_STATUS    *(volatile unsigned long *)(_PNX8550_DCSNC_TM_BASE + 0x010)
#define PNX8550_DCSNC_TIMEOUT_DISABLED 0x01
#define PNX8550_DCSNC_TIMEOUT_LOG2(x)  (((x) & 15) << 1)

#endif
