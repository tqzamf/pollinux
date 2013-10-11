#ifndef PHSTBDEMUX_CA_H    /* Multi-include protection. */
#define PHSTBDEMUX_CA_H

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
001 23022007    steel       Original
--------------------------------------------------------------------------------
    For consistency and standardisation retain the Section Separators.
*/

extern int phStbDemux_ca_register(struct dvb_adapter *adapter, struct dvb_device **ca, struct dvb_phStb_card *card);
extern void phStbDemux_ca_unregister(struct dvb_device *ca);

#endif 
