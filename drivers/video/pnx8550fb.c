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

#include <framebuffer.h>
#include <ak4705.h>
#include <i2c.h>
#include <dcsn.h>
#include <cm.h>

#define I2C_ANABEL_BUS                 PNX8550_I2C_IP3203_BUS1
#define I2C_ANABEL_VIDEO_ADDR          0x66
#define I2C_ANABEL_CLOCK_ADDR          0x76

enum standard {
	STD_UNDEFINED = 0,
	STD_PAL,
	STD_NTSC,
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

// sets a single register in the primary video channel of ANABEL
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

    if (par->std == STD_NTSC) {
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
	} else { // default is PAL
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
	}

	// don't perform signature analysis
	anabel_video_set_reg(par, 0xba, 0x70);
	// DAC configured for CVBS + RGB (instead of Y/C)
	anabel_video_set_reg(par, 0x2d, 0x40);
	// set SD (not HD) mode; 10-bit YUV 4:2:2 D1 interface
	anabel_video_set_reg(par, 0x3a, 0x48);
	anabel_video_set_reg(par, 0x95, 0x80);

	// unblank the screen
	anabel_video_set_reg(par, 0x6e, 0x00);
}

static void pnx8550fb_hw_suspend(struct pnx8550fb_par *par)
{
	// suspend SCART switch disabling video output
	ak4705_suspend();

	// make sure the layer is disabled, to guard against any possibility
	// that stopping the clocks might hang the DMA engine.
	qvcp_set_reg(par, 0x240, 0x00);

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
    qvcp_set_clock(par, clk_pll, PNX8550_CM_PLL_27MHZ);
    qvcp_set_clock(par, clk_dds, PNX8550_CM_DDS_27MHZ);
    qvcp_set_clock(par, out_clk, PNX8550_CM_QVCP_CLK_ENABLE
			| PNX8550_CM_QVCP_CLK_FCLOCK);
	qvcp_set_clock(par, pix_clk, 0);
	qvcp_set_clock(par, proc_clk, 0);
}

static void pnx8550fb_hw_resume(struct pnx8550fb_par *par)
{
    // restore QVCP PLL & DDS, just in case
    qvcp_set_clock(par, clk_pll, PNX8550_CM_PLL_27MHZ);
    qvcp_set_clock(par, clk_dds, PNX8550_CM_DDS_27MHZ);
    // enable QVCP clocks
    qvcp_set_clock(par, out_clk, PNX8550_CM_QVCP_CLK_ENABLE
			| PNX8550_CM_QVCP_CLK_FCLOCK);
	qvcp_set_clock(par, pix_clk, PNX8550_CM_QVCP_CLK_ENABLE
			| PNX8550_CM_QVCP_CLK_FCLOCK | PNX8550_CM_QVCP_CLK_DIV_2);
    // process layers at 17MHz. slow but absolutely sufficient with a
    // single layer.
    qvcp_set_clock(par, proc_clk, PNX8550_CM_QVCP_CLK_ENABLE
			| PNX8550_CM_QVCP_CLK_FCLOCK | PNX8550_CM_QVCP_CLK_PROC17);
    // disable QVCP power-down mode
    qvcp_set_reg(par, PNX8550_DCSN_POWERDOWN_OFFSET, 0);

	// wait for clocks to come up. the I²C transactions will time out if
	// the clocks to the PNX8510 aren't yet available.
	udelay(300);
	
	// make sure the clocks are enabled
	anabel_clock_set_reg(par, 0x01, 0x00);
	anabel_clock_set_reg(par, 0x02, 0x00);
	// power the PNX8510 DACs back up
	anabel_video_set_reg(par, 0xa5, 0x30);

	// layer must be re-enabled separately if desired

	// resume SCART switch as well
	ak4705_resume();
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
		// re-enable the layer
		qvcp_set_reg(par, 0x240, 0x01);
	else
		// disable the layer, blanking the screen
		qvcp_set_reg(par, 0x240, 0x00);

	if (blank > FB_BLANK_NORMAL && !pnx8550fb_hw_is_suspended(par))
		// switching into powerdown. suspend the display hardware.
		pnx8550fb_hw_suspend(par);
}

