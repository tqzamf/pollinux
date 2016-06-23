/*
 *  PNX8550 framebuffer driver.
 *
 *  (Derived from the virtual frame buffer device.)
 *
 *      Copyright (C) 2002 James Simmons
 *
 *	Copyright (C) 1997 Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/ctype.h>

#include <framebuffer.h>
#include <ak4705.h>
#include <i2c.h>
#include <dcsn.h>
#include <cm.h>

#define I2C_ANABEL_BUS                 PNX8550_I2C_IP3203_BUS1
#define I2C_ANABEL_VIDEO_ADDR          0x66
#define I2C_ANABEL_CLOCK_ADDR          0x76

enum standard {
	STD_KEEP = -1,
	STD_UNSET = 0,
	STD_PAL = 1,
	STD_NTSC = 2,
	STD_FAKEHD = 3,
	STD_DEFAULT = STD_PAL,
};

struct pnx8550fb_par {
	dma_addr_t iobase;
	void __iomem *base;
	unsigned int size;
	void __iomem *mmio;
	struct i2c_adapter *i2c;
	uint8_t video_addr;
	uint8_t clock_addr;
	volatile unsigned long *out_clk;
	volatile unsigned long *pix_clk;
	volatile unsigned long *proc_clk;
	volatile unsigned long *clk_pll;
	volatile unsigned long *clk_dds;
	enum standard std;
	int xres;
	int yres;
	int left;
	int right;
	int upper;
	int lower;
	dma_addr_t filterlayer;
};

// filter layer width in pixels
#define PNX8550FB_FILTERLAYER_WIDTH 4
// filter layer width in bytes; has to be a multiple of 8.
#define PNX8550FB_FILTERLAYER_STRIDE \
	(PNX8550FB_FILTERLAYER_WIDTH * sizeof(uint16_t))
static uint16_t pnx8550fb_filterlayer[1080 * PNX8550FB_FILTERLAYER_WIDTH]
		__aligned(8) = {
#define _LINE(x) (x) << 8, (x) << 8, (x) << 8, (x) << 8,
#define _BLOCK _LINE(0) _LINE(127) _LINE(255) _LINE(255) _LINE(127) _LINE(0)
	// 180 6-line blocks for 1080 lines total
#define _5BLOCKS _BLOCK _BLOCK _BLOCK _BLOCK _BLOCK
#define _20BLOCKS _5BLOCKS _5BLOCKS _5BLOCKS _5BLOCKS
	_20BLOCKS _20BLOCKS _20BLOCKS
	_20BLOCKS _20BLOCKS _20BLOCKS
	_20BLOCKS _20BLOCKS _20BLOCKS
};
// sets a single register over I²C
static void i2c_set_reg(struct i2c_adapter *bus, unsigned char addr,
		unsigned char reg, unsigned char value)
{
    unsigned char data[2] = { reg, value };
    int err;
	struct i2c_msg msg = {
		.addr = addr,
		.flags = 0,
		.len = 2,
		.buf = data,
	};

	err = i2c_transfer(bus, &msg, 1);
	if (err != 1)
		printk(KERN_ERR
			"%s: error %d writing device %02x register %02x = %02x\n",
			__func__, err, addr, reg, value);
}

// sets a single register in the primary video channel of ANABEL
static void anabel_video_set_reg(struct pnx8550fb_par *par,
		unsigned char reg, unsigned char value)
{
	i2c_set_reg(par->i2c, par->video_addr, reg, value);
}

// sets a single clock register in the primary video channel of ANABEL
static void anabel_clock_set_reg(struct pnx8550fb_par *par,
		unsigned char reg, unsigned char value)
{
	i2c_set_reg(par->i2c, par->clock_addr, reg, value);
}

// sets a single register in the QVCP
static void qvcp_set_reg(struct pnx8550fb_par *par,
		unsigned int reg, uint32_t value)
{
	*((volatile uint32_t *) (par->mmio + reg)) = value;
}

// reads a single register in the QVCP
static uint32_t qvcp_get_reg(struct pnx8550fb_par *par,
		unsigned int reg)
{
	return *((volatile uint32_t *) (par->mmio + reg));
}

// sets a single QVCP clock register in the clock module
#define qvcp_set_clock(par, reg, value) \
	do { \
		*((par)->reg) = (value); \
	} while (0)


// configures Anabel (PNX8510) for PAL/NTSC
static void pnx8550fb_setup_anabel(struct pnx8550fb_par *par)
{
	// the PNX8510 datasheet lists all undefined registers as "Must be
	// initialized to zero". these are the ones that are actually
	// initialized in the NXP splash screen code; the rest probably
	// doesn't matter.
	anabel_video_set_reg(par, 0x60, 0x00);
	anabel_video_set_reg(par, 0x7d, 0x00);

	// disable insertion of wide screen signalling, copy guard,
	// video progamming system, closed captioning & teletext
	anabel_video_set_reg(par, 0x27, 0x00);
	anabel_video_set_reg(par, 0x2c, 0x00);
	anabel_video_set_reg(par, 0x54, 0x00);
	anabel_video_set_reg(par, 0x6f, 0x00);
	// don't perform signature analysis
	anabel_video_set_reg(par, 0xba, 0x70);

    switch (par->std) {
	case STD_FAKEHD:
		// compiled screen layout for 1080i50 at 99MHz
		// value 0: low:   -512 -358 -358
		anabel_video_set_reg(par, 0xbe, 0x00);
		anabel_video_set_reg(par, 0xbf, 0x9a);
		anabel_video_set_reg(par, 0xc0, 0x9a);
		anabel_video_set_reg(par, 0xc1, 0x2a);
		anabel_video_set_reg(par, 0x98, 0x00);
		anabel_video_set_reg(par, 0x98, 0x01);
		// value 1: blank: -205    0    0
		anabel_video_set_reg(par, 0xbe, 0x33);
		anabel_video_set_reg(par, 0xbf, 0x00);
		anabel_video_set_reg(par, 0xc0, 0x00);
		anabel_video_set_reg(par, 0xc1, 0x30);
		anabel_video_set_reg(par, 0x98, 0x01);
		anabel_video_set_reg(par, 0x98, 0x02);
		// value 2: high:  +102 +358 +358
		anabel_video_set_reg(par, 0xbe, 0x66);
		anabel_video_set_reg(par, 0xbf, 0x66);
		anabel_video_set_reg(par, 0xc0, 0x66);
		anabel_video_set_reg(par, 0xc1, 0x05);
		anabel_video_set_reg(par, 0x98, 0x02);
		anabel_video_set_reg(par, 0x98, 0x03);
		// pattern 1: hsync: 29x low, 29x high, 99x blank
		anabel_video_set_reg(par, 0x87, 0xc8);
		anabel_video_set_reg(par, 0x88, 0x81);
		anabel_video_set_reg(par, 0x89, 0x72);
		anabel_video_set_reg(par, 0x8a, 0x90);
		anabel_video_set_reg(par, 0x8b, 0x62);
		anabel_video_set_reg(par, 0x8c, 0x00);
		anabel_video_set_reg(par, 0x8d, 0x00);
		anabel_video_set_reg(par, 0x8e, 0x00);
		anabel_video_set_reg(par, 0x8e, 0x01);
		// pattern 2: halfsync: 587x low, 136x blank
		anabel_video_set_reg(par, 0x87, 0xa8);
		anabel_video_set_reg(par, 0x88, 0x64);
		anabel_video_set_reg(par, 0x89, 0x1e);
		anabel_video_set_reg(par, 0x8a, 0x02);
		anabel_video_set_reg(par, 0x8b, 0x00);
		anabel_video_set_reg(par, 0x8c, 0x00);
		anabel_video_set_reg(par, 0x8d, 0x00);
		anabel_video_set_reg(par, 0x8e, 0x01);
		anabel_video_set_reg(par, 0x8e, 0x02);
		// pattern 3: halfblank: 587x blank, 136x blank
		anabel_video_set_reg(par, 0x87, 0xa9);
		anabel_video_set_reg(par, 0x88, 0x64);
		anabel_video_set_reg(par, 0x89, 0x1e);
		anabel_video_set_reg(par, 0x8a, 0x02);
		anabel_video_set_reg(par, 0x8b, 0x00);
		anabel_video_set_reg(par, 0x8c, 0x00);
		anabel_video_set_reg(par, 0x8d, 0x00);
		anabel_video_set_reg(par, 0x8e, 0x02);
		anabel_video_set_reg(par, 0x8e, 0x03);
		// pattern 4: blank: 640x blank !640x, 640x blank, 323x blank
		anabel_video_set_reg(par, 0x87, 0xf9);
		anabel_video_set_reg(par, 0x88, 0x67);
		anabel_video_set_reg(par, 0x89, 0xfe);
		anabel_video_set_reg(par, 0x8a, 0x99);
		anabel_video_set_reg(par, 0x8b, 0x42);
		anabel_video_set_reg(par, 0x8c, 0x01);
		anabel_video_set_reg(par, 0x8d, 0x00);
		anabel_video_set_reg(par, 0x8e, 0x03);
		anabel_video_set_reg(par, 0x8e, 0x04);
		// pattern 5: video: 640x *blank, 640x *blank, 323x blank
		anabel_video_set_reg(par, 0x87, 0xf1);
		anabel_video_set_reg(par, 0x88, 0x67);
		anabel_video_set_reg(par, 0x89, 0xfc);
		anabel_video_set_reg(par, 0x8a, 0x99);
		anabel_video_set_reg(par, 0x8b, 0x42);
		anabel_video_set_reg(par, 0x8c, 0x01);
		anabel_video_set_reg(par, 0x8d, 0x00);
		anabel_video_set_reg(par, 0x8e, 0x04);
		anabel_video_set_reg(par, 0x8e, 0x05);
		// line type 1: syncsync:   hsync, halfsync,  hsync, halfsync
		anabel_video_set_reg(par, 0x83, 0x51);
		anabel_video_set_reg(par, 0x84, 0x04);
		anabel_video_set_reg(par, 0x85, 0x00);
		anabel_video_set_reg(par, 0x86, 0x00);
		anabel_video_set_reg(par, 0x86, 0x01);
		// line type 2: blankblank: hsync, halfblank, hsync, halfblank
		anabel_video_set_reg(par, 0x83, 0x59);
		anabel_video_set_reg(par, 0x84, 0x06);
		anabel_video_set_reg(par, 0x85, 0x00);
		anabel_video_set_reg(par, 0x86, 0x01);
		anabel_video_set_reg(par, 0x86, 0x02);
		// line type 3: syncblank:  hsync, halfsync,  hsync, halfblank
		anabel_video_set_reg(par, 0x83, 0x51);
		anabel_video_set_reg(par, 0x84, 0x06);
		anabel_video_set_reg(par, 0x85, 0x00);
		anabel_video_set_reg(par, 0x86, 0x02);
		anabel_video_set_reg(par, 0x86, 0x03);
		// line type 4: blanksync:  hsync, halfblank, hsync, halfsync
		anabel_video_set_reg(par, 0x83, 0x59);
		anabel_video_set_reg(par, 0x84, 0x04);
		anabel_video_set_reg(par, 0x85, 0x00);
		anabel_video_set_reg(par, 0x86, 0x03);
		anabel_video_set_reg(par, 0x86, 0x04);
		// line type 5: blank: hsync, blank!
		anabel_video_set_reg(par, 0x83, 0x21);
		anabel_video_set_reg(par, 0x84, 0x00);
		anabel_video_set_reg(par, 0x85, 0x00);
		anabel_video_set_reg(par, 0x86, 0x04);
		anabel_video_set_reg(par, 0x86, 0x05);
		// line type 6: video: hsync, video
		anabel_video_set_reg(par, 0x83, 0x29);
		anabel_video_set_reg(par, 0x84, 0x00);
		anabel_video_set_reg(par, 0x85, 0x00);
		anabel_video_set_reg(par, 0x86, 0x05);
		anabel_video_set_reg(par, 0x86, 0x06);
		// line count 0: 5x syncsync
		anabel_video_set_reg(par, 0x80, 0x05);
		anabel_video_set_reg(par, 0x81, 0x04);
		anabel_video_set_reg(par, 0x82, 0x00);
		anabel_video_set_reg(par, 0x82, 0x01);
		// line count 1: 1x blankblank
		anabel_video_set_reg(par, 0x80, 0x01);
		anabel_video_set_reg(par, 0x81, 0x08);
		anabel_video_set_reg(par, 0x82, 0x01);
		anabel_video_set_reg(par, 0x82, 0x02);
		// line count 2: 14x blank !14x
		anabel_video_set_reg(par, 0x80, 0x0e);
		anabel_video_set_reg(par, 0x81, 0x14);
		anabel_video_set_reg(par, 0x82, 0x02);
		anabel_video_set_reg(par, 0x82, 0x03);
		// line count 3: 540x video
		anabel_video_set_reg(par, 0x80, 0x1c);
		anabel_video_set_reg(par, 0x81, 0x1a);
		anabel_video_set_reg(par, 0x82, 0x03);
		anabel_video_set_reg(par, 0x82, 0x04);
		// line count 4: 2x blank
		anabel_video_set_reg(par, 0x80, 0x02);
		anabel_video_set_reg(par, 0x81, 0x14);
		anabel_video_set_reg(par, 0x82, 0x04);
		anabel_video_set_reg(par, 0x82, 0x05);
		// line count 5: 1x blanksync
		anabel_video_set_reg(par, 0x80, 0x01);
		anabel_video_set_reg(par, 0x81, 0x10);
		anabel_video_set_reg(par, 0x82, 0x05);
		anabel_video_set_reg(par, 0x82, 0x06);
		// line count 6: 4x syncsync
		anabel_video_set_reg(par, 0x80, 0x04);
		anabel_video_set_reg(par, 0x81, 0x04);
		anabel_video_set_reg(par, 0x82, 0x06);
		anabel_video_set_reg(par, 0x82, 0x07);
		// line count 7: 1x syncblank
		anabel_video_set_reg(par, 0x80, 0x01);
		anabel_video_set_reg(par, 0x81, 0x0c);
		anabel_video_set_reg(par, 0x82, 0x07);
		anabel_video_set_reg(par, 0x82, 0x08);
		// line count 8: 15x blank
		anabel_video_set_reg(par, 0x80, 0x0f);
		anabel_video_set_reg(par, 0x81, 0x14);
		anabel_video_set_reg(par, 0x82, 0x08);
		anabel_video_set_reg(par, 0x82, 0x09);
		// line count 9: 540x video
		anabel_video_set_reg(par, 0x80, 0x1c);
		anabel_video_set_reg(par, 0x81, 0x1a);
		anabel_video_set_reg(par, 0x82, 0x09);
		anabel_video_set_reg(par, 0x82, 0x0a);
		// line count 10: 2x blank
		anabel_video_set_reg(par, 0x80, 0x02);
		anabel_video_set_reg(par, 0x81, 0x14);
		anabel_video_set_reg(par, 0x82, 0x0a);
		anabel_video_set_reg(par, 0x82, 0x0b);
		// line count EOF
		anabel_video_set_reg(par, 0x80, 0x00);
		anabel_video_set_reg(par, 0x81, 0x00);
		anabel_video_set_reg(par, 0x82, 0x0b);
		anabel_video_set_reg(par, 0x82, 0x0c);
		// trigger position
		anabel_video_set_reg(par, 0x98, 0x13);
		anabel_video_set_reg(par, 0x99, 0x0e);
		anabel_video_set_reg(par, 0x9a, 0x00);
		anabel_video_set_reg(par, 0x9b, 0x7f);
		anabel_video_set_reg(par, 0x9c, 0x02);
		anabel_video_set_reg(par, 0x9d, 0x20);
		// screen size: 1760x1125 !4x7
		anabel_video_set_reg(par, 0xae, 0x64);
		anabel_video_set_reg(par, 0xaf, 0x04);
		anabel_video_set_reg(par, 0xb0, 0xdf);
		anabel_video_set_reg(par, 0xb1, 0x06);
		anabel_video_set_reg(par, 0xb2, 0x07);
		anabel_video_set_reg(par, 0xb3, 0x00);
		anabel_video_set_reg(par, 0xb4, 0x04);
		anabel_video_set_reg(par, 0xb5, 0x00);
		// blank offsets + gain for Y/U/V
		anabel_video_set_reg(par, 0xc7, 0x80);
		anabel_video_set_reg(par, 0xc8, 0x80);
		anabel_video_set_reg(par, 0xc9, 0x80);
		anabel_video_set_reg(par, 0xbc, 0x00);
		anabel_video_set_reg(par, 0xa8, 0x30);
		anabel_video_set_reg(par, 0xa9, 0x00);
		anabel_video_set_reg(par, 0xaa, 0x00);
		// DAC configured for YUV / RGB (Y/C disabled)
		anabel_video_set_reg(par, 0x2d, 0x00);
		// set HD (not SD) mode; 10-bit YUVhd 4:2:2 single-D1 interface
		anabel_video_set_reg(par, 0x3a, 0x47);
		anabel_video_set_reg(par, 0x95, 0x84);
		// insert generated sync signal into all (!) components
		anabel_video_set_reg(par, 0xa6, 0xfd);
		// blank the SD video encoder
		anabel_video_set_reg(par, 0x6e, 0x40);
		break;
	case STD_NTSC:
		// color burst config
		anabel_video_set_reg(par, 0x28, 0x25);
		anabel_video_set_reg(par, 0x29, 0x1d);
		anabel_video_set_reg(par, 0x5a, 0x88);
		// chroma encoding config
		anabel_video_set_reg(par, 0x61, 0x11);
		anabel_video_set_reg(par, 0x63, 0x1f);
		anabel_video_set_reg(par, 0x64, 0x7c);
		anabel_video_set_reg(par, 0x65, 0xf0);
		anabel_video_set_reg(par, 0x66, 0x21);
		anabel_video_set_reg(par, 0x6c, 0xf2);
		anabel_video_set_reg(par, 0x6d, 0x03);
		anabel_video_set_reg(par, 0x6e, 0xD0);
		// screen geometry
		anabel_video_set_reg(par, 0x70, 0xfb);
		anabel_video_set_reg(par, 0x71, 0x90);
		anabel_video_set_reg(par, 0x72, 0x60);
		anabel_video_set_reg(par, 0x7a, 0x00);
		anabel_video_set_reg(par, 0x7b, 0x05);
		anabel_video_set_reg(par, 0x7c, 0x40);
		// YUV encoding & pedestal
		anabel_video_set_reg(par, 0xa0, 0x00);
		anabel_video_set_reg(par, 0xa2, 0x0d);
		anabel_video_set_reg(par, 0xa3, 0x80);
		anabel_video_set_reg(par, 0xa4, 0x80);
		anabel_video_set_reg(par, 0x5b, 0x7d);
		anabel_video_set_reg(par, 0x5c, 0xaf);
		anabel_video_set_reg(par, 0x5d, 0x3e);
		anabel_video_set_reg(par, 0x5e, 0x32);
		anabel_video_set_reg(par, 0x5f, 0x32);
		anabel_video_set_reg(par, 0x62, 0x4c);
		// DAC output levels
		anabel_video_set_reg(par, 0xc2, 0x1e);
		anabel_video_set_reg(par, 0xc3, 0x0A);
		anabel_video_set_reg(par, 0xc4, 0x0A);
		anabel_video_set_reg(par, 0xc5, 0x0A);
		anabel_video_set_reg(par, 0xc6, 0x01);
		// DAC configured for CVBS + RGB (instead of Y/C)
		anabel_video_set_reg(par, 0x2d, 0x40);
		// set SD (not HD) mode; 10-bit YUV 4:2:2 D1 interface
		anabel_video_set_reg(par, 0x3a, 0x48);
		anabel_video_set_reg(par, 0x95, 0x80);
		// unblank the screen
		anabel_video_set_reg(par, 0x6e, 0x00);
		break;
	default: // default is PAL
	case STD_PAL:
		// color burst config
		anabel_video_set_reg(par, 0x28, 0x21);
		anabel_video_set_reg(par, 0x29, 0x1d);
		anabel_video_set_reg(par, 0x5a, 0x00);
		// chroma encoding config
		anabel_video_set_reg(par, 0x61, 0x06);
		anabel_video_set_reg(par, 0x63, 0xcb);
		anabel_video_set_reg(par, 0x64, 0x8a);
		anabel_video_set_reg(par, 0x65, 0x09);
		anabel_video_set_reg(par, 0x66, 0x2a);
		anabel_video_set_reg(par, 0x6c, 0x02);
		anabel_video_set_reg(par, 0x6d, 0x22);
		anabel_video_set_reg(par, 0x6e, 0x60);
		// screen geometry
		anabel_video_set_reg(par, 0x70, 0x1a);
		anabel_video_set_reg(par, 0x71, 0x9f);
		anabel_video_set_reg(par, 0x72, 0x61);
		anabel_video_set_reg(par, 0x7a, 0x16);
		anabel_video_set_reg(par, 0x7b, 0x37);
		anabel_video_set_reg(par, 0x7c, 0x40);
		// YUV encoding & pedestal
		anabel_video_set_reg(par, 0xa0, 0x00);
		anabel_video_set_reg(par, 0xa2, 0x10);
		anabel_video_set_reg(par, 0xa3, 0x80);
		anabel_video_set_reg(par, 0xa4, 0x80);
		anabel_video_set_reg(par, 0x5b, 0x89);
		anabel_video_set_reg(par, 0x5c, 0xbe);
		anabel_video_set_reg(par, 0x5d, 0x37);
		anabel_video_set_reg(par, 0x5e, 0x39);
		anabel_video_set_reg(par, 0x5f, 0x39);
		anabel_video_set_reg(par, 0x62, 0x38);
		// DAC output levels
		anabel_video_set_reg(par, 0xc2, 0x1f);
		anabel_video_set_reg(par, 0xc3, 0x0A);
		anabel_video_set_reg(par, 0xc4, 0x0A);
		anabel_video_set_reg(par, 0xc5, 0x0A);
		anabel_video_set_reg(par, 0xc6, 0x00);
		// DAC configured for CVBS + RGB (instead of Y/C)
		anabel_video_set_reg(par, 0x2d, 0x40);
		// set SD (not HD) mode; 10-bit YUV 4:2:2 D1 interface
		anabel_video_set_reg(par, 0x3a, 0x48);
		anabel_video_set_reg(par, 0x95, 0x80);
		// unblank the screen
		anabel_video_set_reg(par, 0x6e, 0x00);
		break;
	}
}

static void pnx8550fb_set_layers_enabled(struct pnx8550fb_par *par,
		bool enabled)
{
	int layer;

	if (!enabled) {
		// disable all implemented layers, just in case.
		for (layer = qvcp_get_reg(par, 0x018) & 7; layer > 0; layer--)
			qvcp_set_reg(par, 0x040 + (layer << 9), 0x00);
	} else {
		// only enable the layers that are actually used.
		switch (par->std) {
		case STD_FAKEHD:
			qvcp_set_reg(par, 0x240, 0x01);
			qvcp_set_reg(par, 0x440, 0x01);
			qvcp_set_reg(par, 0x640, 0x01);
			break;
		default:
			qvcp_set_reg(par, 0x240, 0x01);
			break;
		}
	}
}

static void pnx8550fb_hw_suspend(struct pnx8550fb_par *par)
{
	// suspend SCART switch disabling video output
	ak4705_set_mode(AK4705_SUSPEND);

	// make sure the layer is disabled, to guard against any possibility
	// that stopping the clocks might hang the DMA engine.
	pnx8550fb_set_layers_enabled(par, false);

	// power down the PNX8510 DACs
	anabel_video_set_reg(par, 0xa5, 0x31);
	// stop the clocks
	anabel_clock_set_reg(par, 0x01, 0x01);
	anabel_clock_set_reg(par, 0x02, 0x01);

    // power-down the QVCP module (good measure when stopping the clocks)
    qvcp_set_reg(par, PNX8550_DCSN_POWERDOWN_OFFSET,
			PNX8550_DCSN_POWERDOWN_CMD);
    // stop QVCP clocks. we cannot shut down the output clock else the
    // Anabel stops responding to I²C commands, so we just leave the PLL
    // and output clock running instead. doesn't seem to draw any more
    // power than stopping it.
    // uses 27MHz both for HD and SD. the DACs are disabled and there are no
    // pixels to output anyway, so the frequency doesn't matter.
    qvcp_set_clock(par, clk_pll, PNX8550_CM_PLL_27MHZ);
    qvcp_set_clock(par, clk_dds, PNX8550_CM_DDS_27MHZ);
    qvcp_set_clock(par, out_clk, PNX8550_CM_CLK_ENABLE
			| PNX8550_CM_CLK_FCLOCK);
	qvcp_set_clock(par, pix_clk, 0);
	qvcp_set_clock(par, proc_clk, 0);
}

static void pnx8550fb_hw_resume(struct pnx8550fb_par *par)
{
    // setup QVCP clocks
    // DDS reference: 27MHz
    qvcp_set_clock(par, clk_dds, PNX8550_CM_DDS_27MHZ);
    switch (par->std) {
    case STD_FAKEHD:
        // fake HD uses a 99MHz interface clock
        qvcp_set_clock(par, clk_pll, 0x382c0308);
        // fake HD pixel clock is half the interface clock because it uses
        // 4:2:2 interface (49.5MHz pixel clock)
        qvcp_set_clock(par, pix_clk, PNX8550_CM_CLK_ENABLE
                | PNX8550_CM_CLK_FCLOCK | PNX8550_CM_QVCP_CLK_DIV_2);
        // process layers at 58MHz. has to be faster than the pixel clock,
        // so for fake HD mode it has to be ≥50MHz.
        qvcp_set_clock(par, proc_clk, PNX8550_CM_CLK_ENABLE
                | PNX8550_CM_CLK_FCLOCK | PNX8550_CM_QVCP_CLK_PROC58);
        break;
    default:
        // SD mode requires 27MHz interface clock
        qvcp_set_clock(par, clk_pll, PNX8550_CM_PLL_27MHZ);
        // SD pixel clock is 13.5MHz (half the interface clock)
        qvcp_set_clock(par, pix_clk, PNX8550_CM_CLK_ENABLE
                | PNX8550_CM_CLK_FCLOCK | PNX8550_CM_QVCP_CLK_DIV_2);
        // process layers at 17MHz. slow but sufficient for SD.
        qvcp_set_clock(par, proc_clk, PNX8550_CM_CLK_ENABLE
                | PNX8550_CM_CLK_FCLOCK | PNX8550_CM_QVCP_CLK_PROC17);
        break;
    }
    // enable interface clock
    qvcp_set_clock(par, out_clk, PNX8550_CM_CLK_ENABLE
            | PNX8550_CM_CLK_FCLOCK);
    // disable QVCP power-down mode
    qvcp_set_reg(par, PNX8550_DCSN_POWERDOWN_OFFSET, 0);

	// wait for clocks to come up. the I²C transactions will time out if
	// the clocks to the PNX8510 aren't yet available.
	udelay(300);

    // make sure the ANABEL clocks are enabled
    anabel_clock_set_reg(par, 0x01, 0x00);
    switch (par->std) {
    case STD_FAKEHD:
        // fake HD processing actually runs at 49.5MHz, half the interface
        // clock.
        anabel_clock_set_reg(par, 0x02, 0x02);
        break;
    default:
        // SD processing runs at 27MHz, even though the pixel rate is
        // actually just 13.5MHz.
        anabel_clock_set_reg(par, 0x02, 0x00);
        break;
    }
	// power the PNX8510 DACs back up
	anabel_video_set_reg(par, 0xa5, 0x30);

	// layer must be re-enabled separately if desired

	// resume SCART switch as well
	switch (par->std) {
	case STD_FAKEHD:
		ak4705_set_mode(AK4705_YUV);
		break;
	default:
		ak4705_set_mode(AK4705_CVBS_RGB);
		break;
	}
}

static int pnx8550fb_hw_is_suspended(struct pnx8550fb_par *par)
{
    return qvcp_get_reg(par, PNX8550_DCSN_POWERDOWN_OFFSET)
			& PNX8550_DCSN_POWERDOWN_CMD;
}

// set VESA blanking mode
void pnx8550fb_set_blanking(struct pnx8550fb_par *par, int blank)
{
	if (blank <= FB_BLANK_NORMAL && pnx8550fb_hw_is_suspended(par))
		// switching out of powerdown, ie. need to resume the hardware
		// to the point where it shows a blank screen
		pnx8550fb_hw_resume(par);

    if (blank == FB_BLANK_UNBLANK)
		// re-enable all required layers
		pnx8550fb_set_layers_enabled(par, true);
	else
		// disable all layers, blanking the screen
		pnx8550fb_set_layers_enabled(par, false);

	if (blank > FB_BLANK_NORMAL && !pnx8550fb_hw_is_suspended(par))
		// switching into powerdown. suspend the display hardware.
		pnx8550fb_hw_suspend(par);
}

// sets sensible defaults for the given layer
static void pnx8550fb_init_layer(struct pnx8550fb_par *par, int layer)
{
	uint32_t base = layer << 9;
	// disable chroma keying
	qvcp_set_reg(par, base + 0x05c, 0x0);
	qvcp_set_reg(par, base + 0x06c, 0x0);
	qvcp_set_reg(par, base + 0x07c, 0x0);
	qvcp_set_reg(par, base + 0x08c, 0x0);
	// color mode = RGB 565 (16-bit), with alpha in phantom upper 16 bits
	qvcp_set_reg(par, base + 0x038, 0x0);
	qvcp_set_reg(par, base + 0x0bc, 0xac);
	qvcp_set_reg(par, base + 0x0c4, 0xff84aa8f);
	// brightness / contrast / gamma
	qvcp_set_reg(par, base + 0x0cc, 0x100f100);
	// disable alpha blending by default (actually enables it and uses a
	// constant alpha that is fully opaque) and use default ROPs
	qvcp_set_reg(par, base + 0x0b8, 0xff0e00);
	qvcp_set_reg(par, base + 0x094, 0x00000000);
	qvcp_set_reg(par, base + 0x098, 0xffff0000);
	qvcp_set_reg(par, base + 0x09c, 0x00000000);
	// color space matrix coefficients RGB → YUV
	qvcp_set_reg(par, base + 0x0d0, 0x004d0096);
	qvcp_set_reg(par, base + 0x0d4, 0x001d07da);
	qvcp_set_reg(par, base + 0x0d8, 0x07b60070);
	qvcp_set_reg(par, base + 0x0dc, 0x009d077c);
	qvcp_set_reg(par, base + 0x0e0, 0x07e60100);
	// disable horizontal and vertical scaling
	qvcp_set_reg(par, base + 0x014, 8);
	qvcp_set_reg(par, base + 0x0a8, 0x00000000);
	qvcp_set_reg(par, base + 0x020, 0x0000ffff);
}

// sets up QVCP 1 for PAL / NTSC / fake HD
static void pnx8550fb_setup_qvcp(struct pnx8550fb_par *par)
{
	int layer;

	// disable all layers during init
	pnx8550fb_set_layers_enabled(par, false);

	// setup screen geometry in timing generator
	switch (par->std) {
	case STD_FAKEHD:
		qvcp_set_reg(par, 0x000, 0x06df0231);
		qvcp_set_reg(par, 0x004, 0x05800080);
		qvcp_set_reg(par, 0x008, 0x02300014);
		qvcp_set_reg(par, 0x00c, 0x06c3001d);
		qvcp_set_reg(par, 0x010, 0x02320005);
		qvcp_set_reg(par, 0x014, 0x01400230);
		qvcp_set_reg(par, 0x028, 0x00000000);
		qvcp_set_reg(par, 0x02c, 0x00000000);
		break;
	case STD_NTSC:
		qvcp_set_reg(par, 0x000, 0x03590105);
		qvcp_set_reg(par, 0x004, 0x02d00359);
		qvcp_set_reg(par, 0x008, 0x01070012);
		qvcp_set_reg(par, 0x00c, 0x02dd034a);
		qvcp_set_reg(par, 0x010, 0x0004000b);
		qvcp_set_reg(par, 0x014, 0x00950105);
		qvcp_set_reg(par, 0x028, 0x013002DD);
		qvcp_set_reg(par, 0x02c, 0x012F02DC);
		break;
	default: // default is PAL
	case STD_PAL:
		qvcp_set_reg(par, 0x000, 0x035f0137);
		qvcp_set_reg(par, 0x004, 0x02d0035f);
		qvcp_set_reg(par, 0x008, 0x01390016);
		qvcp_set_reg(par, 0x00c, 0x02dd0350);
		qvcp_set_reg(par, 0x010, 0x0001000b);
		qvcp_set_reg(par, 0x014, 0x00AF0137);
		qvcp_set_reg(par, 0x028, 0x012D02DD);
		qvcp_set_reg(par, 0x02c, 0x012C02DC);
		break;
	}

	// disable VBI generation
	qvcp_set_reg(par, 0x034, 0x00000000);
	qvcp_set_reg(par, 0x038, 0x00000000);
	// pedestals off
	qvcp_set_reg(par, 0x05c, 0x0);
	qvcp_set_reg(par, 0x060, 0x0);
	// gamma correction on, noise shaping (dithering) for 10 bits
	qvcp_set_reg(par, 0x070, 0x00150015);
	qvcp_set_reg(par, 0x074, 0xC03F3F3F);
	// set default background color: black
	qvcp_set_reg(par, 0x01c, 0x800000);
	// initialize all implemented layers to sensible defaults
	for (layer = qvcp_get_reg(par, 0x018) & 7; layer > 0; layer--)
		pnx8550fb_init_layer(par, layer);

	// configure output mode and enable output interface. this produces a
	// blank screen until the layers are enabled.
	switch (par->std) {
	default:
		// interlaced mode, output unblanked
		qvcp_set_reg(par, 0x020, 0x20050005);
		// configure for 10-bit YUV 4:2:2 D1 interface, 2x oversample
		qvcp_set_reg(par, 0x03c, 0x0fe81444);
		break;
	}

	// configure layer timings for selected standard
	switch (par->std) {
	case STD_FAKEHD:
		// primary display layer
		qvcp_set_reg(par, 0x230, 0x00800014);
		qvcp_set_reg(par, 0x234, 0x021C0500);
		// 720 lines in, 1080 lines out, ie. scale by 1.5 vertically.
		qvcp_set_reg(par, 0x220, 0x0000aaaa);
		qvcp_set_reg(par, 0x23c, 0x20);
		// filtering layer. data width is just 4 16-bit pixels for minimal
		// 8-byte alignment; hardware repeats the last (ie. the fourth)
		// pixel until the line is complete.
		qvcp_set_reg(par, 0x430, 0x00800014);
		qvcp_set_reg(par, 0x434, 0x021c0004);
		qvcp_set_reg(par, 0x43c, 0x20);
		// secondary display layer. starts 1 line furter up, so it has to
		// start in "the other" field, and also inherits alpha from the
		// filter layer below.
		qvcp_set_reg(par, 0x630, 0x80800027);
		qvcp_set_reg(par, 0x634, 0x021c0500);
		qvcp_set_reg(par, 0x620, 0x0000aaaa);
		qvcp_set_reg(par, 0x63c, 0x34);
		break;
	case STD_NTSC:
		qvcp_set_reg(par, 0x230, 0x80000030);
		qvcp_set_reg(par, 0x234, 0x00F002d0);
		qvcp_set_reg(par, 0x23c, 0x20);
		break;
	default: // default is PAL
	case STD_PAL:
		qvcp_set_reg(par, 0x230, 0x80000030);
		qvcp_set_reg(par, 0x234, 0x012002d0);
		qvcp_set_reg(par, 0x23c, 0x20);
		break;
	}

	// set layer base address(es) and size(s)
	switch (par->std) {
	case STD_FAKEHD:
		// primary video layer
		qvcp_set_reg(par, 0x200, par->iobase + PNX8550FB_STRIDE);
		qvcp_set_reg(par, 0x20c, par->iobase);
		qvcp_set_reg(par, 0x204, 2 * PNX8550FB_STRIDE);
		qvcp_set_reg(par, 0x210, 2 * PNX8550FB_STRIDE);
		qvcp_set_reg(par, 0x208, PNX8550FB_LINE_SIZE_FAKEHD);
		qvcp_set_reg(par, 0x2b4, PNX8550FB_WIDTH_FAKEHD);
		// filtering pseudo-layer; special 8+8 alpha + gray format. last pixel
		// repeated horizontally to fill screen width.
		qvcp_set_reg(par, 0x4c4, 0xefe7e7e7);
		qvcp_set_reg(par, 0x400, par->filterlayer + PNX8550FB_FILTERLAYER_STRIDE);
		qvcp_set_reg(par, 0x40c, par->filterlayer);
		qvcp_set_reg(par, 0x404, 2 * PNX8550FB_FILTERLAYER_STRIDE);
		qvcp_set_reg(par, 0x410, 2 * PNX8550FB_FILTERLAYER_STRIDE);
		qvcp_set_reg(par, 0x408, PNX8550FB_FILTERLAYER_STRIDE);
		qvcp_set_reg(par, 0x4b4, PNX8550FB_WIDTH_FAKEHD);
		// generate per-pixel alpha, but don't apply it to this layer. this
		// avoids painting black over primary video layer lines that should
		// be 50% blended with the secondary video layer.
		qvcp_set_reg(par, 0x4b8, 0x008e00);
		qvcp_set_reg(par, 0x494, 0x0000ffff);
		qvcp_set_reg(par, 0x498, 0x00000000);
		// secondary video layer. alpha actually comes from the filter layer
		// below it.
		qvcp_set_reg(par, 0x600, par->iobase);
		qvcp_set_reg(par, 0x60c, par->iobase + PNX8550FB_STRIDE);
		qvcp_set_reg(par, 0x604, 2 * PNX8550FB_STRIDE);
		qvcp_set_reg(par, 0x610, 2 * PNX8550FB_STRIDE);
		qvcp_set_reg(par, 0x608, PNX8550FB_LINE_SIZE_FAKEHD);
		qvcp_set_reg(par, 0x6b4, PNX8550FB_WIDTH_FAKEHD);
		qvcp_set_reg(par, 0x6b8, 0x008e00);
		break;
	default:
		qvcp_set_reg(par, 0x200, par->iobase);
		qvcp_set_reg(par, 0x20c, par->iobase + PNX8550FB_STRIDE);
		qvcp_set_reg(par, 0x204, 2 * PNX8550FB_STRIDE);
		qvcp_set_reg(par, 0x210, 2 * PNX8550FB_STRIDE);
		qvcp_set_reg(par, 0x208, PNX8550FB_LINE_SIZE_SD);
		qvcp_set_reg(par, 0x2b4, PNX8550FB_WIDTH_SD);
		break;
	}

	// enable the layers, unblanking the screen
	pnx8550fb_set_layers_enabled(par, true);
}

// shuts down the QVCP and powers it down
static void pnx8550fb_shutdown_qvcp(struct pnx8550fb_par *par)
{
	// disable timing generator, and thus reset all layers
    qvcp_set_reg(par, 0x020, 0x00000000);
}

/* Function used to initialise the screen. */
void pnx8550fb_setup_display(struct pnx8550fb_par *par)
{
	// make sure hardware isn't suspended; might have been left in that
	// state by something else.
	pnx8550fb_hw_resume(par);

    // set up the QVCP registers
    pnx8550fb_setup_qvcp(par);

    // set up Anabel over I2C
    pnx8550fb_setup_anabel(par);
}

