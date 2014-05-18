/*
 *  Definitions for STB810 framebuffer.
 *
 *  Public domain.
 *
 */

#ifndef __PNX8550_FRAMEBUFFER_H
#define __PNX8550_FRAMEBUFFER_H

extern unsigned int pnx8550_fb_base;

/* Macros defining the frame buffer display attributes.
 * Maximum size is exactly the 1620k required by the PAL framebuffer.
 * This wastes 270k for NTSC, but that is negligible. */
#define PNX8550_FRAMEBUFFER_HEIGHT_PAL     576
#define PNX8550_FRAMEBUFFER_HEIGHT_NTSC    480
#define PNX8550_FRAMEBUFFER_WIDTH          720
#define PNX8550_FRAMEBUFFER_STRIDE         1440
#define PNX8550_FRAMEBUFFER_LINE_SIZE (PNX8550_FRAMEBUFFER_WIDTH * sizeof(int))
#define PNX8550_FRAMEBUFFER_SIZE (PNX8550_FRAMEBUFFER_HEIGHT_PAL * PNX8550_FRAMEBUFFER_LINE_SIZE)

#endif
