/*
 *  CIMaX I²C register definitions
 *
 *  Public domain.
 *
 */

#include <pci.h>
#include <gpio.h>

#ifndef __PNX8550_CIMAX_H
#define __PNX8550_CIMAX_H

#define CIMAX_CONTROL        0x1f
#define CIMAX_CONTROL_RESET  0x80
#define CIMAX_CONTROL_LOCK   0x01

#define CIMAX_MC_A           0x00
#define CIMAX_MC_B           0x09
#define CIMAX_MC_RESET       0x80
#define CIMAX_MC_TSOEN       0x40
#define CIMAX_MC_TSIEN       0x20
#define CIMAX_MC_HAD         0x10
#define CIMAX_MC_ACS_MASK    0x0C
#define CIMAX_MC_ACS_AM      0x00
#define CIMAX_MC_ACS_IO      0x04
#define CIMAX_MC_ACS_CM      0x08
#define CIMAX_MC_ACS_EC      0x0C
#define CIMAX_MC_AUTO        0x02
#define CIMAX_MC_DET         0x01

#define CIMAX_DEST           0x17
#define CIMAX_DEST_TSWAP     0x40
#define CIMAX_DEST_SEL_NONE  0x00
#define CIMAX_DEST_SEL_MOD_A 0x02
#define CIMAX_DEST_SEL_MOD_B 0x04
#define CIMAX_DEST_SEL_EXT   0x06
#define CIMAX_DEST_AUTOSEL   0x01

#define CIMAX_POWER          0x18
#define CIMAX_POWER_VCCC_OD  0x00
#define CIMAX_POWER_VCCC_PP  0x80
#define CIMAX_POWER_VCCC_AH  0x40
#define CIMAX_POWER_VCCC_AL  0x00
#define CIMAX_POWER_VCC_EN   0x01

#define CIMAX_TIMING_A       0x05
#define CIMAX_TIMING_B       0x0E
#define CIMAX_TIMING_AM(x) ((x) << 4)
#define CIMAX_TIMING_CM(x) (x)
#define CIMAX_TIMING_100NS   0
#define CIMAX_TIMING_150NS   1
#define CIMAX_TIMING_200NS   2
#define CIMAX_TIMING_250NS   3
#define CIMAX_TIMING_600NS   4

#define CIMAX_INT_STATUS     0x1a
#define CIMAX_INT_MASK       0x1b
#define CIMAX_INT_DET_A      0x01
#define CIMAX_INT_DET_B      0x02
#define CIMAX_INT_IRQ_A      0x04
#define CIMAX_INT_IRQ_B      0x08

#define CIMAX_INT_CONFIG     0x1c
#define CIMAX_INT_AL         0x00
#define CIMAX_INT_AH         0x02
#define CIMAX_INT_OD         0x00
#define CIMAX_INT_PP         0x04

#define CIMAX_BUS             0x1d
#define CIMAX_BUS_CS_AL       0x00
#define CIMAX_BUS_CS_AH       0x08
#define CIMAX_BUS_MODE_DIRSTR 0x01
    #define CIMAX_BUS_DIR_RD_LOW  0x00
    #define CIMAX_BUS_DIR_WR_LOW  0x02
    #define CIMAX_BUS_STR_AL      0x00
    #define CIMAX_BUS_STR_AH      0x04
#define CIMAX_BUS_MODE_RDWR   0x00
    #define CIMAX_BUS_RD_AL       0x00
    #define CIMAX_BUS_RD_AH       0x02
    #define CIMAX_BUS_WR_AL       0x00
    #define CIMAX_BUS_WR_AH       0x04

#define CIMAX_WAIT            0x1e
#define CIMAX_WAIT_MODE_WAIT  0x00
    #define CIMAX_WAIT_AL     0x00
    #define CIMAX_WAIT_AH     0x01
    #define CIMAX_WAIT_OD     0x00
    #define CIMAX_WAIT_PP     0x02
#define CIMAX_WAIT_MODE_ACK   0x04
    #define CIMAX_ACK_AL      0x00
    #define CIMAX_ACK_AH      0x01
    #define CIMAX_ACK_OD      0x00
    #define CIMAX_ACK_PP      0x02

// base address relative to XIO base, in units of 1MB.
// place the CIMaX right after flash at +64MB
#define CIMAX_OFFSET_MB   64
#define CIMAX_I2C_ADDR   0x40
#define CIMAX_IRQ        PNX8550_INT_PCI_INTA
#define CIMAX_IRQ_STATUS PNX8550_GPIO_DATA(PNX8550_GPIO_IRQSSTAT_CIMAX)
// the device provides 4 block of 64k each, selected by A24/25
#define CIMAX_NUM_BLOCKS 4
#define CIMAX_BLOCK_SIZE 65536
#define CIMAX_BLOCK_SEL_SHIFT 24

#endif
