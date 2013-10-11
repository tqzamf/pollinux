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
002 22022007    steel        add ca
--------------------------------------------------------------------------------
    For consistency and standardisation retain the Section Separators.
*/

#include <linux/i2c.h>
#include "dmxdev.h"
#include "dvbdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"
#include "dvb_net.h"
#include "phStbDemux_common.h"

struct dvb_phStb_card {
   struct semaphore           lock;
   int                        pdma;
   int                        nfeeds;
   int                        ndescramblers;
   char                       card_name[32];
   struct dvb_adapter         dvb_adapter;
   void                      *hw;
   struct phStb_demux_device *command;
   struct dvb_demux           demux;
   struct dmxdev              dmxdev;
   struct dmx_frontend        fe_hw;
   struct i2c_adapter        *i2c_adapter;
   struct dvb_net             dvbnet;				
   struct dvb_frontend       *fe;
   struct dvb_device         *ca_dev;
   u8                         tuner_addr;
};