/* Function used to shutdown the video system. */
void pnx8550fb_shutdown_display(struct pnx8550fb_par *par)
{
    // shutdown display hardware
    pnx8550fb_hw_suspend(par);

    // power down the QVCP a bit more deeply
    pnx8550fb_shutdown_qvcp(par);
}

static struct fb_fix_screeninfo fix = {
	.id =          "PNX8550-STB810",
	.type =        FB_TYPE_PACKED_PIXELS,
	.visual =      FB_VISUAL_TRUECOLOR,
	.accel =       FB_ACCEL_NONE,
	.line_length = PNX8550FB_STRIDE,
};

static struct fb_var_screeninfo def = {
	.bits_per_pixel = 16,
	.red =            { 11, 5, 0 },
	.green =          { 5,  6, 0 },
	.blue =           { 0,  5, 0 },
	.activate =       FB_ACTIVATE_NOW,
	.height =         -1,
	.width =          -1,
	.sync =           FB_SYNC_BROADCAST,
};

static enum standard std = STD_DEFAULT;
static short stdwidth = -1;
static short stdheight = -1;

static void pnx8550fb_standard_apply(enum standard newstd, int width, int height)
{
	// switch to new standard, if requested. this defaults to resetting sizes,
	// unless they are specified as well.
	if (newstd != STD_KEEP) {
		std = newstd;
		stdwidth = -1;
		stdheight = -1;
	}
	// update default sizes
	if (width > 0)
		stdwidth = width;
	if (height > 0)
		stdheight = height;

	// update screen / raster parameters
	switch (std) {
	case STD_FAKEHD:
		def.yres_virtual = PNX8550FB_HEIGHT_FAKEHD;
		def.xres_virtual = PNX8550FB_WIDTH_FAKEHD;
		def.upper_margin = PNX8550FB_MARGIN_UPPER_FAKEHD;
		def.lower_margin = PNX8550FB_MARGIN_LOWER_FAKEHD;
		def.left_margin = PNX8550FB_MARGIN_LEFT_FAKEHD;
		def.right_margin = PNX8550FB_MARGIN_RIGHT_FAKEHD;
		def.vsync_len = PNX8550FB_VSYNC_FAKEHD;
		def.hsync_len = PNX8550FB_HSYNC_FAKEHD;
		def.pixclock = PNX8550FB_PIXCLOCK_FAKEHD;
		def.vmode = FB_VMODE_INTERLACED;
		break;
	case STD_PAL:
		def.yres_virtual = PNX8550FB_HEIGHT_PAL;
		def.xres_virtual = PNX8550FB_WIDTH_SD;
		def.upper_margin = PNX8550FB_MARGIN_UPPER_PAL;
		def.lower_margin = PNX8550FB_MARGIN_LOWER_PAL;
		def.left_margin = PNX8550FB_MARGIN_LEFT_SD;
		def.right_margin = PNX8550FB_MARGIN_RIGHT_SD;
		def.vsync_len = PNX8550FB_VSYNC_PAL;
		def.hsync_len = PNX8550FB_HSYNC_PAL;
		def.pixclock = PNX8550FB_PIXCLOCK_SD;
		def.vmode = FB_VMODE_INTERLACED;
		break;
	case STD_NTSC:
		def.yres_virtual = PNX8550FB_HEIGHT_NTSC;
		def.xres_virtual = PNX8550FB_WIDTH_SD;
		def.upper_margin = PNX8550FB_MARGIN_UPPER_NTSC;
		def.lower_margin = PNX8550FB_MARGIN_LOWER_NTSC;
		def.left_margin = PNX8550FB_MARGIN_LEFT_SD;
		def.right_margin = PNX8550FB_MARGIN_RIGHT_SD;
		def.vsync_len = PNX8550FB_VSYNC_NTSC;
		def.hsync_len = PNX8550FB_HSYNC_NTSC;
		def.pixclock = PNX8550FB_PIXCLOCK_SD;
		def.vmode = FB_VMODE_INTERLACED;
		break;
	default:
		// that should never happen: unsupported standard selected!
		BUG();
	}

	// (re)calculate default sizes
	if (stdwidth <= 0)
		def.xres = def.xres_virtual - def.left_margin - def.right_margin;
	else
		def.xres = stdwidth;
	if (stdheight <= 0)
		def.yres = def.yres_virtual - def.upper_margin - def.lower_margin;
	else
		def.yres = stdheight;
}

