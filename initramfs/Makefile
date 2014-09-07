MAKEFLAGS=--no-print-directory

initramfs.cpio: files.lst
	../usr/gen_init_cpio files.lst >initramfs.cpio

files.lst: files.txt init telinit busybox/busybox busybox/busybox.links
	cat files.txt >files.lst
	sed -f files.sed busybox/busybox.links >>files.lst

busybox/busybox busybox/busybox.links: busybox/.config \
		busybox/*/* busybox/*/*/* busybox/*/*/*/*
	KCONFIG_NOTIMESTAMP=1 make -C busybox busybox busybox.links

.SUFFIXES:
