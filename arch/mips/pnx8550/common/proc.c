/*
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/random.h>

#include <asm/io.h>
#include <int.h>
#include <uart.h>


static int pnx8550_timers_read(char* page, char** start, off_t offset, int count, int* eof, void* data)
{
        int len = 0;
	int configPR = read_c0_config7();

        if (offset==0) {
		len += sprintf(&page[len], "Timer:      count,    compare, tc, status\n");
                len += sprintf(&page[len], "    1: %10u, %10u,  %1i, %s\n",
			       read_c0_count(), read_c0_compare(),
			      (configPR>>6)&0x1, ((configPR>>3)&0x1)? "off":"on");
                len += sprintf(&page[len], "    2: %10u, %10u,  %1i, %s\n",
			       read_c0_count2(), read_c0_compare2(),
			      (configPR>>7)&0x1, ((configPR>>4)&0x1)? "off":"on");
                len += sprintf(&page[len], "    3: %10u, %10u,  %1i, %s\n",
			       read_c0_count3(), read_c0_compare3(),
			      (configPR>>8)&0x1, ((configPR>>5)&0x1)? "off":"on");
        }

        return len;
}

static int pnx8550_registers_read(char* page, char** start, off_t offset, int count, int* eof, void* data)
{
        int len = 0;

        if (offset==0) {
        #define DUMP_NAMED(reg, name) \
                len += sprintf(&page[len], "%-10s %#10.8x\n", name ":", read_c0_##reg())
        #define DUMP(reg) \
                len += sprintf(&page[len], "%-10s %#10.8x\n", #reg ":", read_c0_##reg())
        #define DUMP_LONG(reg) \
                len += sprintf(&page[len], "%-10s %#10.8lx\n", #reg ":", read_c0_##reg())

                DUMP(prid);
                DUMP(config);
                DUMP(config1);
                DUMP(config2);
                DUMP(config3);
                DUMP_NAMED(config7, "configPR");

                DUMP(status);
                DUMP(cause);
                DUMP(debug);
                DUMP(wired);
                DUMP(pagemask);
                DUMP(pagegrain);

                DUMP(count);
                DUMP(count2);
                DUMP(count3);
                DUMP(compare);
                DUMP(compare2);
                DUMP(compare3);

                DUMP_NAMED(diag4, "cmem0");
                DUMP_NAMED(diag5, "cmem1");
                DUMP_NAMED(diag6, "cmem2");
                DUMP_NAMED(diag7, "cmem3");
                DUMP(hwrena);
                DUMP_LONG(userlocal);
        }

        return len;
}

static struct proc_dir_entry* pnx8550_dir;
static struct proc_dir_entry* pnx8550_timers;
static struct proc_dir_entry* pnx8550_registers;

static int pnx8550_proc_init( void )
{

	// Create /proc/pnx8550
        pnx8550_dir = proc_mkdir("pnx8550", NULL);
        if (!pnx8550_dir) {
                printk(KERN_ERR "Can't create pnx8550 proc dir\n");
                return -1;
        }

	// Create /proc/pnx8550/timers
        pnx8550_timers = create_proc_read_entry(
		"timers",
		0,
		pnx8550_dir,
		pnx8550_timers_read,
		NULL);

        if (!pnx8550_timers)
                printk(KERN_ERR "Can't create pnx8550 timers proc file\n");

	// Create /proc/pnx8550/registers
        pnx8550_registers = create_proc_read_entry(
		"registers",
		0,
		pnx8550_dir,
		pnx8550_registers_read,
		NULL);

        if (!pnx8550_registers)
                printk(KERN_ERR "Can't create pnx8550 registers proc file\n");

	return 0;
}

__initcall(pnx8550_proc_init);