static int pnx8550fb_standard_set(const char *val,
		const struct kernel_param *kp)
{
	enum standard new_std = STD_KEEP;
	int width = -1, height = -1;
	const char *widthstr, *heightstr;
	char buffer[5];
	unsigned long result;
	int n;

	// strip newline (when writing the sysfs file)
	n = strlen(val);
	if (n > 0 && val[n - 1] == '\n')
		n--;

	// format: [pal|ntsc|fakehd][WIDTH][xHEIGHT]
	if (!strncasecmp(val, "default", 7)) {
		new_std = STD_DEFAULT;
		val += 7;
	} else if (!strncasecmp(val, "ntsc", 4)) {
		new_std = STD_NTSC;
		val += 4;
	} else if (!strncasecmp(val, "pal", 3)) {
		new_std = STD_PAL;
		val += 3;
	} else if (!strncasecmp(val, "fakehd", 6)) {
		new_std = STD_FAKEHD;
		val += 6;
	}

	// skip field-separating whitespace (from sysfs writes only)
	while (isspace(*val))
		val++;

	// parse (possibly partial) size: find the "x" (case-insensitively)
	for (n = 0; val[n]; n++)
		if (tolower(val[n]) == 'x')
			break;

	if (val[n]) {
		// we found an "x". split the fields, copying width to a temporary
		// buffer. if it's too long, then that's because it's too large,
		// ie. out of range.
		if (n + 1 > sizeof(buffer))
			return -ERANGE;
		strlcpy(buffer, val, n + 1);
		widthstr = buffer;
		heightstr = &val[n + 1];
	} else {
		// no "x" in string, ie. only width present.
		widthstr = val;
		heightstr = NULL;
	}

	// parse width and height. range checking isn't strict but the code can
	// handle somewhat out-of-spec values, just not wildly strange ones.
	if (heightstr && *heightstr) {
		if (kstrtol(heightstr, 10, &result))
			return -EINVAL;
		if (result <= 0 || result > PNX8550FB_HEIGHT_FAKEHD)
			return -ERANGE;
		height = result;
	}
	if (widthstr && *widthstr) {
		if (kstrtol(widthstr, 10, &result))
			return -EINVAL;
		if (result <= 0 || result > PNX8550FB_WIDTH_FAKEHD)
			return -ERANGE;
		width = result;
	}

	// apply settings, changing what isn't set to -1
	pnx8550fb_standard_apply(new_std, width, height);
	return 0;
}

