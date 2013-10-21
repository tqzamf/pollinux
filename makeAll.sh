#!/bin/bash
## Little kernel build helper
## Builds kernel and modules, afterwards packs modules into build/modules.tar and generates uimage to buld/00094000_00594000__uImage.bin 

TOP=/root/pollinbox
KERNPATH=$TOP/linux-3-4-63
BUILDPATH=$TOP/build

cd $KERNPATH
./makeuImage
mv $KERNPATH/00094000_00594000__uImage.bin $BUILDPATH
make modules_install ARCH=mips INSTALL_MOD_PATH=/tmp/modules/
cd /tmp/modules && tar cfv $BUILDPATH/modules.tar lib/