// sets up QVCP 1 for PAL/NTSC
static void pnx8550fb_setup_qvcp(struct pnx8550fb_par *par)
{
    // setup screen geometry
    if (par->std == STD_NTSC)
    {
        qvcp_set_reg(par, 0x000, 0x03590105);
        qvcp_set_reg(par, 0x004, 0x02d00359);
        qvcp_set_reg(par, 0x008, 0x01070012);
        qvcp_set_reg(par, 0x00c, 0x02dd034a);
        qvcp_set_reg(par, 0x010, 0x0004000b);
        qvcp_set_reg(par, 0x014, 0x00950105);
        qvcp_set_reg(par, 0x028, 0x013002DD);
        qvcp_set_reg(par, 0x02c, 0x012F02DC);
        qvcp_set_reg(par, 0x230, 0x80100030);
        qvcp_set_reg(par, 0x234, 0x00F002d0);
    }
    else // default is PAL
    {
        qvcp_set_reg(par, 0x000, 0x035f0137);
        qvcp_set_reg(par, 0x004, 0x02d0035f);
        qvcp_set_reg(par, 0x008, 0x01390016);
        qvcp_set_reg(par, 0x00c, 0x02dd0350);
        qvcp_set_reg(par, 0x010, 0x0001000b);
        qvcp_set_reg(par, 0x014, 0x00AF0137);
        qvcp_set_reg(par, 0x028, 0x012D02DD);
        qvcp_set_reg(par, 0x02c, 0x012C02DC);
        qvcp_set_reg(par, 0x230, 0x80100030);
        qvcp_set_reg(par, 0x234, 0x012002d0);
    }
    // configure and enable timing generator
    qvcp_set_reg(par, 0x020, 0x20050005);
    // configure for 10-bit YUV 4:2:2 D1 interface, 2x oversample
    qvcp_set_reg(par, 0x03c, 0x0fe81400);
    // disable VBI generation
    qvcp_set_reg(par, 0x034, 0x00000000);
    qvcp_set_reg(par, 0x038, 0x00000000);
    // pedestals off
    qvcp_set_reg(par, 0x05c, 0x0);
    qvcp_set_reg(par, 0x060, 0x0);
    // gamma correction on, noise shaping (dithering) for 10 bits
    qvcp_set_reg(par, 0x074, 0xC03F3F3F);
    qvcp_set_reg(par, 0x070, 0x00150015);
    // set default background color: black
    qvcp_set_reg(par, 0x01c, 0x800000);
    // set layer base address and size
    qvcp_set_reg(par, 0x200, par->iobase);
    qvcp_set_reg(par, 0x204, PNX8550FB_STRIDE*4);
    qvcp_set_reg(par, 0x208, PNX8550FB_STRIDE*2);
    qvcp_set_reg(par, 0x20c, par->iobase + (PNX8550FB_STRIDE*2));
    qvcp_set_reg(par, 0x210, PNX8550FB_STRIDE*4);
    qvcp_set_reg(par, 0x214, 8);
    qvcp_set_reg(par, 0x2b4, PNX8550FB_WIDTH);
    // set pixel format
    qvcp_set_reg(par, 0x23c, 0x20);
    qvcp_set_reg(par, 0x238, 0x0);
    // disable chroma keying
    qvcp_set_reg(par, 0x25c, 0x0);
    qvcp_set_reg(par, 0x26c, 0x0);
    qvcp_set_reg(par, 0x27c, 0x0);
    qvcp_set_reg(par, 0x28c, 0x0);
    // color mode = ARGB 8888
    qvcp_set_reg(par, 0x2bc, 0xec);
    qvcp_set_reg(par, 0x2c4, 0xffe7eff7);
    // brightness / contrast / gamma
    qvcp_set_reg(par, 0x2b8, 0xe00);
    qvcp_set_reg(par, 0x2cc, 0x100f100);
    // matrix coefficients
    qvcp_set_reg(par, 0x2d0, 0x004d0096);
    qvcp_set_reg(par, 0x2d4, 0x001d07da);
    qvcp_set_reg(par, 0x2d8, 0x07b60070);
    qvcp_set_reg(par, 0x2dc, 0x009d077c);
    qvcp_set_reg(par, 0x2e0, 0x07e60100);
	// enable the layer
    qvcp_set_reg(par, 0x240, 0x1);
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
	.line_length = PNX8550FB_LINE_SIZE,
};

