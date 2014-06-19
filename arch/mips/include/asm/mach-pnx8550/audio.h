/* 
 * PNX8550 Audio Out 1 module definitions.
 *
 * Public domain.
 * 
 */

#ifndef __PNX8550_AO_H
#define __PNX8550_AO_H

#include <asm/mach-pnx8550/cm.h>
#include <asm/mach-pnx8550/int.h>

#define PNX8550_AO_BASE	0xBBF10000

#define PNX8550_AO_STATUS      *(volatile unsigned long *)(PNX8550_AO_BASE + 0x000)
#define PNX8550_AO_CTL         *(volatile unsigned long *)(PNX8550_AO_BASE + 0x004)
#define PNX8550_AO_SERIAL      *(volatile unsigned long *)(PNX8550_AO_BASE + 0x008)
#define PNX8550_AO_FRAMING     *(volatile unsigned long *)(PNX8550_AO_BASE + 0x00C)
#define PNX8550_AO_BUF1_BASE   *(volatile unsigned long *)(PNX8550_AO_BASE + 0x014)
#define PNX8550_AO_BUF2_BASE   *(volatile unsigned long *)(PNX8550_AO_BASE + 0x018)
#define PNX8550_AO_SIZE        *(volatile unsigned long *)(PNX8550_AO_BASE + 0x01C)
#define PNX8550_AO_CC          *(volatile unsigned long *)(PNX8550_AO_BASE + 0x020)
#define PNX8550_AO_CFC         *(volatile unsigned long *)(PNX8550_AO_BASE + 0x024)

#define PNX8550_AO_DDS_CTL     PNX8550_CM_DDS4_CTL
#define PNX8550_AO_DDS_REF     1728000000
#define PNX8550_AO_DDS_MIN     5090332
#define PNX8550_AO_DDS_MAX     61083979
#define PNX8550_AO_DDS_48      30541990
#define PNX8550_AO_RATE_MIN    8000
#define PNX8550_AO_RATE_MAX    96000
#define PNX8550_AO_OSCK_CTL    PNX8550_CM_AO1_OSCK_CTL
#define PNX8550_AO_SCLK_CTL    PNX8550_CM_AO1_SCLK_CTL
#define PNX8550_AO_CLK_ENABLE  (PNX8550_CM_AO_CLK_ENABLE | PNX8550_CM_AO_CLK_FCLOCK)

#define PNX8550_AO_RESET                 0x80000000
#define PNX8550_AO_TRANS_ENABLE          0x40000000
#define PNX8550_AO_TRANS_MODE_16         0x20000000
#define PNX8550_AO_TRANS_MODE_32         0x00000000
#define PNX8550_AO_TRANS_MODE_MONO       0x00000000
#define PNX8550_AO_TRANS_MODE_STEREO     0x10000000
#define PNX8550_AO_SIGN_CONVERT_UNSIGNED 0x08000000
#define PNX8550_AO_SIGN_CONVERT_SIGNED   0x00000000
#define PNX8550_AO_BUF_INTEN             0x00000030

#define PNX8550_AO_BUF1         0x00000001
#define PNX8550_AO_BUF2         0x00000002
#define PNX8550_AO_HBE          0x00000004
#define PNX8550_AO_UDR          0x00000008
#define PNX8550_AO_BUF1_ACTIVE  0x00000010

#define PNX8550_AO_SERIAL_I2S   0x80003F03
#define PNX8550_AO_FRAMING_I2S  0x00000200
#define PNX8550_AO_CFC_I2S      0x00000000
#define PNX8550_AO_BUF_SIZE     8192
#define PNX8550_AO_BUF_ALLOC    16384
#define PNX8550_AO_SAMPLES      2048
#define PNX8550_AO_IRQ          PNX8550_INT_AUDIO_OUTPUT1

#define AK4705_VOL_DEFAULT 25
#define AK4705_VOL_MAX     34

#endif
