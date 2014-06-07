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
#include <asm/delay.h>
#include <asm/reboot.h>
#include <glb.h>
#include <standbyctl.h>
#include <prom.h>

void pnx8550_machine_restart(char *command)
{
	PNX8550_RST_CTL = PNX8550_RST_DO_SW_RST;
}

void pnx8550_machine_halt(void)
{
	// busy-halt. we do not power the machine off here because halt isn't
	// supposed to do that
	while (1) {
		if (cpu_wait)
			cpu_wait();
	}
}

void pnx8550_machine_power_off(void)
{
	/* GPIO12 controls reset behavior in a complex way. After boot, it is 1,
	 * meaning that a CPU reset causes a reboot. If it is ever set to 0, the
	 * machine will instead power off on reset. Setting GPIO12 back to 1
	 * makes it power off IMMEDIATELY after ~1sec.
	 * Thus, the method for powering off is simple: set GPIO12 to open-drain
	 * output (in case something misconfigured it), pulse it low and pull it
	 * back up immediately. Note that we must leave it high until this point
	 * for reboot to work as intended. */
	// FIXME doesn't work if board was powered up by the remote
	// but nothing works reliably in that case
	PNX8550_STANDBYCTL_MODE = (PNX8550_STANDBYCTL_MODE & PNX8550_STANDBYCTL_MODE_MASK) | PNX8550_STANDBYCTL_MODE_CONFIG;
	PNX8550_STANDBYCTL_DATA = PNX8550_STANDBYCTL_ENABLE_POWEROFF;
	udelay(2000);
	PNX8550_STANDBYCTL_DATA = PNX8550_STANDBYCTL_POWEROFF_NOW;

	// wait for poweroff. if it fails, we busy-loop until the end of time.
	while (1) {
		if (cpu_wait)
			cpu_wait();
	}
}