static int pnx8550fb_standard_get(char *buffer,
		const struct kernel_param *kp)
{
	int n;

	switch (std) {
	case STD_NTSC:
		strcpy(buffer, "NTSC");
		break;
	case STD_PAL:
		strcpy(buffer, "PAL");
		break;
	case STD_FAKEHD:
		strcpy(buffer, "fakeHD");
		break;
	default:
		sprintf(buffer, "#%d", std);
		break;
	}

	n = strlen(buffer);
	if (stdwidth > 0 && stdheight > 0)
		sprintf(&buffer[n], " %dx%d", stdwidth, stdheight);
	else if (stdwidth > 0)
		sprintf(&buffer[n], " %dx", stdwidth);
	else if (stdheight > 0)
		sprintf(&buffer[n], " x%d", stdheight);
	return strlen(buffer);
}

static struct kernel_param_ops pnx8550fb_standard_ops = {
	.set = pnx8550fb_standard_set,
	.get = pnx8550fb_standard_get,
};

module_param_cb(standard, &pnx8550fb_standard_ops, &std, 0644);
MODULE_PARM_DESC(standard, "video standard: PAL, NTSC, fakeHD. use fbset to apply!");

/* Dummy palette to make fbcon work. */
static int pnx8550_framebuffer_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			 u_int transp, struct fb_info *info)
{
	u32 v;

	if (regno >= PNX8550FB_PSEUDO_PALETTE_SIZE)
		return 1;

	v = (red >> (16-5) << (5+6)) | (green >> (16-6) << 5) | (blue >> (16-5) << 0);
	((u32 *) (info->pseudo_palette))[regno] = v;

	return 0;
}

