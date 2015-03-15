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
#include <cm.h>

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

static uint16_t pnx8550_read_clock(uint8_t select) {
	PNX8550_CM_FREQ_CTR = (select << 4) | 1;
	while (!(PNX8550_CM_FREQ_CTR & 2));
	return PNX8550_CM_FREQ_CTR >> 16;
}

static int pnx8550_clocks_read(char* page, char** start, off_t offset, int count, int* eof, void* data)
{
        int len = 0;

        if (offset==0) {
        #define DUMPCLK(name, reg) do { \
				uint16_t temp = pnx8550_read_clock(PNX8550_CM_##reg##_CTR); \
                len += sprintf(&page[len], "%-11s %3d.%01dMHz\n", #name ":", \
						temp / 10, temp % 10); \
			} while (0)
        #define DUMP2CLK(name, reg) do { \
				uint16_t temp1 = pnx8550_read_clock(PNX8550_CM_##reg##_CTR1); \
				uint16_t temp2 = pnx8550_read_clock(PNX8550_CM_##reg##_CTR2); \
                len += sprintf(&page[len], "%-11s %3d.%01dMHz, %3d.%01dMHz\n", \
						#name ":", temp1 / 10, temp1 % 10, temp2 / 10, temp2 % 10); \
			} while (0)
        #define DUMPCLK2(name, reg1, reg2) do { \
				uint16_t temp1 = pnx8550_read_clock(PNX8550_CM_##reg1##_CTR); \
				uint16_t temp2 = pnx8550_read_clock(PNX8550_CM_##reg2##_CTR); \
                len += sprintf(&page[len], "%-11s %3d.%01dMHz, %3d.%01dMHz\n", \
						#name ":", temp1 / 10, temp1 % 10, temp2 / 10, temp2 % 10); \
			} while (0)
        #define DUMPCLK3(name, reg1, reg2, reg3) do { \
				uint16_t temp1 = pnx8550_read_clock(PNX8550_CM_##reg1##_CTR); \
				uint16_t temp2 = pnx8550_read_clock(PNX8550_CM_##reg2##_CTR); \
				uint16_t temp3 = pnx8550_read_clock(PNX8550_CM_##reg3##_CTR); \
                len += sprintf(&page[len], "%-11s %3d.%01dMHz, %3d.%01dMHz," \
						" %3d.%01dMHz\n", #name ":", temp1 / 10, temp1 % 10, \
						temp2 / 10, temp2 % 10,	temp3 / 10, temp3 % 10); \
			} while (0)

				DUMPCLK(dram, MEM);
				DUMPCLK(mips, MIPS);
				DUMPCLK2(tm32, TM0, TM1);
				DUMPCLK2(mips_dcsn, MDCSN, MDTL);
				DUMPCLK2(tm32_dcsn, TDCSN, TDTL);
				DUMPCLK(tunnel, TUNNEL);

                DUMPCLK3(qvcp1, QVCP1_OUT, QVCP1_PIX, QVCP1_PROC);
                DUMPCLK3(qvcp2, QVCP2_OUT, QVCP2_PIX, QVCP2_PROC);

                DUMPCLK(spdif_out, SPDO_BCLK);
                DUMPCLK2(audio_in1, AI1_OSCK, AI1_SCLK);
                DUMPCLK2(audio_out1, AO1_OSCK, AO1_SCLK);
                DUMPCLK2(audio_in2, AI2_OSCK, AI2_SCLK);
                DUMPCLK2(audio_out2, AO2_OSCK, AO2_SCLK);

                DUMP2CLK(ohci, OHCI);
                DUMPCLK2(i2c, I2C_HP, I2C_FAST);
                DUMPCLK2(uart, UART1, UART2);
                DUMPCLK2(smartcard, SC1, SC2);

                DUMPCLK(output_27, CAB_27);
                DUMPCLK3(divider, CAB_102, CAB_108, CAB_115);
                DUMPCLK3(divider, CAB_123, CAB_133, CAB_144);
                DUMPCLK3(divider, CAB_157, CAB_173, CAB_192);

                DUMPCLK(vmpg, VMPG);
                DUMPCLK(vld, VLD);
                DUMPCLK3(mbs, MBS1, MBS2, MBS3);
                DUMPCLK2(vip, VIP1, VIP2);
                DUMP2CLK(msp1, MSP1);
                DUMP2CLK(msp2, MSP2);
                DUMPCLK(timestamp, TSTAMP);
                DUMPCLK(tsdma, TSDMA);
                DUMPCLK(dvd_css, DVDCSS);
        }

        return len;
}

static struct proc_dir_entry* pnx8550_dir;
static struct proc_dir_entry* pnx8550_timers;
static struct proc_dir_entry* pnx8550_registers;
static struct proc_dir_entry* pnx8550_clocks;

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

	// Create /proc/pnx8550/clocks
        pnx8550_clocks = create_proc_read_entry(
		"clocks",
		0,
		pnx8550_dir,
		pnx8550_clocks_read,
		NULL);

        if (!pnx8550_clocks)
                printk(KERN_ERR "Can't create pnx8550 clocks proc file\n");

	return 0;
}

__initcall(pnx8550_proc_init);
