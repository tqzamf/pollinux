#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#
# Copyright (C) 2007 NXP B.V.
# All Rights Reserved.
#
# NXP based DVB adapter driver.
#
#

# Rev Date        Author        Comments
#--------------------------------------------------------------------------------
#   4 20060511   batelaan    Use $(PRJROOT)/etc/Kbuild.mk
#   5 20060923   batelaan    Rm phStbDemux_pnx8335.o; it's become a separate module.
#   6 20070223   steel       add ca

#include $(PRJROOT)/etc/Kbuild.mk

obj-$(CONFIG_DVB_CORE) += phStbDemux_ca.o phStbDemux_dvb.o 

EXTRA_CFLAGS += -Idrivers/media/dvb/dvb-core/ \
                -Idrivers/media/dvb/frontends