/* Stub to make X11 happy. It spits out annoying errors otherwise. */
static int pnx8550_framebuffer_blank(int blank, struct fb_info *info)
{
	struct pnx8550fb_par *par = info->par;
	pnx8550fb_set_blanking(par, blank);
	return 0;
}

static void calc_offsets(int *res, int *left, int *right, int vres)
{
	int rem, rem_left;

	// clamp to physical resolution, and don't allow screens which are
	// too small.
	if (*res > vres)
		*res = vres;
	if (*res < vres / 2)
		*res = vres / 2;

	// remaining number of pixels. ≥0 if pixels are left over; <0 if
	// margins are too wide.
	rem = vres - *res - *left - *right;

	// add half the remaining pixels to left, and the rest to the right.
	// if rem ≥ 0, this makes the margins larger, else smaller.
	rem_left = rem / 2;
	*left += rem_left;
	*right += rem - rem_left;

	// if rem < 0, one of the margins may have become negative. if so,
	// set it to zero and enlarge the opposite margin to compensate.
	if (*left < 0) {
		*right -= *left;
		*left = 0;
	} else if (*right < 0) {
		*left -= *right;
		*right = 0;
	}
}

static int pnx8550fb_check_var(struct fb_var_screeninfo *var,
			     struct fb_info *info)
{
	struct pnx8550fb_par *par = info->par;

