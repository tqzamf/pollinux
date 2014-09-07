/*
 *  Definitions for STB810 framebuffer.
 *
 *  Public domain.
 *
 */

#ifndef __PNX8550FB_H
#define __PNX8550FB_H

extern void pnx8550fb_shutdown_display(void);
extern void pnx8550fb_setup_display(unsigned int base, int pal);
extern void pnx8550fb_set_blanking(int blank);
extern void pnx8550fb_set_volume(int volume);

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

#endif
