MAKEFLAGS=--no-print-directory

initramfs.cpio: files.lst
	../usr/gen_init_cpio files.lst >initramfs.cpio

files.lst: files.txt init telinit busybox/busybox busybox/busybox.links
	cat files.txt >files.lst
	sed -f files.sed busybox/busybox.links >>files.lst

libc: uclibc/.config uclibc/*/* uclibc/*/*/* uclibc/*/*/*/* \
		uclibc/*/*/*/*/* uclibc/*/*/*/*/*/* uclibc/*/*/*/*/*/*/* \
		uclibc/*/*/*/*/*/*/*/* uclibc/*/*/*/*/*/*/*/*/*
	make -j10 PREFIX=../libc -C uclibc install

busybox/busybox busybox/busybox.links: libc busybox/.config \
		busybox/*/* busybox/*/*/* busybox/*/*/*/*
	rm -f busybox/busybox.links
	make -j10 KCONFIG_NOTIMESTAMP=1 -C busybox busybox
	make KCONFIG_NOTIMESTAMP=1 -C busybox busybox.links

.SUFFIXES:
