/* This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Copyright (C) 2006 Koninklijke Philips Electronics N.V.
 * All rights reserved
 *
 */
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/bootmem.h>
#include <linux/module.h>
#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <asm/mach-pnx8550/glb.h>
#include <trimedia.h>
#include <framebuffer.h>

#define HAVE_SD_DISPLAY 1

int prom_argc;
char **prom_argv, **prom_envp;
extern void  __init prom_init_cmdline(void);
extern char *prom_getenv(char *envname);
extern char *prom_getcmdline(void);
extern unsigned long get_system_mem_size(void);
#if HAVE_SD_DISPLAY
extern void pnx8550_setupDisplay(int pal, int high_mem, unsigned int background);
#endif
void *phStbMmio_Base;
extern int __init pnx8550_gpio_init(void);

//static char my_cmdline[COMMAND_LINE_SIZE] = {"console=ttyS1 stb810_display=pal"};

const char *get_system_type(void)
{
    return "Philips PNX8550/STB810";
}

/*
 * Prom init. We read our one and only communication with the firmware.
 * Grab the amount of installed memory
 */
void __init prom_init(void)
{
    unsigned int mem_size;
    unsigned int fb_base;
    char* argptr;
    int pal = 1;
#if HAVE_SD_DISPLAY
    int setup_display = 1;
#endif
    unsigned int background = 0xFFFFFFFF; /* Default background colour is WHITE */
    char * background_param;

    set_io_port_base(KSEG1);
    printk("PNX8550: prom_init called! %d\n", HAVE_SD_DISPLAY);

    /* Check argc count, and if a cmd line was passed copy it to the arcs cmdline */
    /*if (fw_arg0 == 1)
    {
        char **argv= (char**)fw_arg1;
        strcpy(arcs_cmdline, argv[0]);
    }
    else
    {
        strcpy(arcs_cmdline, my_cmdline);
    }*/
    prom_argc = (int) fw_arg0;
    prom_argv = (char **) fw_arg1;
    prom_envp = (char **) fw_arg2;

    prom_init_cmdline();
    
    /* TriMedia uses memory above 0x08000000 (for 256M).
     * This memory can be used by Linux instead, but only when the TriMedias
     * are guaranteed to be (and stay) stopped.
     * We stop them here in case something left them running. If some driver
     * starts them, Linux will crash. In fact, just loading a TriMedia memory
     * image will overwrite some "random" Linux memory region and can thus
     * cause a crash. */
    PNX8550_TM0_CTL = PNX8550_TM_CTL_STOP_AND_RESET;
    PNX8550_TM1_CTL = PNX8550_TM_CTL_STOP_AND_RESET;
    
    /* Determine the amount of memory installed and allocate all of that to
     * the kernel. The only exception is the framebuffer placed right at the
     * end of physical memory. */
    mem_size = get_system_mem_size();
    fb_base = mem_size - PNX8550_FRAMEBUFFER_SIZE;
    add_memory_region(0, fb_base, BOOT_MEM_RAM);
    add_memory_region(fb_base, PNX8550_FRAMEBUFFER_SIZE, BOOT_MEM_RESERVED);

#if HAVE_SD_DISPLAY
    argptr = prom_getcmdline();
    if (strstr(argptr, "stb810_display=ntsc") ||
        strstr(argptr, "stb810_display=pal60"))
    {
        setup_display = 1;
        pal = 0;
    }
    else if(strstr(argptr, "stb810_display=pal"))
    {
        setup_display = 1;
        pal = 1;
    }
    else
    {
        setup_display = 0;
    }

    if ((background_param = strstr(argptr, "stb810_background=")) != NULL)
    {
        /* Move pointer onto start of value */
        background_param += 18;
        background = (unsigned int) simple_strtol(background_param, NULL, 0);
    }
    printk("PNX8550: CMD: %s", argptr);
#endif // HAVE_SD_DISPLAY
    
    /* Setup MMIO region */
    phStbMmio_Base = ioremap_nocache(PNX8550_MMIO_BASE_ADDR, PNX8550_MMIO_SIZE);
    if (phStbMmio_Base == NULL)
    {
        printk(KERN_ERR "Unable to map MMIO\n");
    }

#if HAVE_SD_DISPLAY
    printk("PNX8550: Setup DIsplay: %d (Pal: %d, BG: %d)", setup_display, pal, background);
    if(setup_display)
    {
        pnx8550_setupDisplay(pal, fb_base, background);
        printk("Setting initial splash screen display mode %s\n", pal ? "SD (50Hz)" : "SD (60Hz)");
        printk("!!!!!!!!!\n");
    }
#endif
}

EXPORT_SYMBOL(phStbMmio_Base);
