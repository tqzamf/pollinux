/*
 *  Definitions for STB810 framebuffer.
 *
 *  Public domain.
 *
 */

#ifndef __PNX8550_FRAMEBUFFER_H
#define __PNX8550_FRAMEBUFFER_H

extern void pnx8550fb_shutdown_display(void);
extern void pnx8550fb_setup_display(unsigned int base, int pal);
extern void pnx8550fb_set_blanking(int blank);

#define PNX8550FB_PSEUDO_PALETTE_SIZE     16

/* Macros defining the frame buffer display attributes.
 * Maximum size is exactly the 1620k required by the PAL framebuffer.
 * This wastes 270k for NTSC, but that is negligible. */
#define PNX8550_FRAMEBUFFER_HEIGHT_PAL     576
#define PNX8550_FRAMEBUFFER_HEIGHT_NTSC    480
#define PNX8550_FRAMEBUFFER_WIDTH          720
#define PNX8550_FRAMEBUFFER_MARGIN_UPPER_PAL  32
#define PNX8550_FRAMEBUFFER_MARGIN_LOWER_PAL  32
#define PNX8550_FRAMEBUFFER_MARGIN_UPPER_NTSC 20
#define PNX8550_FRAMEBUFFER_MARGIN_LOWER_NTSC 28
#define PNX8550_FRAMEBUFFER_MARGIN_LEFT    32
#define PNX8550_FRAMEBUFFER_MARGIN_RIGHT   48
#define PNX8550_FRAMEBUFFER_STRIDE         1440
#define PNX8550_FRAMEBUFFER_HSYNC_PAL      144
#define PNX8550_FRAMEBUFFER_VSYNC_PAL      49
#define PNX8550_FRAMEBUFFER_HSYNC_NTSC     138
#define PNX8550_FRAMEBUFFER_VSYNC_NTSC     45
#define PNX8550_FRAMEBUFFER_LINE_SIZE (PNX8550_FRAMEBUFFER_WIDTH * sizeof(int))
#define PNX8550_FRAMEBUFFER_SIZE (PNX8550_FRAMEBUFFER_HEIGHT_PAL * PNX8550_FRAMEBUFFER_LINE_SIZE)

#endif