	// (re)calculate appropriate margins
	par->xres = var->xres;
	par->left = var->left_margin;
	par->right = var->right_margin;
	calc_offsets(&par->xres, &par->left, &par->right, def.xres_virtual);

	par->yres = var->yres;
	par->upper = var->upper_margin;
	par->lower = var->lower_margin;
	calc_offsets(&par->yres, &par->upper, &par->lower, def.yres_virtual);

	// we only support the default config, with slight modifications
	*var = def;
	// apply calculated sizes and margins
	var->xres_virtual = var->xres = par->xres;
	var->yres_virtual = var->yres = par->yres;
	var->left_margin = par->left;
	var->right_margin = par->right;
	var->upper_margin = par->upper;
	var->lower_margin = par->lower;

	return 0;
}

static int pnx8550fb_resize(struct fb_info *info, int init)
{
	struct pnx8550fb_par *par = info->par;
	int start_offset, end_offset;

	if (par->std != std) {
		// if standard has changed, re-initialize the display. this
		// involves completely shutting it down and starting it up again
		// so that the switch works without invalid intermediate output.
		// note that it WILL cause flicker; that's just impossible to
		// avoid when switching video standards.
		pnx8550fb_shutdown_display(par);
		par->std = std;
		pnx8550fb_setup_display(par);
	}

	// blank the screen. the client needs to redraw it anyway, but the
	// borders will stay black.
	memset(par->base, 0, par->size);

	// move the famebuffer's base address in memory so that the borders
	// aren't painted over. the stride stays the same because each line
	// is still as wide as it used to be, only now there are some unused
	// pixels included in that.
	start_offset = fix.line_length * par->upper + PNX8550FB_PIXEL * par->left;
	end_offset = fix.line_length * par->lower + PNX8550FB_PIXEL * par->right;

	// set base address to move the start of the screen
	if (!init)
		mutex_lock(&info->mm_lock);
	info->screen_base = (char __iomem *) (par->base + start_offset);
	info->screen_size = par->size - start_offset - end_offset;
	info->fix.smem_start = (unsigned long) (par->iobase + start_offset);
	info->fix.smem_len = info->screen_size;
	if (!init)
		mutex_unlock(&info->mm_lock);
	printk(KERN_DEBUG "pnx8550fb using %dx%d screen @%08x / %08x offset %d\n",
			par->xres, par->yres, (unsigned int) info->fix.smem_start,
			(unsigned int) info->screen_base, start_offset);

	return 0;
}

