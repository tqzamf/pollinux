/*.
 *
 * ########################################################################
 *
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
 *
 * ########################################################################
 *
 * Reset the PNX8550 board.
 *
 */
#include <linux/kernel.h>

#include <asm/processor.h>
#include <asm/reboot.h>
#include <glb.h>
#include <standbyctl.h>

void pnx8550_machine_restart(char *command)
{
	PNX8550_RST_CTL = PNX8550_RST_DO_SW_RST;
}

void pnx8550_machine_halt(void)
{
	/* GPIO12 controls reset behavior in a complex way. After boot, it is 1,
	 * meaning that a CPU reset causes a reboot. If it is ever set to 0, the
	 * machine will instead power off on reset. Setting GPIO12 back to 1
	 * makes it power off IMMEDIATELY after ~1sec.
	 * Thus, the method for powering off is simple: set GPIO12 to open-drain
	 * output (in case something misconfigured it), pull it low, and then
	 * reset the CPU. */
	PNX8550_STANDBYCTL_MODE = (PNX8550_STANDBYCTL_MODE & PNX8550_STANDBYCTL_MODE_MASK) | PNX8550_STANDBYCTL_MODE_CONFIG;
	PNX8550_STANDBYCTL_DATA = PNX8550_STANDBYCTL_ENABLE_POWEROFF;
	pnx8550_machine_restart(NULL);

	// if that went wrong, busy-loop until the end of time
	while (1) {
		if (cpu_wait)
			cpu_wait();
	}
}
