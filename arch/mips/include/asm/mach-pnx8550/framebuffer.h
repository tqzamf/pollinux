/*
 *  Definitions for STB810 framebuffer.
 *
 *  Public domain.
 *
 */

#ifndef __PNX8550FB_H
#define __PNX8550FB_H

#define PNX8550FB_PSEUDO_PALETTE_SIZE     16

/* Macros defining the frame buffer display attributes.
 * Maximum size is exactly the 1620k required by the PAL framebuffer.
 * This wastes 270k for NTSC, but that is negligible. */
#define PNX8550FB_HEIGHT_PAL     576
#define PNX8550FB_HEIGHT_NTSC    480
#define PNX8550FB_WIDTH          720
#define PNX8550FB_MARGIN_UPPER_PAL  32
#define PNX8550FB_MARGIN_LOWER_PAL  32
#define PNX8550FB_MARGIN_UPPER_NTSC 20
#define PNX8550FB_MARGIN_LOWER_NTSC 28
#define PNX8550FB_MARGIN_LEFT    32
#define PNX8550FB_MARGIN_RIGHT   48
#define PNX8550FB_STRIDE         1440
#define PNX8550FB_HSYNC_PAL      144
#define PNX8550FB_VSYNC_PAL      49
#define PNX8550FB_HSYNC_NTSC     138
#define PNX8550FB_VSYNC_NTSC     45
#define PNX8550FB_LINE_SIZE (PNX8550FB_WIDTH * sizeof(int))
#define PNX8550FB_SIZE (PNX8550FB_HEIGHT_PAL * PNX8550FB_LINE_SIZE)

#define PNX8550FB_QVCP1_BASE 0xBBF0E000
#define PNX8550FB_QVCP1_REG(x) (*(volatile unsigned long *)(PNX8550FB_QVCP1_BASE + (x)))
#define PNX8550FB_QVCP2_BASE 0xBBF0F000
#define PNX8550FB_QVCP2_REG(x) (*(volatile unsigned long *)(PNX8550FB_QVCP2_BASE + (x)))
#define PNX8550FB_QVCP2_DAC_BASE 0xBBF17000
#define PNX8550FB_QVCP2_DAC_REG(x) (*(volatile unsigned long *)(PNX8550FB_QVCP2_DAC_BASE + 4 * (x)))

#endif