static int pnx8550fb_set_par(struct fb_info *info)
{
	return pnx8550fb_resize(info, 0);
}

static struct fb_ops ops = {
	.owner = THIS_MODULE,
	.fb_setcolreg	= pnx8550_framebuffer_setcolreg,
	.fb_blank       = pnx8550_framebuffer_blank,
	.fb_check_var   = pnx8550fb_check_var,
	.fb_set_par     = pnx8550fb_set_par,
	.fb_fillrect	= sys_fillrect,
	.fb_copyarea	= sys_copyarea,
	.fb_imageblit	= sys_imageblit,
};

static int pnx8550_framebuffer_probe(struct platform_device *dev)
{
	int retval = -ENOMEM;
	struct pnx8550fb_par *par;
	struct fb_info *info;
	dma_addr_t fb_base;
	void *fb_ptr;
	char *standard;

	info = framebuffer_alloc(sizeof(struct pnx8550fb_par)
			+ sizeof(u32) * PNX8550FB_PSEUDO_PALETTE_SIZE, &dev->dev);
	if (!info) {
		printk(KERN_ERR "pnx8550fb failed to allocate device\n");
		goto err0;
	}
	platform_set_drvdata(dev, info);
	par = info->par;
	par->base = NULL;
	par->filterlayer = 0;

	fb_ptr = dma_alloc_noncoherent(&dev->dev, PNX8550FB_SIZE, &fb_base,
			GFP_USER);
	if (!fb_ptr) {
		printk(KERN_ERR "pnx8550fb failed to allocate screen memory\n");
		goto err1;
	}
	par->iobase = fb_base;
	par->base = fb_ptr;
	par->size = PNX8550FB_SIZE;
	par->std = STD_UNSET;

	par->filterlayer = dma_map_single(&dev->dev, pnx8550fb_filterlayer,
			sizeof(pnx8550fb_filterlayer), DMA_TO_DEVICE);
	if (dma_mapping_error(&dev->dev, par->filterlayer)) {
		printk(KERN_ERR "pnx8550fb failed to map fakeHD filter layer\n");
		goto err2;
	}
	// hardware setup. should probably be defined in the platform_device
	// instead of being hardcoded.
	par->mmio = (void *) PNX8550FB_QVCP1_BASE;
	par->i2c = i2c_get_adapter(I2C_ANABEL_BUS);
	par->video_addr = I2C_ANABEL_VIDEO_ADDR;
	par->clock_addr = I2C_ANABEL_CLOCK_ADDR;
	par->out_clk = &PNX8550_CM_QVCP1_OUT_CTL;
	par->pix_clk = &PNX8550_CM_QVCP1_PIX_CTL;
	par->proc_clk = &PNX8550_CM_QVCP1_PROC_CTL;
	par->clk_pll = &PNX8550_CM_QVCP1_PLL;
	par->clk_dds = &PNX8550_CM_QVCP1_DDS;

	info->fbops = &ops;
	info->var = def;
	info->fix = fix;
	info->pseudo_palette = info->par + sizeof(struct pnx8550fb_par);
	info->flags = FBINFO_FLAG_DEFAULT;

	// set initial screen size. because the standard changes, this enables
	// the display as a side effect.
	pnx8550fb_check_var(&info->var, info);
	pnx8550fb_resize(info, 1);

	retval = register_framebuffer(info);
	if (retval) {
		printk(KERN_ERR "pnx8550fb failed to register: %d\n", retval);
		goto err3;
	}

	switch (par->std) {
	case STD_PAL:
		standard = "PAL";
		break;
	case STD_NTSC:
		standard = "NTSC";
		break;
	case STD_FAKEHD:
		standard = "fakeHD";
		break;
	default:
		standard = "unknown";
		break;
	}
	printk(KERN_INFO "fb%d: PNX8550-STB810 %s framebuffer at 0x%08x / %08x\n",
			info->node, standard, (unsigned int) fb_base,
			(unsigned int) fb_ptr);
	return 0;

err3:
	pnx8550fb_shutdown_display(info->par);
	dma_unmap_single(&dev->dev, par->filterlayer,
			sizeof(pnx8550fb_filterlayer), DMA_TO_DEVICE);
err2:
	dma_free_noncoherent(&dev->dev, PNX8550FB_SIZE, par->base, GFP_USER);
err1:
	framebuffer_release(info);
err0:
	return retval;
}

