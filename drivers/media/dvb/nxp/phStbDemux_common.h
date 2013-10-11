#ifndef PHSTBDEMUX_PNXCOMMON_H    /* Multi-include protection. */
#define PHSTBDEMUX_PNXCOMMON_H

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Copyright (C) 2002,2003 Florian Schirmer <jolt@tuxbox.org>
 *
 * Copyright (C) 2007 NXP B.V.
 * All Rights Reserved.
 *
 * NXP based DVB adapter driver.
 * Based on the bt8xx driver by Florian Schirmer <jolt@tuxbox.org>
 *
*//*

Rev Date        Author        Comments
--------------------------------------------------------------------------------
001 25042006    steel        First Revision
002 22032006    steel        improve section fitering
003 17112006    steel        fixes for tmdlVmsp
004 23022007    steel        add ca
--------------------------------------------------------------------------------
    For consistency and standardisation retain the Section Separators.
*/
#include "demux.h"

struct phStb_ts_filter {
   int                 ts_type;
   enum dmx_ts_pes     pes_type;
   u16                 pid;
   int                 descramble;
   dmx_ts_cb           callback;
   struct dmx_ts_feed *feed;
};

struct phStb_section_filter {
   u16                        pid;
   int                        descramble;
   dmx_section_cb             callback;
   struct dmx_section_filter *filter;
};

struct phStb_capabilities {
   int numPesFilters;
   int numSectionFilters;
   int numDescramblers;
};

struct phStb_demux_device {
   void (*start)(void *device, int nfeed);
   void (*stop)(void *device, int nfeed);
   int  (*set_ts_filter)(void *device, struct phStb_ts_filter *filter, int pdma);
   int  (*del_ts_filter)(void *device, struct phStb_ts_filter *filter, int pdma);
   int  (*set_section_filter)(void *device, struct phStb_section_filter *filter, int pdma);
   int  (*del_section_filter)(void *device, struct phStb_section_filter *filter, int pdma);
   void (*dvr_start)(void *device, int nfeed);
   void (*dvr_stop)(void *device, int nfeed);
   int  (*dvr_load)(void *device, const u8 *buf, size_t len);
   void (*set_desc_word)(void *device, int index, int parity, unsigned char *cw);
   int  (*set_desc_pid)(void *device, int index, unsigned int pid);
};

extern int dvb_phStb_register(int index, void *hw, struct phStb_demux_device *command, struct phStb_capabilities *capabilities);
extern int dvb_phStb_dvr_register(int index, void *hw, struct phStb_demux_device *command, struct phStb_capabilities *capabilities);
extern int dvb_phStb_remove(int index);
extern int dvb_phStb_dvr_remove(int index);

#endif
