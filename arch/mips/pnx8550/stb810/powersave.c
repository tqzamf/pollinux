/*
 * Non-driver to power down all unused peripherals.
 * 
 * Public domain.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <cm.h>
#include <dcsn.h>

static int __init pnx8550_powerdown_unused(void)
{
#define POWERDOWN(modname) \
	PNX8550_DCSN_POWERDOWN_CTL(PNX8550_ ## modname ## _BASE) \
			= PNX8550_DCSN_POWERDOWN_CMD; \
	PNX8550_CM_ ## modname ## _CTL = 0;
	
	// smart card 2 and internal OHCI. both are unused because they aren't
	// connected anywhere, but probably don't use much power anyway.
	POWERDOWN(SC2);
	POWERDOWN(OHCI);
	
	// MPEG routing and decoding hardware. unused without tuners.
	POWERDOWN(VMPG);
	POWERDOWN(VLD);
	POWERDOWN(MSP1);
	POWERDOWN(MSP2);
	POWERDOWN(TSDMA);
	// (analog) video input hardware. the board never had analog video in
	// the first place, so these have always been unused.
	POWERDOWN(VIP1);
	POWERDOWN(VIP2);
	// memory-based scalers. not needed when there is no video decoding.
	POWERDOWN(MBS1);
	POWERDOWN(MBS2);
	POWERDOWN(MBS3);

	// the DVD CSS module. undocumented to prevent unauthorized use, and thus
	// useless.
	// the CSS cryptographic break is probably documented significantly better
	// than this module...
	POWERDOWN(DVDCSS);

    return 0;
}
subsys_initcall(pnx8550_powerdown_unused);