static void pnx8550_framebuffer_shutdown(struct platform_device *dev)
{
	struct fb_info *info = platform_get_drvdata(dev);
	pnx8550fb_shutdown_display(info->par);
}

static int pnx8550_framebuffer_remove(struct platform_device *dev)
{
	struct fb_info *info = platform_get_drvdata(dev);
	struct pnx8550fb_par *par = info->par;
	int res;

	// remove device. if this fails, the module cannot be unloaded.
	res = unregister_framebuffer(info);
	if (res) {
		printk("pnx8550fb failed to unregister: %d\n", res);
		return res;
	}

	// shutdown the hardware
	pnx8550fb_shutdown_display(par);

	// release memory
	dma_free_noncoherent(&dev->dev, PNX8550FB_SIZE, par->base, GFP_USER);
	dma_unmap_single(&dev->dev, par->filterlayer,
			sizeof(pnx8550fb_filterlayer), DMA_TO_DEVICE);
	framebuffer_release(info);
	return 0;
}

static struct platform_driver pnx8550_framebuffer_driver = {
        .probe    = pnx8550_framebuffer_probe,
        .remove   = pnx8550_framebuffer_remove,
        .shutdown = pnx8550_framebuffer_shutdown,
        .driver   = {
            .name = "pnx8550fb",
        },
};

static int __init pnx8550_framebuffer_init(void)
{
#ifndef MODULE
	char *option = NULL;
#endif

	// apply standard so that screen parameters are valid. required if
	// standard isn't set by module parameter.
	pnx8550fb_standard_apply(STD_KEEP, -1, -1);

#ifndef MODULE
	if (fb_get_options("pnx8550fb", &option))
		return -ENODEV;
	if (option)
		pnx8550fb_standard_set(option);
#endif

	return platform_driver_register(&pnx8550_framebuffer_driver);
}

static void __exit pnx8550_framebuffer_exit(void)
{
	platform_driver_unregister(&pnx8550_framebuffer_driver);
}

module_init(pnx8550_framebuffer_init);
module_exit(pnx8550_framebuffer_exit);

MODULE_AUTHOR("Matthias <tqzamf@gmail.com>");
MODULE_DESCRIPTION("PNX8550 STB810 framebuffer driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");

static struct platform_device_id platform_device_ids[] = {
	{ .name = "pnx8550fb" },
	{}
};
MODULE_DEVICE_TABLE(platform, platform_device_ids);
