#!/bin/bash
make -C busybox install
cat >files.tmp <<EOF
dir proc 755 0 0
dir sys 755 0 0
dir tmp 755 0 0
dir dev 755 0 0
nod dev/console 0600 0 0 c 5 1
dir root 755 0 0
dir mnt 755 0 0
dir etc 755 0 0
slink etc/mtab /proc/mounts 777 0 0
file init init 755 0 0
dir bin 755 0 0
dir sbin 755 0 0
file sbin/telinit telinit 755 0 0
file bin/busybox busybox.bin/bin/busybox 755 0 0
dir lib 755 0 0
dir lib/modules 755 0 0
EOF
find busybox.bin -type l | sed -e 's_^busybox.bin/_slink _' -e 's_$_ /bin/busybox 777 0 0_' >>files.tmp
../usr/gen_init_cpio files.tmp >initramfs.cpio
