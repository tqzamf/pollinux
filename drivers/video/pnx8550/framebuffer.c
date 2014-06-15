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
#include <framebuffer.h>

#include <linux/fb.h>
#include <linux/init.h>

struct pnx8550fb_par {
	dma_addr_t iobase;
	void __iomem *base;
	unsigned int size;
	int xres;
	int yres;
	int left;
	int right;
	int upper;
	int lower;
};

static struct fb_fix_screeninfo fix = {
	.id =          "PNX8550-STB810",
	.type =        FB_TYPE_PACKED_PIXELS,
	.visual =      FB_VISUAL_TRUECOLOR,
	.accel =       FB_ACCEL_NONE,
	.line_length = PNX8550_FRAMEBUFFER_LINE_SIZE,
};

static struct fb_var_screeninfo def = {
	.xres_virtual =   PNX8550_FRAMEBUFFER_WIDTH,
	.yres_virtual =   PNX8550_FRAMEBUFFER_HEIGHT_PAL,
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
	.left_margin =    PNX8550_FRAMEBUFFER_MARGIN_LEFT,
	.right_margin =   PNX8550_FRAMEBUFFER_MARGIN_RIGHT,
	.upper_margin =   PNX8550_FRAMEBUFFER_MARGIN_UPPER_PAL,
	.lower_margin =   PNX8550_FRAMEBUFFER_MARGIN_LOWER_PAL,
	// at least make sure the timings are correct
	.pixclock =       74074,
	.hsync_len =      PNX8550_FRAMEBUFFER_HSYNC_PAL,
	.vsync_len =      PNX8550_FRAMEBUFFER_VSYNC_PAL,
	.sync =           FB_SYNC_BROADCAST,
	.vmode =          FB_VMODE_INTERLACED,
};

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
	pnx8550fb_set_blanking(blank);
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

	fb_ptr = dma_alloc_noncoherent(&dev->dev, PNX8550_FRAMEBUFFER_SIZE,
					&fb_base, GFP_USER);
	if (!fb_ptr) {
		printk(KERN_ERR "pnx8550fb failed to allocate screen memory\n");
		pnx8550_framebuffer_remove(dev);
		return 1;
	}
	par->iobase = fb_base;
	par->base = fb_ptr;
	par->size = PNX8550_FRAMEBUFFER_SIZE;
	
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
	pnx8550fb_setup_display(fb_base,
			def.yres_virtual == PNX8550_FRAMEBUFFER_HEIGHT_PAL);
	printk(KERN_INFO "fb%d: PNX8550-STB810 %s framebuffer at 0x%08x / %08x\n",
			info->node,
			(def.yres_virtual == PNX8550_FRAMEBUFFER_HEIGHT_PAL) ? "PAL" : "NTSC",
			(unsigned int) fb_base, (unsigned int) fb_ptr);
	return 0;
}

static void pnx8550_framebuffer_shutdown(struct platform_device *dev)
{
	pnx8550fb_shutdown_display();
}

static int pnx8550_framebuffer_remove(struct platform_device *dev)
{
	struct pnx8550fb_par *par;
	struct fb_info *info = platform_get_drvdata(dev);

	if (info) {
		par = info->par;

		pnx8550_framebuffer_shutdown(dev);
		if (par->base)
			dma_free_noncoherent(&dev->dev, PNX8550_FRAMEBUFFER_SIZE,
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
		// parse standard and set correct vertical parameters
		if (!strncasecmp(option, "ntsc", 4)) {
			def.yres_virtual = PNX8550_FRAMEBUFFER_HEIGHT_NTSC;
			def.upper_margin = PNX8550_FRAMEBUFFER_MARGIN_UPPER_NTSC;
			def.lower_margin = PNX8550_FRAMEBUFFER_MARGIN_LOWER_NTSC;
			def.vsync_len = PNX8550_FRAMEBUFFER_VSYNC_NTSC;
			def.hsync_len = PNX8550_FRAMEBUFFER_HSYNC_NTSC;
			option += 4;
		} else if (!strncasecmp(option, "pal", 3)) {
			def.yres_virtual = PNX8550_FRAMEBUFFER_HEIGHT_PAL;
			def.upper_margin = PNX8550_FRAMEBUFFER_MARGIN_UPPER_PAL;
			def.lower_margin = PNX8550_FRAMEBUFFER_MARGIN_LOWER_PAL;
			def.vsync_len = PNX8550_FRAMEBUFFER_VSYNC_PAL;
			def.hsync_len = PNX8550_FRAMEBUFFER_HSYNC_PAL;
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
