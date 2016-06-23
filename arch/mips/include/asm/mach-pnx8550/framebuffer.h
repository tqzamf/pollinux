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
 * Maximum size is exactly the 1800k required by the HD framebuffer.
 * This wastes 1125k for NTSC and 990k for PAL, but is easier than
 * reallocating the framebuffer when the video standard changes. */
#define PNX8550FB_HEIGHT_PAL     576
#define PNX8550FB_HEIGHT_NTSC    480
#define PNX8550FB_HEIGHT_FAKEHD  720
#define PNX8550FB_WIDTH_SD       720
#define PNX8550FB_WIDTH_FAKEHD   1280
// some underscan so that all of the screen is visible. for SD, underscan
// is >10% because analog TVs can underscan considerably. for fake HD, the
// TV shouldn't underscan at all, but some do it anyway. leave a 5% border
// just in case.
#define PNX8550FB_MARGIN_UPPER_PAL    32
#define PNX8550FB_MARGIN_LOWER_PAL    32
#define PNX8550FB_MARGIN_UPPER_NTSC   24
#define PNX8550FB_MARGIN_LOWER_NTSC   24
#define PNX8550FB_MARGIN_UPPER_FAKEHD 24
#define PNX8550FB_MARGIN_LOWER_FAKEHD 24
#define PNX8550FB_MARGIN_LEFT_SD      40
#define PNX8550FB_MARGIN_RIGHT_SD     40
#define PNX8550FB_MARGIN_LEFT_FAKEHD  32
#define PNX8550FB_MARGIN_RIGHT_FAKEHD 32
#define PNX8550FB_PIXEL          (sizeof(uint16_t))
#define PNX8550FB_STRIDE         (PNX8550FB_WIDTH_FAKEHD * PNX8550FB_PIXEL)
#define PNX8550FB_HSYNC_PAL      144
#define PNX8550FB_VSYNC_PAL      49
#define PNX8550FB_HSYNC_NTSC     138
#define PNX8550FB_VSYNC_NTSC     45
#define PNX8550FB_HSYNC_FAKEHD   480
#define PNX8550FB_VSYNC_FAKEHD   405 /* most of that is actually repeated video lines, not sync */
#define PNX8550FB_PIXCLOCK_SD      74074 /* 13.5MHz */
#define PNX8550FB_PIXCLOCK_FAKEHD  20202 /* 49.5MHz */
#define PNX8550FB_LINE_SIZE_SD     (PNX8550FB_WIDTH_SD * PNX8550FB_PIXEL)
#define PNX8550FB_LINE_SIZE_FAKEHD (PNX8550FB_WIDTH_FAKEHD * PNX8550FB_PIXEL)
#define PNX8550FB_SIZE             (PNX8550FB_HEIGHT_FAKEHD * PNX8550FB_LINE_SIZE_FAKEHD)

#define PNX8550FB_QVCP1_BASE 0xBBF0E000
#define PNX8550FB_QVCP1_REG(x) (*(volatile unsigned long *)(PNX8550FB_QVCP1_BASE + (x)))
#define PNX8550FB_QVCP2_BASE 0xBBF0F000
#define PNX8550FB_QVCP2_REG(x) (*(volatile unsigned long *)(PNX8550FB_QVCP2_BASE + (x)))
#define PNX8550FB_QVCP2_DAC_BASE 0xBBF17000
#define PNX8550FB_QVCP2_DAC_REG(x) (*(volatile unsigned long *)(PNX8550FB_QVCP2_DAC_BASE + 4 * (x)))

#endif
