/*
 *
 * BRIEF MODULE DESCRIPTION
 *   Clock module specific definitions
 *
 * Author: source@mvista.com
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */

#ifndef __PNX8550_CM_H
#define __PNX8550_CM_H

#define PNX8550_CM_BASE	0xBBE47000

// PLL0: MIPS core
// PLL1: TM32 core #1
// PLL2: QVCP #1
// PLL3: QVCP #2
// PLL5: DRAM
// PLL6: TM32 core #2
#define PNX8550_CM_PLL0_CTL    *(volatile unsigned long *)(PNX8550_CM_BASE + 0x000)
#define PNX8550_CM_PLL1_CTL    *(volatile unsigned long *)(PNX8550_CM_BASE + 0x004)
#define PNX8550_CM_PLL2_CTL    *(volatile unsigned long *)(PNX8550_CM_BASE + 0x008)
#define PNX8550_CM_PLL3_CTL    *(volatile unsigned long *)(PNX8550_CM_BASE + 0x00C)
#define PNX8550_CM_PLL4_CTL    *(volatile unsigned long *)(PNX8550_CM_BASE + 0x010)
#define PNX8550_CM_PLL5_CTL    *(volatile unsigned long *)(PNX8550_CM_BASE + 0x014)
#define PNX8550_CM_PLL6_CTL    *(volatile unsigned long *)(PNX8550_CM_BASE + 0x048)
#define PNX8550_CM_PLL1728_CTL *(volatile unsigned long *)(PNX8550_CM_BASE + 0x018)

// DDS0: AICP/QVCP #1
// DDS1: AICP/QVCP #2
// DDS3: probably AI1 oversampling clock
// DDS4: AO1 oversampling clock
// DDS5: probably AI2 oversampling clock
// DDS6: probably AO2 oversampling clock
// DDS7: probably AIO3 oversampling clock
// DDS8: probably SPDIF clock
// DDS9: probably TSDMA clock
#define PNX8550_CM_DDS0_CTL    *(volatile unsigned long *)(PNX8550_CM_BASE + 0x01C)
#define PNX8550_CM_DDS1_CTL    *(volatile unsigned long *)(PNX8550_CM_BASE + 0x020)
#define PNX8550_CM_DDS2_CTL    *(volatile unsigned long *)(PNX8550_CM_BASE + 0x024)
#define PNX8550_CM_DDS3_CTL    *(volatile unsigned long *)(PNX8550_CM_BASE + 0x028)
#define PNX8550_CM_DDS4_CTL    *(volatile unsigned long *)(PNX8550_CM_BASE + 0x02C)
#define PNX8550_CM_DDS5_CTL    *(volatile unsigned long *)(PNX8550_CM_BASE + 0x030)
#define PNX8550_CM_DDS6_CTL    *(volatile unsigned long *)(PNX8550_CM_BASE + 0x034)
#define PNX8550_CM_DDS7_CTL    *(volatile unsigned long *)(PNX8550_CM_BASE + 0x038)
#define PNX8550_CM_DDS8_CTL    *(volatile unsigned long *)(PNX8550_CM_BASE + 0x03C)
#define PNX8550_CM_DDS9_CTL    *(volatile unsigned long *)(PNX8550_CM_BASE + 0x040)

// AO1 is verified, the rest is guessed
#define PNX8550_CM_AI1_OSCK_CTL   *(volatile unsigned long *)(PNX8550_CM_BASE + 0xB04)
#define PNX8550_CM_AO1_OSCK_CTL   *(volatile unsigned long *)(PNX8550_CM_BASE + 0xB08)
#define PNX8550_CM_AI2_OSCK_CTL   *(volatile unsigned long *)(PNX8550_CM_BASE + 0xB0C)
#define PNX8550_CM_AO2_OSCK_CTL   *(volatile unsigned long *)(PNX8550_CM_BASE + 0xB10)
#define PNX8550_CM_AI1_SCLK_CTL   *(volatile unsigned long *)(PNX8550_CM_BASE + 0xB18)
#define PNX8550_CM_AO1_SCLK_CTL   *(volatile unsigned long *)(PNX8550_CM_BASE + 0xB1C)
#define PNX8550_CM_AI2_SCLK_CTL   *(volatile unsigned long *)(PNX8550_CM_BASE + 0xB20)
#define PNX8550_CM_AO2_SCLK_CTL   *(volatile unsigned long *)(PNX8550_CM_BASE + 0xB24)
#define PNX8550_CM_AO_CLK_ENABLE 0x01
#define PNX8550_CM_AO_CLK_FCLOCK 0x02

#define PNX8550_CM_QVCP1_MUX   *(volatile unsigned long *)(PNX8550_CM_BASE + 0xa00)
#define PNX8550_CM_QVCP1_CTL1  *(volatile unsigned long *)(PNX8550_CM_BASE + 0xa04)
#define PNX8550_CM_QVCP1_CTL2  *(volatile unsigned long *)(PNX8550_CM_BASE + 0xa18)
#define PNX8550_CM_QVCP2_MUX   *(volatile unsigned long *)(PNX8550_CM_BASE + 0xa08)
#define PNX8550_CM_QVCP2_CTL1  *(volatile unsigned long *)(PNX8550_CM_BASE + 0xa0c)
#define PNX8550_CM_QVCP2_CTL2  *(volatile unsigned long *)(PNX8550_CM_BASE + 0xa1c)
#define PNX8550_CM_QVCP_CLK_ENABLE 0x01
#define PNX8550_CM_QVCP_CLK_PLL    0x02
#define PNX8550_CM_QVCP_CLK_DIV_2  0x08
#define PNX8550_CM_QVCP_CTL2_DIV   0x38

#define PNX8550_CM_TM0_CTL   *(volatile unsigned long *)(PNX8550_CM_BASE + 0x204)
#define PNX8550_CM_TM1_CTL   *(volatile unsigned long *)(PNX8550_CM_BASE + 0x208)
#define PNX8550_CM_TM_CLK_ENABLE 0x01
#define PNX8550_CM_TM_CLK_PLL    0x02
#define PNX8550_CM_TM_CLK_MIPS   0x04

#define PNX8550_CM_PLL_BLOCKED_MASK     0x80000000
#define PNX8550_CM_PLL_LOCK_MASK        0x40000000
#define PNX8550_CM_PLL_CURRENT_ADJ_MASK 0x3c000000
#define PNX8550_CM_PLL_N_MASK           0x01ff0000
#define PNX8550_CM_PLL_M_MASK           0x00003f00
#define PNX8550_CM_PLL_P_MASK           0x0000000c
#define PNX8550_CM_PLL_PD_MASK          0x00000002
// sets a PLL to its minimum permitted frequency of 1.6875MHz
#define PNX8550_CM_PLL_MIN_FREQ         0x3801020c

#endif
