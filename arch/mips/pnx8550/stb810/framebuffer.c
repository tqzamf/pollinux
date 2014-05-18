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

static struct fb_fix_screeninfo fix = {
	.id =          "PNX8550-STB810",
	.type =        FB_TYPE_PACKED_PIXELS,
	.visual =      FB_VISUAL_TRUECOLOR,
	.accel =       FB_ACCEL_NONE,
	.line_length = PNX8550_FRAMEBUFFER_LINE_SIZE,
};

static struct fb_var_screeninfo var = {
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
	.pixclock =       72222,
	.left_margin =    79,
	.right_margin =   23,
	.upper_margin =   21,
	.lower_margin =   0,
	.hsync_len =      65,
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

static struct fb_ops ops = {
	.fb_setcolreg	= pnx8550_framebuffer_setcolreg,
	.fb_blank       = pnx8550_framebuffer_blank,
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
	info = framebuffer_alloc(sizeof(u32) * 16, &dev->dev);
	if (!info) {
		printk(KERN_ERR
				"pnx8550_stb810_fb alloc failed\n");
		return 1;
	}

	info->screen_base = (char __iomem *) fb_ptr;
	info->screen_size = PNX8550_FRAMEBUFFER_SIZE;
	info->fbops = &ops;
	info->var = var;
	fix.smem_start = (unsigned long) fb_ptr;
	fix.smem_len = PNX8550_FRAMEBUFFER_SIZE;
	info->fix = fix;     
	info->pseudo_palette = info->par;
	info->par = NULL;
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

static int pnx8550_framebuffer_remove(struct platform_device *dev)
{
	struct fb_info *info = platform_get_drvdata(dev);

	if (info) {
		unregister_framebuffer(info);
		framebuffer_release(info);
	}
	return 0;
}

static struct platform_driver pnx8550_framebuffer_driver = {
        .probe  = pnx8550_framebuffer_probe,
        .remove = pnx8550_framebuffer_remove,
        .driver = {
                .name   = "pnx8550fb",
        },
};

static struct platform_device *pnx8550_framebuffer_device;

extern void pnx8550_setupDisplay(int pal);

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
			var.yres_virtual = var.yres = PNX8550_FRAMEBUFFER_HEIGHT_NTSC;
		} else if (!strcasecmp(option, "pal")) {
			pal = 1;
			var.yres_virtual = var.yres = PNX8550_FRAMEBUFFER_HEIGHT_PAL;
		}
	}
	pnx8550_setupDisplay(pal);
	
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
