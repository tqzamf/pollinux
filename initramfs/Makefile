MAKEFLAGS=--no-print-directory

initramfs.cpio: files.lst
	../usr/gen_init_cpio files.lst >initramfs.cpio

files.lst: files.txt busybox.bin/*/*
	cat files.txt >files.lst
	find busybox.bin -type l | sed -f files.sed >>files.lst

busybox.bin/*/*: busybox/busybox
	KCONFIG_NOTIMESTAMP=1 make -C busybox install

busybox/busybox: busybox/.config busybox/*/* busybox/*/*/* busybox/*/*/*/*
	KCONFIG_NOTIMESTAMP=1 make -C busybox busybox