static struct fb_var_screeninfo def = {
	.xres_virtual =   PNX8550FB_WIDTH,
	.yres_virtual =   PNX8550FB_HEIGHT_PAL,
	.bits_per_pixel = 32,
	.red =            { 16, 8, 0 },
	.green =          { 8,  8, 0 },
	.blue =           { 0,  8, 0 },
	.transp =         { 30, 1, 0 },
	.activate =       FB_ACTIVATE_NOW,
	.height =         -1,
	.width =          -1,
	// these borders are nonsense for both PAL and NTSC, but provide a
	// centered initial display
	.left_margin =    PNX8550FB_MARGIN_LEFT,
	.right_margin =   PNX8550FB_MARGIN_RIGHT,
	.upper_margin =   PNX8550FB_MARGIN_UPPER_PAL,
	.lower_margin =   PNX8550FB_MARGIN_LOWER_PAL,
	// at least make sure the timings are correct
	.pixclock =       74074,
	.hsync_len =      PNX8550FB_HSYNC_PAL,
	.vsync_len =      PNX8550FB_VSYNC_PAL,
	.sync =           FB_SYNC_BROADCAST,
	.vmode =          FB_VMODE_INTERLACED,
};

enum standard std = STD_PAL;

static int pnx8550fb_standard_set(const char *val,
		const struct kernel_param *kp)
{
	int n;

	// skip newlines (can happen when writing the sysfs file)
	n = strlen(val);
	if (n > 0 && val[n - 1] == '\n')
		n--;

	if (!strncasecmp(val, "ntsc", n)) {
		std = STD_NTSC;
		def.yres_virtual = PNX8550FB_HEIGHT_NTSC;
		def.upper_margin = PNX8550FB_MARGIN_UPPER_NTSC;
		def.lower_margin = PNX8550FB_MARGIN_LOWER_NTSC;
		def.vsync_len = PNX8550FB_VSYNC_NTSC;
		def.hsync_len = PNX8550FB_HSYNC_NTSC;
	} else if (!strncasecmp(val, "pal", n)
			|| !strncasecmp(val, "default", n)) {
		std = STD_PAL;
		def.yres_virtual = PNX8550FB_HEIGHT_PAL;
		def.upper_margin = PNX8550FB_MARGIN_UPPER_PAL;
		def.lower_margin = PNX8550FB_MARGIN_LOWER_PAL;
		def.vsync_len = PNX8550FB_VSYNC_PAL;
		def.hsync_len = PNX8550FB_HSYNC_PAL;
	} else
		return -EINVAL;

	return 0;
}

static int pnx8550fb_standard_get(char *buffer,
		const struct kernel_param *kp)
{
	switch (std) {
	case STD_NTSC:
		strcpy(buffer, "ntsc");
		break;
	case STD_PAL:
		strcpy(buffer, "pal");
		break;
	default:
		sprintf(buffer, "%d", std);
		break;
	}
	return strlen(buffer);
}

static struct kernel_param_ops pnx8550fb_standard_ops = {
	.set = pnx8550fb_standard_set,
	.get = pnx8550fb_standard_get,
};

module_param_cb(standard, &pnx8550fb_standard_ops, &std, 0644);

/* Dummy palette to make fbcon work. */
static int pnx8550_framebuffer_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			 u_int transp, struct fb_info *info)
{
	u32 v;

	if (regno >= PNX8550FB_PSEUDO_PALETTE_SIZE)
		return 1;

	v = (red >> 8 << 16) | (green >> 8 << 8) | (blue >> 8 << 0);
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
	start_offset = fix.line_length * par->upper + sizeof(int) * par->left;
	end_offset = fix.line_length * par->lower + sizeof(int) * par->right;
	
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
	.fb_setcolreg	= pnx8550_framebuffer_setcolreg,
	.fb_blank       = pnx8550_framebuffer_blank,
	.fb_check_var   = pnx8550fb_check_var,
	.fb_set_par     = pnx8550fb_set_par,
	.fb_fillrect	= sys_fillrect,
	.fb_copyarea	= sys_copyarea,
	.fb_imageblit	= sys_imageblit,
};

