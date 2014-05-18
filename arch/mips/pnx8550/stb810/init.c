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

int prom_argc;
char **prom_argv, **prom_envp;
unsigned int pnx8550_fb_base;
extern void  __init prom_init_cmdline(void);
extern unsigned long get_system_mem_size(void);

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

    set_io_port_base(KSEG1);
    
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
    pnx8550_fb_base = fb_base;
}
