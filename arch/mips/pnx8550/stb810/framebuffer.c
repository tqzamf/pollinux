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
#include <framebuffer.h>

#include <linux/fb.h>
#include <linux/init.h>

struct pnx8550fb_par {
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
	.xres =           PNX8550_FRAMEBUFFER_WIDTH,
	.yres =           PNX8550_FRAMEBUFFER_HEIGHT_PAL,
	.xres_virtual =   PNX8550_FRAMEBUFFER_WIDTH,
	.yres_virtual =   PNX8550_FRAMEBUFFER_HEIGHT_PAL,
	.bits_per_pixel = 32,
	.red =            { 16, 8, 0 },
	.green =          { 8,  8, 0 },
	.blue =           { 0,  8, 0 },
	.activate =       FB_ACTIVATE_NOW,
	.height =         -1,
	.width =          -1,
	// this is only correct for PAL, but should be enough to make fbset happy
	// even for NTSC
	.pixclock =       74074,
	.left_margin =    63,
	.right_margin =   18,
	.upper_margin =   17,
	.lower_margin =   0,
	.hsync_len =      63,
	.vsync_len =      7,
	.sync =           FB_SYNC_BROADCAST,
	.vmode =          FB_VMODE_INTERLACED,
};

/* Dummy palette to make fbcon work. */
static int pnx8550_framebuffer_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			 u_int transp, struct fb_info *info)
{
	u32 v;

	if (regno >= 16)	/* no. of virtual palette registers */
		return 1;

	v = (red >> 8 << 16) | (green >> 8 << 8) | (blue >> 8 << 0);
	((u32 *) (info->pseudo_palette))[regno] = v;
	
	return 0;
}

/* Stub to make X11 happy. It spits out annoying errors otherwise. */
static int pnx8550_framebuffer_blank(int blank, struct fb_info *info)
{
	return 0;
}

static void calc_offsets(int *res, int *left, int *right, int vres)
{
	int rem, rem_left;
	
	if (*res > vres)
		*res = vres;
	
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

	// only truecolor
	//if (var->bits_per_pixel != 32)
		//return -EINVAL;
	
	par->xres = var->xres;
	par->left = var->left_margin - def.left_margin;
	par->right = var->right_margin - def.right_margin;
	calc_offsets(&par->xres, &par->left, &par->right, def.xres_virtual);
	
	par->yres = var->yres;
	par->upper = var->upper_margin - def.upper_margin;
	par->lower = var->lower_margin - def.lower_margin;
	calc_offsets(&par->yres, &par->upper, &par->lower, def.yres_virtual);
	
	// we only support the default, and maybe a bit of underscan
	*var = def;
	var->xres = par->xres;
	var->yres = par->yres;
	var->left_margin += par->left;
	var->right_margin += par->right;
	var->upper_margin += par->upper;
	var->lower_margin += par->lower;

	return 0;
}

static int pnx8550fb_set_par(struct fb_info *info)
{
	struct pnx8550fb_par *par = info->par;
	int start_offset, end_offset;
	
	// blank the screen. the client needs to redraw it anyway, but the
	// borders will stay black.
	memset(info->screen_base, 0, info->screen_size);

	// move the famebuffer's base address in memory so that the borders
	// aren't painted over. the stride stays the same because each line
	// is still as wide as it used to be, only now there are some unused
	// pixels included in that.
	start_offset = fix.line_length * par->upper + sizeof(int) * par->left;
	end_offset = fix.line_length * par->lower + sizeof(int) * par->right;
	info->screen_base = (char __iomem *) (fix.smem_start + start_offset);
	info->screen_size = fix.smem_len - start_offset - end_offset;
	printk(KERN_DEBUG "pnx8550fb using %dx%d screen @%08x offset %d\n",
			par->xres, par->yres, (unsigned int) info->screen_base,
			start_offset);

	return 0;
}

static struct fb_ops ops = {
	.fb_setcolreg	= pnx8550_framebuffer_setcolreg,
	.fb_blank       = pnx8550_framebuffer_blank,
	.fb_check_var   = pnx8550fb_check_var,
	.fb_set_par     = pnx8550fb_set_par,
	.fb_read        = fb_sys_read,
	.fb_write       = fb_sys_write,
	.fb_fillrect	= sys_fillrect,
	.fb_copyarea	= sys_copyarea,
	.fb_imageblit	= sys_imageblit,
};

static int pnx8550_framebuffer_probe(struct platform_device *dev)
{
	int retval = -ENOMEM;
	struct fb_info *info;
	void __iomem *fb_ptr;

	fb_ptr = ioremap(pnx8550_fb_base, PNX8550_FRAMEBUFFER_SIZE);
	info = framebuffer_alloc(sizeof(struct pnx8550fb_par) + sizeof(u32) * 16, &dev->dev);
	if (!info) {
		printk(KERN_ERR
				"pnx8550_stb810_fb alloc failed\n");
		return 1;
	}

	info->screen_base = (char __iomem *) fb_ptr;
	info->screen_size = PNX8550_FRAMEBUFFER_SIZE;
	info->fbops = &ops;
	info->var = def;
	fix.smem_start = (unsigned long) fb_ptr;
	fix.smem_len = PNX8550_FRAMEBUFFER_SIZE;
	info->fix = fix;     
	info->pseudo_palette = info->par + sizeof(struct pnx8550fb_par);
	info->flags = FBINFO_FLAG_DEFAULT;

	retval = register_framebuffer(info);
	if (retval < 0) {
		printk(KERN_ERR
				"pnx8550_stb810_fb failed to register: %d\n", retval);
		return 1;
	}
	printk(KERN_INFO
			"fb%d: PNX8550-STB810 %s framebuffer at 0x%08x\n",
			info->node,
			info->var.yres == PNX8550_FRAMEBUFFER_HEIGHT_PAL ? "PAL" : "NTSC",
			(unsigned int) info->screen_base);
	return 0;
}

extern void pnx8550fb_shutdown_display(void);
extern void pnx8550fb_setup_display(int pal);

static void pnx8550_framebuffer_shutdown(struct platform_device *dev)
{
	pnx8550fb_shutdown_display();
}

static int pnx8550_framebuffer_remove(struct platform_device *dev)
{
	struct fb_info *info = platform_get_drvdata(dev);

	if (info) {
		pnx8550_framebuffer_shutdown(dev);
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

static struct platform_device *pnx8550_framebuffer_device;

static int __init pnx8550_framebuffer_init(void)
{
	int ret = 0;
	char *option = NULL;
	int pal = 1; /* default is PAL */

	if (fb_get_options("pnx8550fb", &option))
		return -ENODEV;
	
	if (option) {
		if (!strcasecmp(option, "ntsc")) {
			pal = 0;
			def.yres_virtual = def.yres = PNX8550_FRAMEBUFFER_HEIGHT_NTSC;
		} else if (!strcasecmp(option, "pal")) {
			pal = 1;
			def.yres_virtual = def.yres = PNX8550_FRAMEBUFFER_HEIGHT_PAL;
		}
	}
	pnx8550fb_setup_display(pal);
	
	ret = platform_driver_register(&pnx8550_framebuffer_driver);

	if (!ret) {
		pnx8550_framebuffer_device = platform_device_alloc("pnx8550fb", 0);

		if (pnx8550_framebuffer_device)
			ret = platform_device_add(pnx8550_framebuffer_device);
		else
			ret = -ENOMEM;

		if (ret) {
			platform_device_put(pnx8550_framebuffer_device);
			platform_driver_unregister(&pnx8550_framebuffer_driver);
		}
	}

	return ret;
}

module_init(pnx8550_framebuffer_init);
