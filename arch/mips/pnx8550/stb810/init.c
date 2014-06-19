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
#include <linux/delay.h>
#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <asm/mach-pnx8550/glb.h>
#include <linux/platform_device.h>
#include <trimedia.h>
#include <cm.h>
#include <framebuffer.h>
#include <prom.h>

int prom_argc;
char **prom_argv, **prom_envp;

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

    set_io_port_base(KSEG1);
    
    prom_argc = (int) fw_arg0;
    prom_argv = (char **) fw_arg1;
    prom_envp = (char **) fw_arg2;
    prom_init_cmdline();
    prom_init_mtdparts();
    
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
     * the kernel. Everything else, such as the framebuffer, is allocated
     * dynamically from that pool. */
    mem_size = get_system_mem_size();
    add_memory_region(0, mem_size, BOOT_MEM_RAM);
}

static int __init pnx8550_tm_powerdown(void)
{
    /* Reset the TriMedias again, just in case. It doesn't hurt to stop them,
     * it only delays execution by ~500ns. */
    PNX8550_TM0_CTL = PNX8550_TM_CTL_STOP_AND_RESET;
    PNX8550_TM1_CTL = PNX8550_TM_CTL_STOP_AND_RESET;
    
    /* Then we also power them down, for power saving and to make it even harder
     * to restart them unintentionally.
     * The delay is there to guarantee that the TriMedias don't get stuck, which
     * they might due to errata. This has to be done later during boot, when the
     * BogoMIPS have already been measured, because udelay doesn't work
     * otherwise. */
    udelay(100);
    PNX8550_TM0_POWER_CTL = PNX8550_TM_POWER_CTL_REQ_POWERDOWN;
    PNX8550_TM1_POWER_CTL = PNX8550_TM_POWER_CTL_REQ_POWERDOWN;

    /* As soon as the TriMedias have been powered down, remove their clocks as
     * well. These are 270MHz clocks, so removing them down might save a bit of
     * power.
     * We cannot shut them down completely, else any access to the modules will
     * completely hang the system, but we can reduce it to the minimum the PLL
     * can output, which is 1.6875MHz. */
    while (!(PNX8550_TM0_POWER_STATUS & PNX8550_TM_POWER_CTL_ACK_POWERDOWN));
    PNX8550_CM_TM0_CTL = PNX8550_CM_TM_CLK_ENABLE | PNX8550_CM_TM_CLK_PLL;
    PNX8550_CM_PLL1_CTL = PNX8550_CM_PLL_MIN_FREQ;
    while (!(PNX8550_TM1_POWER_STATUS & PNX8550_TM_POWER_CTL_ACK_POWERDOWN));
    PNX8550_CM_TM1_CTL = PNX8550_CM_TM_CLK_ENABLE | PNX8550_CM_TM_CLK_PLL;
    PNX8550_CM_PLL6_CTL = PNX8550_CM_PLL_MIN_FREQ;
    
    return 0;
}
subsys_initcall(pnx8550_tm_powerdown);
