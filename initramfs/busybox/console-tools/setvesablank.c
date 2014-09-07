/* vi: set sw=4 ts=4: */
/*
 * setvesablank: Set VESA blanking mode for console.
 *
 * Copyright (C) 2006 by Jan Kiszka <jan.kiszka@web.de>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

//usage:#define setvesablank_trivial_usage
//usage:       "[MODE]"
//usage:#define setvesablank_full_usage "\n\n"
//usage:       "Set VESA blanking mode to MODE"
//usage:       "\n	0	off: no blanking"
//usage:       "\n	1	suspend: stop vsync only"
//usage:       "\n	2	standby: stop hsync only"
//usage:       "\n	3	powerdown: stop both syncs (default)"

#include "libbb.h"

int setvesablank_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int setvesablank_main(int argc UNUSED_PARAM, char **argv)
{
	struct {
		char fn;
		char subarg;
	} arg = {
		10, /* TIOCL_SETVESABLANK */
		3   /* blanking mode (default: powerdown) */
	};

	if (argv[1])
		arg.subarg = xatou_range(argv[1], 0, 3);

	xioctl(xopen(VC_1, O_RDONLY), TIOCLINUX, &arg);

	return EXIT_SUCCESS;
}
