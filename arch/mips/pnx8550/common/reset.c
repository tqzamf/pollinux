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
#include <gpio.h>
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
	/* GPIO12 controls reset behavior in a complex way: If it is released
	 * high for >~1sec, after having been set low previously, the board
	 * powers off. That is, a raising edge on GPIO12 powers off, unless
	 * the GPIO is set low again during this time.
	 *
	 * The default boot script leaves GPIO12 high, so if it is ever set
	 * low, a CPU reset will release the GPIO and power down the board.
	 * If the GPIO is kept high, a CPU reset simply causes a reboot.
	 *
	 * If the boot script is modified to set GPIO12 low, then a CPU reset
	 * will release GPIO12, but only for a very short time. Thus the
	 * board will not power down and perform a normal reboot instead.
	 * However, releasing GPIO12 without a reset still causes the board
	 * to power down after ~1sec.
	 *
	 * Thus, the method for powering off is simple: set GPIO12 to
	 * open-drain output (in case something misconfigured it), pull it
	 * low (in case it hasn't already been set low), and pull it back up
	 * immediately. This works regardless of what GPIO12's state was
	 * before.
	 */
	PNX8550_GPIO_MODE_PUSHPULL(PNX8550_GPIO_STANDBY);
	PNX8550_GPIO_SET_LOW(PNX8550_GPIO_STANDBY);
	udelay(2000);
	PNX8550_GPIO_SET_HIGH(PNX8550_GPIO_STANDBY);

	// wait for poweroff. if it fails, we busy-loop until the end of time.
	while (1) {
		if (cpu_wait)
			cpu_wait();
	}
}