static int pnx8550_framebuffer_remove(struct platform_device *dev);
static int pnx8550_framebuffer_probe(struct platform_device *dev)
{
	int retval = -ENOMEM;
	struct pnx8550fb_par *par;
	struct fb_info *info;
	dma_addr_t fb_base;
	void *fb_ptr;
	
	info = framebuffer_alloc(sizeof(struct pnx8550fb_par)
			+ sizeof(u32) * PNX8550FB_PSEUDO_PALETTE_SIZE, &dev->dev);
	if (!info) {
		printk(KERN_ERR "pnx8550fb failed to allocate device\n");
		pnx8550_framebuffer_remove(dev);
		return 1;
	}
	par = info->par;
	par->base = NULL;

	fb_ptr = dma_alloc_noncoherent(&dev->dev, PNX8550FB_SIZE,
					&fb_base, GFP_USER);
	if (!fb_ptr) {
		printk(KERN_ERR "pnx8550fb failed to allocate screen memory\n");
		pnx8550_framebuffer_remove(dev);
		return 1;
	}
	par->iobase = fb_base;
	par->base = fb_ptr;
	par->size = PNX8550FB_SIZE;
	par->std = def.yres_virtual == PNX8550FB_HEIGHT_NTSC ? STD_NTSC : STD_PAL;
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

	// set initial screen size
	pnx8550fb_check_var(&info->var, info);
	pnx8550fb_resize(info, 1);

	retval = register_framebuffer(info);
	if (retval < 0) {
		printk(KERN_ERR "pnx8550fb failed to register: %d\n", retval);
		pnx8550_framebuffer_remove(dev);
		return 1;
	}

	// only enable display once the driver is already registered
	pnx8550fb_setup_display(par);
	printk(KERN_INFO "fb%d: PNX8550-STB810 %s framebuffer at 0x%08x / %08x\n",
			info->node,
			(def.yres_virtual == PNX8550FB_HEIGHT_PAL) ? "PAL" : "NTSC",
			(unsigned int) fb_base, (unsigned int) fb_ptr);
	return 0;
}

static void pnx8550_framebuffer_shutdown(struct platform_device *dev)
{
	struct fb_info *info = platform_get_drvdata(dev);

	if (info)
		pnx8550fb_shutdown_display(info->par);
}

static int pnx8550_framebuffer_remove(struct platform_device *dev)
{
	struct pnx8550fb_par *par;
	struct fb_info *info = platform_get_drvdata(dev);

	if (info) {
		par = info->par;

		pnx8550_framebuffer_shutdown(dev);
		if (par->base)
			dma_free_noncoherent(&dev->dev, PNX8550FB_SIZE,
					par->base, GFP_USER);

		unregister_framebuffer(info);
		framebuffer_release(info);
	}
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
	unsigned long res;
	char *option = NULL, *rest;

	if (fb_get_options("pnx8550fb", &option))
		return -ENODEV;
	
	// format: [pal|ntsc][WIDTH][xHEIGHT]
	if (option) {
		// set standard if given. this overrides the module parameter...
		if (!strncasecmp(option, "ntsc", 4)) {
			pnx8550fb_standard_set("ntsc", NULL);
			option += 4;
		} else if (!strncasecmp(option, "pal", 3)) {
			pnx8550fb_standard_set("pal", NULL);
			option += 3;
		}

		// parse (possibly partial) sizes and apply
		rest = strchr(option, 'x');
		if (!rest)
			rest = strchr(option, 'X');
		if (rest) {
			*rest = 0;
			rest++;
			if (!kstrtol(rest, 10, &res))
				def.yres = res;
			*rest = 'x';
		}
		if (!kstrtol(option, 10, &res))
			def.xres = res;
	}

	// calculate default sizes if necessary
	if (!def.xres)
		def.xres = def.xres_virtual - def.left_margin - def.right_margin;
	if (!def.yres)
		def.yres = def.yres_virtual - def.upper_margin - def.lower_margin;
	
	return platform_driver_register(&pnx8550_framebuffer_driver);
}

module_init(pnx8550_framebuffer_init);
