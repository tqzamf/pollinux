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
001             Chris Steel   port from STB810
002 20060316    Chris Steel   add tuner for STB220
003 20060320    Chris Steel   fix section filtering
004 20060511    G.Lawrance    Imported emulation speed-ups from C.Steel
005 20060922    Chris Steel   update tuner support for STB225
006 20070116    Chris Steel   update for 2.6.19.1 kernel
007 20070119    Chris Steel   fix tuner allocation
008 20070126    Chris Steel   fix tuner i2c errors
009 20070130    Mike Neill    move to NXP prefix
010 20070131    Mike Neill    Change startup text
011 20070205    Chris Steel   use hotplug firmware download by default
012 23022007    Chris Steel   add ca
013 21032007    Chris Steel   align kernel config symbols
014 04042007    Chris Steel   swap tuners over for single tuner STB225
011 20070912    MikeWhittaker Re-enable TU1216 capability, selection with prjconfig
11.1.1 20071101 MikeWhittaker (gbr02691) Add calls to gate I2C bus off for 8271 tuner
11.1.2 20071129 MikeWhittaker (gbr02691) Remove/factor dependencies on 10021 frontend into CONFIG_10021
012 25012008    Chris Steel   swap tuners back for dual tuner STB225
--------------------------------------------------------------------------------
    For consistency and standardisation retain the Section Separators.
*/

#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/vmalloc.h>
#include <linux/dvb/frontend.h>
#include <linux/firmware.h>

// For tuner configuration
//#include <prjconfig.h>

#include "phStbDemux_dvb.h"
#include "phStbDemux_ca.h"
#include "phStbDemux_common.h"
#include "tda1004x.h"
#include "tda1002x.h"

#define THIS_MODULE_DESCRIPTION "NXP DVB adapter driver"

#define I2C_NAME(s) (s)->name

#define MAX_SUPPORTED_DEMUX 4
#define MAX_SUPPORTED_TUNER 2

/* Frontend I2C addresses for different models/types */
#define I2C_TDA10046_TDA8275A_1 0x14
#define I2C_TDA10046_TDA8275A_2 0x16

#define TUNER_I2C_ADDRESS_TDA8275A_1 0x60
#define TUNER_I2C_ADDRESS_TDA8275A_2 0x61
#define TUNER_I2C_ADDRESS_TDA8275A_3 0x62
#define TUNER_I2C_ADDRESS_TDA8275A_4 0x63

#define I2C_TDA10046_TU1216_1 0x10
#define I2C_TDA10046_TU1216_2 0x14
#define TUNER_I2C_ADDRESS_TU1216  0x60

#define I2C_TDA10021_CU1216_1 0x1A
#define I2C_TDA10021_CU1216_2 0x18

#define I2C_TDA10021_TDHE1_1  0x18

#define KHZ   1000

#define dprintk( args... ) \
	do { \
		if (debug) printk(KERN_DEBUG args); \
	} while (0)

/******************************************************************
* LOCAL TYPEDEFS                                                  *
* recommendation only:                     [p]phStbDbg_<Word>+_t  *
*******************************************************************/

typedef enum {
  FE_Model_TDA8275A,
  FE_Model_TU1216,
  FE_Model_CU1216,
  FE_Model_TDHE1,
  FE_Model_COUNT
} FE_Model_t;

typedef enum fe_bandwidth {
        BANDWIDTH_8_MHZ,
        BANDWIDTH_7_MHZ,
        BANDWIDTH_6_MHZ,
        BANDWIDTH_AUTO,
        BANDWIDTH_5_MHZ,
        BANDWIDTH_10_MHZ,
        BANDWIDTH_1_712_MHZ,
} fe_bandwidth_t;

typedef struct FE_Model_Desc {
    FE_Model_t     fe_model;
    char *         fe_name;        // e.g. "CU1216"
    unsigned short * normal_i2c;   // list of poss. I2C addresses
} FE_Model_Desc_t;

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

static int dvb_phStb_probe (struct i2c_client *i2c, const struct i2c_device_id *id);

static void gate_tuner_bus(struct dvb_frontend* fe, int gate_on);

/******************************************************************
* STATIC DATA                  g[k|p|kp|pk|kpk]<Module>_<Word>+   *
*******************************************************************/

static int debug;   // module param
static struct dvb_phStb_card phStb_demux[MAX_SUPPORTED_DEMUX];

#define CONFIG_TDA10023_CU1216

/* Statically select the tuner frontend combination at build time */

#if defined(CONFIG_TDA10046_TU1216)
 static char * fe_model_name = "TU1216";         // module param
 static FE_Model_t gFE_Model = FE_Model_TU1216;  // this module will handle only gFE_Model model.
#elif defined(CONFIG_TDA10023_CU1216)
 static char * fe_model_name = "CU1216";         // module param
 static FE_Model_t gFE_Model = FE_Model_CU1216;  // this module will handle only gFE_Model model.
#elif defined(CONFIG_TDA10046_TDA8275A)
 static char * fe_model_name = "TDA8275A";         // module param
 static FE_Model_t gFE_Model = FE_Model_TDA8275A;  // this module will handle only gFE_Model model.
#elif defined(CONFIG_TDA10048_TDA18211)
 #error "No driver available for TDA10048_TDA18211"
#else
 #error "No tuner/frontend selected"
#endif

static unsigned short normal_i2c_TDA8275A[] = {I2C_TDA10046_TDA8275A_1 >> 1, I2C_TDA10046_TDA8275A_2 >> 1, I2C_CLIENT_END};
static unsigned short normal_i2c_TU1216[] = {I2C_TDA10046_TU1216_1 >> 1, I2C_TDA10046_TU1216_2 >> 1, I2C_CLIENT_END};
static unsigned short normal_i2c_CU1216[] = {I2C_TDA10021_CU1216_1 >> 1, I2C_TDA10021_CU1216_2 >> 1, I2C_CLIENT_END};
static unsigned short normal_i2c_TDHE1[]  = {I2C_TDA10021_TDHE1_1  >> 1, I2C_CLIENT_END};

static const FE_Model_Desc_t supported_FEs [FE_Model_COUNT] = {
     { FE_Model_TDA8275A, "TDA8275A" , normal_i2c_TDA8275A},
     { FE_Model_TU1216,   "TU1216" ,   normal_i2c_TU1216},
     { FE_Model_CU1216,   "CU1216" ,   normal_i2c_CU1216},
     { FE_Model_TDHE1,    "TDHE1" ,    normal_i2c_TDHE1}
};

static const u8 tda8275aSlaveAddr[] = {TUNER_I2C_ADDRESS_TDA8275A_2, TUNER_I2C_ADDRESS_TDA8275A_1};

static long gMin_Freq = (174000 * KHZ); // TODO: shouldn't this be 30MHz (lowest VHF) !
static long gMax_Freq = (862000 * KHZ); // TODO: shouldn't this be 3GHz  (Highest UHF) !

/* I2C detection variables */
static int dvb_phStb_frontend_count = 0;
static struct i2c_driver dvb_phStb_i2c_driver;
static struct i2c_client *dvb_phStb_i2c_client[MAX_SUPPORTED_DEMUX];

/*
 * Generic i2c probe
 * concerning the addresses: i2c wants 7 bit (without the r/w bit), so '>>1'
 */


const unsigned short *dvb_phStb_address_data;

/*static const FE_Model_Desc_t dvb_phStb_address_data [FE_Model_COUNT] = {
     { FE_Model_TDA8275A, "TDA8275A" , normal_i2c_TDA8275A},
     { FE_Model_TU1216,   "TU1216" ,   normal_i2c_TU1216},
     { FE_Model_CU1216,   "CU1216" ,   normal_i2c_CU1216},
     { FE_Model_TDHE1,    "TDHE1" ,    normal_i2c_TDHE1}
};*/

static unsigned short normal_i2c[] = {
        I2C_TDA10046_TDA8275A_1 >> 1, I2C_TDA10046_TDA8275A_2 >> 1,
        I2C_TDA10046_TU1216_1 >> 1, I2C_TDA10046_TU1216_2 >> 1,
        I2C_TDA10021_CU1216_1 >> 1, I2C_TDA10021_CU1216_2 >> 1,
        I2C_TDA10021_TDHE1_1  >> 1, I2C_CLIENT_END
};
static const struct i2c_device_id dvb_ids[] = {
   { "TDA8275A", 0 },
   { "TU1216", 0 },
   { "CU1216", 0 },
   { "TDHE1", 0 },
   { }
};

static struct i2c_driver dvb_phStb_i2c_driver = {
        .driver = {
            .name = "dvb_phStb",
         },
        /*.id = I2C_DRIVERID_DVB,*/
        .probe = dvb_phStb_probe,
        .id_table = dvb_ids,
        .address_list = normal_i2c,
};

static int dvb_phStb_write_to_demux(struct dvb_demux *dvbdmx, const u8 *buf, size_t len)
{
   struct dvb_phStb_card *card = dvbdmx->priv;
   int rc = 0;

   dprintk("dvb_phStb: write to demux\n");

   if (card->pdma)
   {
      rc = card->command->dvr_load(card->hw, buf, len);
   }
   else
   {
      rc = -EINVAL;
   }

   return rc;
}

static int dvb_phStb_set_ts_filter(struct dvb_demux_feed *dvbdmxfeed)
{
   struct dvb_demux      *dvbdmx = dvbdmxfeed->demux;
   struct dvb_phStb_card *card = dvbdmx->priv;
   struct phStb_ts_filter filter;
   int rc = 0;

   dprintk("dvb_phStb: set_ts_filter\n");
   dprintk("pes_type = %d, buffer size %d\n", dvbdmxfeed->pes_type, dvbdmxfeed->buffer_size);

   filter.pid        = dvbdmxfeed->pid;
   filter.ts_type    = dvbdmxfeed->ts_type;
   filter.pes_type   = dvbdmxfeed->pes_type;
   filter.callback   = dvbdmxfeed->cb.ts;
   filter.feed       = &dvbdmxfeed->feed.ts;

   if (down_interruptible(&card->lock))
   {
      dprintk("dvb_phStb_set_ts_filter failed to get mutex\n");
      return -ERESTARTSYS;
   }
   rc = card->command->set_ts_filter(card->hw, &filter, card->pdma);
   up(&card->lock);

   return rc;
}

static int dvb_phStb_del_ts_filter(struct dvb_demux_feed *dvbdmxfeed)
{
   struct dvb_demux       *dvbdmx = dvbdmxfeed->demux;
   struct dvb_phStb_card  *card   = dvbdmx->priv;
   struct phStb_ts_filter  filter;
   int rc = 0;

   dprintk("dvb_phStb: del_ts_filter\n");

   filter.pid        = dvbdmxfeed->pid;
   filter.ts_type    = dvbdmxfeed->ts_type;
   filter.pes_type   = dvbdmxfeed->pes_type;
   filter.callback   = dvbdmxfeed->cb.ts;

   if (down_interruptible(&card->lock))
   {
      dprintk("dvb_phStb_del_ts_filter failed to get mutex\n");
      return -ERESTARTSYS;
   }
   rc = card->command->del_ts_filter(card->hw, &filter, card->pdma);
   up(&card->lock);

   return rc;
}

static int dvb_phStb_set_section_filter(struct dvb_demux_feed *dvbdmxfeed)
{
   struct dvb_demux           *dvbdmx = dvbdmxfeed->demux;
   struct dvb_phStb_card      *card   = dvbdmx->priv;
   struct phStb_section_filter filter;
   int rc = 0;

   dprintk("dvb_phStb: set_section_filter pid %d\n", dvbdmxfeed->pid);
   dprintk("buffer size %d\n", dvbdmxfeed->buffer_size);
   filter.pid        = dvbdmxfeed->pid;
   filter.callback   = dvbdmxfeed->cb.sec;
   filter.filter     = &dvbdmxfeed->filter->filter;
 
   if (down_interruptible(&card->lock))
   {
      dprintk("dvb_phStb_set_section_filter failed to get mutex\n");
      return -ERESTARTSYS;
   }
   rc = card->command->set_section_filter(card->hw, &filter, card->pdma);
   up(&card->lock);
   return rc;
}

static int dvb_phStb_del_section_filter(struct dvb_demux_feed *dvbdmxfeed)
{
   struct dvb_demux           *dvbdmx = dvbdmxfeed->demux;
   struct dvb_phStb_card      *card   = dvbdmx->priv;
   struct phStb_section_filter filter;
   int rc = 0;

   dprintk("dvb_phStb: del_section_filter\n");

   filter.pid        = dvbdmxfeed->pid;
   filter.callback   = dvbdmxfeed->cb.sec;
   filter.filter     = &dvbdmxfeed->filter->filter;

   if (down_interruptible(&card->lock))
   {
      dprintk("dvb_phStb_del_section_filter failed to get mutex\n");
      return -ERESTARTSYS;
   }
   rc = card->command->del_section_filter(card->hw, &filter, card->pdma);
   up(&card->lock);

   return rc;
}

static int dvb_phStb_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
   struct dvb_demux      *dvbdmx = dvbdmxfeed->demux;
   struct dvb_phStb_card *card = dvbdmx->priv;
   int rc;

   dprintk("dvb_phStb: start_feed\n");
	
   if (!dvbdmx->dmx.frontend)
   {
      return -EINVAL;
   }

   if (down_interruptible(&card->lock))
   {
      dprintk("dvb_phStb_start_feed failed to get mutex\n");
      return -ERESTARTSYS;
   }
   card->nfeeds++;
   rc = card->nfeeds;

   if (card->pdma)
   {
      card->command->dvr_start(card->hw, card->nfeeds);
   }
   else
   {
      card->command->start(card->hw, card->nfeeds);
   }

   up(&card->lock);
   return rc;
}

static int dvb_phStb_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
   struct dvb_demux       *dvbdmx = dvbdmxfeed->demux;
   struct dvb_phStb_card  *card   = dvbdmx->priv;

   dprintk("dvb_phStb: stop_feed\n");
	
   if (!dvbdmx->dmx.frontend)
   {
      return -EINVAL;
   }	
   if (down_interruptible(&card->lock))
   {
      dprintk("dvb_phStb_stop_feed failed to get mutex\n");
      return -ERESTARTSYS;
   }
   card->nfeeds--;

   if (card->pdma)
   {
      card->command->dvr_stop(card->hw, card->nfeeds);
   }
   else
   {
      card->command->stop(card->hw, card->nfeeds);
   }

   up(&card->lock);
   return 0;
}

/**
 Gate tuner I2c bus on/off - bus should be disabled when not actively tuning

 @param fe frontend device
 @param gate_on Enable or disable tuner bus - 0: disable bus; !0: enable bus
*/
static void gate_tuner_bus(struct dvb_frontend* fe, int gate_on)
{
   if (fe != NULL && fe->ops.i2c_gate_ctrl)
   {
      fe->ops.i2c_gate_ctrl(fe, gate_on);
   }
}

#if !(defined(CONFIG_IC_EMULATION) || defined(TMFL_IC_EMULATION))
static int tu1216_pll_init(struct dvb_frontend* fe)
{
   struct dvb_phStb_card * this_device = (struct dvb_phStb_card *) fe->dvb->priv;
   static u8 tu1216_init_agc[]  = { 0x0b, 0xf5, 0x84, 0xab };
   static u8 tu1216_init_divider[] = { 0x0b, 0xf5, 0xCA, 0xab };
   struct i2c_msg tuner_write_msg = {.addr = TUNER_I2C_ADDRESS_TU1216, .flags = 0};

   u8 b0[] = { TUNER_I2C_ADDRESS_TU1216 };
   u8 b1[] = { 0 };
   struct i2c_msg tuner_read_msg[] = {{ .flags = 0,        .buf = b0, .len = 1 },
                                      { .flags = I2C_M_RD,  .buf = b1, .len = 1 }};
   // Gate tuner I2c on
   ///@todo Rewrite so single 'return' with single gate-Off
   gate_tuner_bus(fe, 1);

   dprintk("tu1216_pll_init\n");
   // setup AGC configuration (for internal AGC)
   tuner_write_msg.buf = tu1216_init_agc;
   tuner_write_msg.len = sizeof(tu1216_init_agc);

   if (i2c_transfer(this_device->i2c_adapter, &tuner_write_msg, 1) != 1)
   {
      gate_tuner_bus(fe, 0);    // Gate tuner I2c off
      return -EIO;
   }
   msleep(1);

   // setup PLL configuration for 166KHz
   tuner_write_msg.buf = tu1216_init_divider;
   tuner_write_msg.len = sizeof(tu1216_init_divider);

   if (i2c_transfer(this_device->i2c_adapter, &tuner_write_msg, 1) != 1)
   {
      gate_tuner_bus(fe, 0);    // Gate tuner I2c off
      return -EIO;
   }
   msleep(1);

   if (debug)
   {
      int ret;

      // read status

      tuner_read_msg[0].addr = TUNER_I2C_ADDRESS_TU1216;
      tuner_read_msg[1].addr = TUNER_I2C_ADDRESS_TU1216;
      ret = i2c_transfer(this_device->i2c_adapter, tuner_read_msg, 2);

      msleep(1);

      dprintk("tu1216_pll_init: Tuner STATUS=0x%X\n", b1[0]);
      if (ret != 2)
      {
         gate_tuner_bus(fe, 0);    // Gate tuner I2c off
         return -EIO;
      }
   }

    gate_tuner_bus(fe, 0);    // Gate tuner I2c off
    return 0;
}

static int tu1216_pll_set(struct dvb_frontend* fe)
{
   struct dtv_frontend_properties *params = &fe->dtv_property_cache;
   struct dvb_phStb_card *this_device = (struct dvb_phStb_card *) fe->dvb->priv;
   u8 tuner_buf[4];
   struct i2c_msg tuner_msg = {.addr = 0x60,.flags = 0,.buf = tuner_buf,.len = sizeof(tuner_buf) };
   int tuner_frequency = 0;
   u8 band, cp, filter;

   u8 b0[] = { TUNER_I2C_ADDRESS_TU1216 };
   u8 b1[] = { 0 };
   int ret;
   struct i2c_msg tuner_read_msg[] = {{ .flags = 0,        .buf = b0, .len = 1 },
                                      { .flags = I2C_M_RD,  .buf = b1, .len = 1 }};

   // Gate tuner I2c on
   ///@todo Rewrite so single 'return' with single gate-Off
   gate_tuner_bus(fe, 1);

   dprintk("tu1216_pll_set\n");
   if (debug)
   {
      // read status

      tuner_read_msg[0].addr = TUNER_I2C_ADDRESS_TU1216;
      tuner_read_msg[1].addr = TUNER_I2C_ADDRESS_TU1216;
      ret = i2c_transfer(this_device->i2c_adapter, tuner_read_msg, 2);

      msleep(1);

      dprintk("tu1216_pll_set: Tuner STATUS=0x%X\n", b1[0]);
      if (ret != 2) {
         gate_tuner_bus(fe, 0);    // Gate tuner I2c off
         return -EIO;
      }
   }

   // determine charge pump
   tuner_frequency = params->frequency + 36166000;
   if (tuner_frequency < 87000000)
   {
      gate_tuner_bus(fe, 0);    // Gate tuner I2c off
      return -EINVAL;
   }
   else if (tuner_frequency < 130000000)
      cp = 3;
   else if (tuner_frequency < 160000000)
      cp = 5;
   else if (tuner_frequency < 200000000)
      cp = 6;
   else if (tuner_frequency < 290000000)
      cp = 3;
   else if (tuner_frequency < 420000000)
      cp = 5;
   else if (tuner_frequency < 480000000)
      cp = 6;
   else if (tuner_frequency < 620000000)
      cp = 3;
   else if (tuner_frequency < 830000000)
      cp = 5;
   else if (tuner_frequency < 895000000)
      cp = 7;
   else
   {
      gate_tuner_bus(fe, 0);    // Gate tuner I2c off
      return -EINVAL;
   }

   // determine band
   if (params->frequency < 49000000)
   {
      gate_tuner_bus(fe, 0);    // Gate tuner I2c off
      return -EINVAL;
   }
   else if (params->frequency < 161000000)
      band = 1;
   else if (params->frequency < 444000000)
      band = 2;
   else if (params->frequency < 861000000)
      band = 4;
   else
   {
      gate_tuner_bus(fe, 0);    // Gate tuner I2c off
      return -EINVAL;
   }

   // setup PLL filter
   switch (params->bandwidth_hz) {
      case BANDWIDTH_6_MHZ:
         filter = 0;
         break;

      case BANDWIDTH_7_MHZ:
         filter = 0;
         break;

      case BANDWIDTH_8_MHZ:
         filter = 1;
         break;

      default:
         gate_tuner_bus(fe, 0);    // Gate tuner I2c off
         return -EINVAL;
   }

   // calculate divisor
   // ((36166000+((1000000/6)/2)) + Finput)/(1000000/6)
   tuner_frequency = (((params->frequency / 1000) * 6) + 217496) / 1000;

   // setup tuner buffer
   tuner_buf[0] = (tuner_frequency >> 8) & 0x7f;
   tuner_buf[1] = tuner_frequency & 0xff;
   tuner_buf[2] = 0xca;
   tuner_buf[3] = (cp << 5) | (filter << 3) | band;

   if (i2c_transfer(this_device->i2c_adapter, &tuner_msg, 1) != 1)
   {
      gate_tuner_bus(fe, 0);    // Gate tuner I2c off
      return -EIO;
   }

   msleep(1);
   gate_tuner_bus(fe, 0);    // Gate tuner I2c off
   return 0;
}

static int tu1216_request_firmware(struct dvb_frontend* fe, const struct firmware **fw, char* name)
{
    struct dvb_phStb_card *this_device = (struct dvb_phStb_card *) fe->dvb->priv;

    return request_firmware(fw, name, &this_device->i2c_adapter->dev);
}


static struct tda1004x_config tu1216_config[MAX_SUPPORTED_DEMUX] = {
   {
   // .demod_address is set in frontend_init 
      .invert = 1,
      .invert_oclk = 1,
      .xtal_freq  = TDA10046_XTAL_4M,
      .if_freq    = TDA10046_FREQ_3617,
      .agc_config = TDA10046_AGC_DEFAULT,
      .request_firmware = tu1216_request_firmware
   },
   {
   // .demod_address is set in frontend_init
      .invert = 1,
      .invert_oclk = 1,
      .xtal_freq  = TDA10046_XTAL_4M,
      .if_freq    = TDA10046_FREQ_3617,
      .agc_config = TDA10046_AGC_DEFAULT,
      .request_firmware = tu1216_request_firmware
   }
};

static int tda8275a_pll_init(struct dvb_frontend* fe)
{
   struct dvb_phStb_card * this_device = (struct dvb_phStb_card *) fe->dvb->priv;
   u8 tda8275a_init_data[] = {0x1e, 0xea, 0x00, 0x10, 0x00, 0x5f, 0x0c, 0x06, 0x26, 0xff, 0x40, 0x00, 0x39};
   u8 tda8275a_init_addr[] = {0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xa0, 0xb0, 0xc0};
   struct i2c_msg tuner_write_msg;
   u8     buffer[2];
   int i = 0;
   int retcode; // success (0) or error (-ve) return code

   tuner_write_msg.addr  = this_device->tuner_addr;
   tuner_write_msg.flags = 0;
   tuner_write_msg.buf   = buffer;
   tuner_write_msg.len   = 2;

#ifdef CONFIG_NXP_STB220
   {
      /* nasty hack to turn on STB220 tuner power supply */
      unsigned long *mmio = (unsigned long *)0xb7e0f000;
      unsigned long tmp;
      tmp = *(mmio + 5);
      *(mmio + 5) = (tmp & 0x7ffdf);
      tmp = *(mmio + 2);
      *(mmio + 2) = (tmp | 0x00020);
      tmp = *(mmio + 1);
      *(mmio + 1) = (tmp | 0x00020);
   }
#endif

   // Gate tuner I2c on - 'returns' must hereafter go via exit: label
   gate_tuner_bus(fe, 1);

   retcode = 0;

   do
   {
      buffer[0] = tda8275a_init_addr[i];
      buffer[1] = tda8275a_init_data[i];
      if (i2c_transfer(this_device->i2c_adapter, &tuner_write_msg, 1) != 1)
      {
          retcode = -EIO;
          goto exit;
      }
   } while (++i < sizeof(tda8275a_init_data));

   msleep(1);

exit: // common exit once tuner bus is gated on ...

   // Gate tuner I2c off
   gate_tuner_bus(fe, 0);

   return retcode;
}

static int tda8275a_pll_set(struct dvb_frontend* fe)
{
   struct dtv_frontend_properties *params = &fe->dtv_property_cache;
   struct dvb_phStb_card *this_device = (struct dvb_phStb_card *) fe->dvb->priv;
   u8 tda8275a_tune_data[] = { 0x00, 0x00, 0x00, 0x16, 0x00, 0x4b, 0x0c, 0x06, 0x24, 0xff, 0x60, 0x00, 0x39, 0x3c, 0x40};
   u8 tda8275a_tune_addr[] = { 0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xa0, 0xb0, 0xc0, 0x60, 0xa0};
   struct i2c_msg tuner_write_msg;
   static const unsigned long freq[26][4] = {{820000000, 0x1c,  1, 1},
                                             {780000000, 0x1c,  1, 0},
                                             {700000000, 0x14,  1, 1},
                                             {650000000, 0x14,  1, 0},
                                             {620000000, 0x0c,  1, 1},
                                             {550000000, 0x0c,  1, 0},
                                             {538000000, 0x0b,  1, 0},
                                             {520000000, 0x03,  1, 1},
                                             {455000000, 0x03,  1, 0},
                                             {390000000, 0x3b,  2, 0},
                                             {325000000, 0x33,  2, 0},
                                             {290000000, 0x2b,  2, 0},
                                             {269000000, 0x2a,  2, 0},
                                             {227500000, 0x22,  2, 0},
                                             {195000000, 0x5a,  4, 0},
                                             {183000000, 0x52,  4, 0},
                                             {162500000, 0x51,  4, 0},
                                             {154000000, 0x49,  4, 0},
                                             {134500000, 0x49,  4, 0},
                                             {113750000, 0x41,  4, 0},
                                             { 97500000, 0x79,  8, 0},
                                             { 81250000, 0x70,  8, 0},
                                             { 67250000, 0x68,  8, 0},
                                             { 56875000, 0x60,  8, 0},
                                             { 49000000, 0x98, 16, 0},
                                             {        0,    0,  0, 0}};
   u8            buffer[2];
   unsigned long tunerFreq;
   unsigned char Dmask;
   unsigned long D;
   unsigned long N;
   unsigned long scr;
   int i = 0;
   int retcode; // success (0) or error (-ve) return code

   tuner_write_msg.addr  = this_device->tuner_addr;
   tuner_write_msg.flags = 0;
   tuner_write_msg.buf   = buffer;
   tuner_write_msg.len   = 2;

   switch (params->bandwidth_hz)
   {
      case BANDWIDTH_6_MHZ:
         tunerFreq = params->frequency + 4000000;
         break;

      case BANDWIDTH_7_MHZ:
         tunerFreq = params->frequency + 4500000;
         break;

      case BANDWIDTH_8_MHZ:
         tunerFreq = params->frequency + 5000000;
         break;

      default:
         return -EINVAL;
   }

   if (tunerFreq > 900000000)
   {
      return -EINVAL;
   }
   else
   {
      int i = 0;

      while(freq[i][0] > tunerFreq)
      {
         i++;
      }

      if (freq[i][0] == 0)
      {
         return -EINVAL;
      }

      Dmask = freq[i][1];
      D     = freq[i][2];
      scr   = freq[i][3];
   }

   N = (tunerFreq * D) / 250000;

   dprintk("tda8275a_pll_set freq = %u, D = %ld, N = %ld\n", params->frequency, D, N);

   tda8275a_tune_data[0] &= 0xc0;
   tda8275a_tune_data[0] |= ((N >> 6) & 0x3f);
   tda8275a_tune_data[1] &= 0x03;
   tda8275a_tune_data[1] |= ((N << 2) & 0xfc);
   tda8275a_tune_data[4]  = Dmask;
   tda8275a_tune_data[5] &= 0xcf;
   tda8275a_tune_data[5] |= ((tunerFreq < 550000000) ? 0x10 : 0x00);

   // Gate tuner I2c on - 'returns' must hereafter go via exit: label
   gate_tuner_bus(fe, 1);

   retcode = 0;

   do
   {
      buffer[0] = tda8275a_tune_addr[i];
      buffer[1] = tda8275a_tune_data[i];
      if (i2c_transfer(this_device->i2c_adapter, &tuner_write_msg, 1) != 1)
      {
          retcode = -EIO;
          goto exit;
      }
   } while (++i < sizeof(tda8275a_tune_data));

   msleep(2);

   tda8275a_tune_data[3] &= 0xf0;
   tda8275a_tune_data[3] |= scr;

   buffer[0] = tda8275a_tune_addr[3];
   buffer[1] = tda8275a_tune_data[3];
   if (i2c_transfer(this_device->i2c_adapter, &tuner_write_msg, 1) != 1)
   {
       retcode = -EIO;
       goto exit;
   }

   msleep(550);

   tda8275a_tune_data[5] &= 0xf0;
   tda8275a_tune_data[5] |= 0x0f;
   buffer[0] = tda8275a_tune_addr[5];
   buffer[1] = tda8275a_tune_data[5];
   if (i2c_transfer(this_device->i2c_adapter, &tuner_write_msg, 1) != 1)
   {
       retcode = -EIO;
       goto exit;
   }

exit: // common exit once tuner bus is gated on ...

   // Gate tuner I2c off
   gate_tuner_bus(fe, 0);

   return retcode;
}

static struct tda1004x_config tda8275a_config[MAX_SUPPORTED_DEMUX] = {
   {
   // .demod_address is set in frontend_init 
      .invert = 1,
      .invert_oclk = 1,
      .xtal_freq  = TDA10046_XTAL_4M,
      .if_freq    = TDA10046_FREQ_045,
      .agc_config = TDA10046_AGC_TDA827X,
      .request_firmware = tu1216_request_firmware
   },
   {
   // .demod_address is set in frontend_init
      .invert = 1,
      .invert_oclk = 1,
      .xtal_freq  = TDA10046_XTAL_4M,
      .if_freq    = TDA10046_FREQ_045,
      .agc_config = TDA10046_AGC_TDA827X,
      .request_firmware = tu1216_request_firmware
   }
};

#if defined(CONFIG_10021)
static int alps_tdhe1_pll_set(struct dvb_frontend *fe, struct dvb_frontend_parameters *params)
{
	struct dvb_phStb_card *this_device = (struct dvb_phStb_card *) fe->dvb->priv;
	u8 buf[4];
	struct i2c_msg msg = {.addr = 0x61,.flags = 0,.buf = buf,.len = sizeof(buf) };

#define TUNER_MUL 62500
#define _IF_ 36125000
	u32 div;

	div = (params->frequency + _IF_ + (TUNER_MUL / 2)) / TUNER_MUL;
	buf[0] = (div >> 8) & 0x7f;
	buf[1] = div & 0xff;
	buf[2] = 0x85;
	buf[3] = (params->frequency < (153000000 + _IF_) ? 0x01 :
             params->frequency < (430000000 + _IF_) ? 0x02 :
             params->frequency < (822000000 + _IF_) ? 0x08 : 0x88);

	if (i2c_transfer(this_device->i2c_adapter, &msg, 1) != 1)
   {
		return -EIO;
   }
   msleep(1);

	return 0;
}

static struct tda10021_config alps_tdhe1_config[MAX_SUPPORTED_TUNER];

static u8 read_pwm_tdhe1(struct dvb_phStb_card *card)
{
   u8 b = 0x34;	/* 0xff Modified by ZHQ 11/07/2005 */
   u8 pwm;
   struct i2c_msg msg[] = { {.addr = 0x0c,.flags = 0,       .buf = &b,  .len = 1},
                            {.addr = 0x0c,.flags = I2C_M_RD,.buf = &pwm,.len = 1}
                          };

   if ((i2c_transfer(card->i2c_adapter, msg, 2) != 2) || (pwm == 0xff))
   {
		pwm = 0x48;
   }
   return pwm;
}

static int philips_cu1216_pll_set(struct dvb_frontend *fe)
{
   struct dtv_frontend_properties *params = &fe->dtv_property_cache;
	struct dvb_phStb_card *this_device = (struct dvb_phStb_card *) fe->dvb->priv;
	u8 buf[4];
	struct i2c_msg msg = {.addr = 0x60,.flags = 0,.buf = buf,.len = sizeof(buf) };

#define TUNER_MUL 62500

	u32 div = (params->frequency + 36125000 + TUNER_MUL / 2) / TUNER_MUL;

	buf[0] = (div >> 8) & 0x7f;
	buf[1] = div & 0xff;
	buf[2] = 0xCe;
	buf[3] = (params->frequency < 174500000 ? 0x01 :
		  params->frequency < 454000000 ? 0x02 : 0x04);
	if (i2c_transfer(this_device->i2c_adapter, &msg, 1) != 1)
		return -EIO;
	return 0;
}


static struct tda10021_config cu1216_config[MAX_SUPPORTED_TUNER];

static u8 read_pwm_cu1216(struct dvb_phStb_card *card)
{
   u8 b = 0x34;
   u8 pwm;
   struct i2c_msg msg[] = { {.addr = 0x0c,.flags = 0,       .buf = &b,  .len = 1},
                            {.addr = 0x0c,.flags = I2C_M_RD,.buf = &pwm,.len = 1}
                          };

   if ((i2c_transfer(card->i2c_adapter, msg, 2) != 2) || (pwm == 0xff))
   {
		pwm = 0x48;
   }
   return pwm;
}

#endif /*CONFIG_10021*/


static void frontend_init(struct dvb_phStb_card *card, int instance_count)
{
   /* Check to see if a frontend is attached */
   if ((card->i2c_adapter) && (dvb_phStb_i2c_client[instance_count]))
   {
      switch (gFE_Model) 
      {
#if defined(CONFIG_10021)
      case FE_Model_TDHE1:
              /* Set up the I2C address */
              alps_tdhe1_config[instance_count].demod_address = dvb_phStb_i2c_client[instance_count]->addr;
              card->fe = dvb_attach(tda10021_attach, &alps_tdhe1_config[instance_count], card->i2c_adapter, read_pwm_tdhe1(card));
              card->fe->ops.tuner_ops.set_params = alps_tdhe1_pll_set;
            break;
        case FE_Model_CU1216:
              /* Set up the I2C address */
              cu1216_config[instance_count].demod_address = dvb_phStb_i2c_client[instance_count]->addr;
              card->fe = dvb_attach(tda10021_attach, &cu1216_config[instance_count], card->i2c_adapter, read_pwm_cu1216(card));
              card->fe->ops.tuner_ops.set_params = philips_cu1216_pll_set;
            break;
#endif /*CONFIG_10021*/
        case FE_Model_TU1216:
              /* Set up the I2C address */
              tu1216_config[instance_count].demod_address = dvb_phStb_i2c_client[instance_count]->addr;
              card->fe = dvb_attach(tda10046_attach, &tu1216_config[instance_count], card->i2c_adapter);
              card->fe->ops.tuner_ops.init = tu1216_pll_init;
              card->fe->ops.tuner_ops.set_params = tu1216_pll_set;
            break;
        case FE_Model_TDA8275A:
              /* Set up the I2C address */
              tda8275a_config[instance_count].demod_address = dvb_phStb_i2c_client[instance_count]->addr;
              card->tuner_addr = tda8275aSlaveAddr[instance_count];
              card->fe = dvb_attach(tda10046_attach, &tda8275a_config[instance_count], card->i2c_adapter);
              card->fe->ops.tuner_ops.init       = tda8275a_pll_init;
              card->fe->ops.tuner_ops.set_params = tda8275a_pll_set;
            break;
        default:
              card->fe = NULL;
            break;
      }

      if (card->fe != NULL)
      {
         card->fe->ops.info.frequency_min = gMin_Freq;
         card->fe->ops.info.frequency_max = gMax_Freq;
         if (dvb_register_frontend(&card->dvb_adapter, card->fe))
         {
            dprintk("dvb-phStb: Frontend registration failed!\n");
            dvb_frontend_detach(card->fe);
            card->fe = NULL;
         }
      }
   }
}
#endif

static int dvb_phStb_load_card(struct dvb_phStb_card *card, int instance_count, struct phStb_capabilities *caps)
{
   int result;

   dprintk("dvb_phStb: dvb_phStb_load_card\n");

   if ((result = dvb_register_adapter(&card->dvb_adapter, card->card_name, THIS_MODULE, NULL, adapter_nr)) < 0)
   {
      dprintk("dvb_phStb: dvb_register_adapter failed (errno = %d)\n", result);
   }

   card->dvb_adapter.priv = card;

   if ((instance_count < MAX_SUPPORTED_DEMUX) &&
       (dvb_phStb_i2c_client[instance_count] != NULL) &&
       (card->i2c_adapter = dvb_phStb_i2c_client[instance_count]->adapter) != NULL)
   {
      dprintk("dvb_phStb: got I2C adapter (%d)\n", instance_count);
   }

   card->pdma = 0;

   card->nfeeds = 0;
   card->ndescramblers = caps->numDescramblers;
   card->demux.dmx.capabilities = DMX_TS_FILTERING | DMX_SECTION_FILTERING;

   card->demux.priv               = card;
   card->demux.filternum          = caps->numPesFilters + caps->numSectionFilters;
   card->demux.feednum            = caps->numPesFilters + caps->numSectionFilters;
   card->demux.start_feed         = dvb_phStb_start_feed;
   card->demux.stop_feed          = dvb_phStb_stop_feed;
   card->demux.set_ts_filter      = dvb_phStb_set_ts_filter;
   card->demux.del_ts_filter      = dvb_phStb_del_ts_filter;
   card->demux.set_section_filter = dvb_phStb_set_section_filter;
   card->demux.del_section_filter = dvb_phStb_del_section_filter;
   card->demux.write_to_demux     = dvb_phStb_write_to_demux;
	
   if ((result = dvb_dmx_init(&card->demux)) < 0)
   {
      dprintk("dvb_phStb: dvb_dmx_init failed (errno = %d)\n", result);

      dvb_unregister_adapter(&card->dvb_adapter);
      return result;
   }

   card->dmxdev.filternum    = caps->numPesFilters + caps->numSectionFilters;
   card->dmxdev.demux        = &card->demux.dmx;
   card->dmxdev.capabilities = 0;
	
   if ((result = dvb_dmxdev_init(&card->dmxdev, &card->dvb_adapter)) < 0)
   {
      dprintk("dvb_phStb: dvb_dmxdev_init failed (errno = %d)\n", result);

      dvb_dmx_release(&card->demux);
      dvb_unregister_adapter(&card->dvb_adapter);
      return result;
   }

   card->fe_hw.source = DMX_FRONTEND_0;

   if ((result = card->demux.dmx.add_frontend(&card->demux.dmx, &card->fe_hw)) < 0)
   {
      dprintk("dvb_phStb: dvb_dmx_init failed (errno = %d)\n", result);

      dvb_dmxdev_release(&card->dmxdev);
      dvb_dmx_release(&card->demux);
      dvb_unregister_adapter(&card->dvb_adapter);
      return result;
   }
	
   if ((result = card->demux.dmx.connect_frontend(&card->demux.dmx, &card->fe_hw)) < 0)
   {
      dprintk("dvb_phStb: dvb_dmx_init failed (errno = %d)\n", result);

      card->demux.dmx.remove_frontend(&card->demux.dmx, &card->fe_hw);
      dvb_dmxdev_release(&card->dmxdev);
      dvb_dmx_release(&card->demux);
      dvb_unregister_adapter(&card->dvb_adapter);
      return result;
   }

   if ((result = phStbDemux_ca_register(&card->dvb_adapter, &card->ca_dev, card)) < 0)
   {
      dprintk("dvb_pnx8550: phStb_ca_register failed (errno = %d)\n", result);
   }

   dvb_net_init(&card->dvb_adapter, &card->dvbnet, &card->demux.dmx);

#if !(defined(CONFIG_IC_EMULATION) || defined(TMFL_IC_EMULATION))
   frontend_init(card, instance_count);
#else
   printk("IC_EMULATION : phStbDemux Skipping frontend_init\n");
#endif
   return 0;
}

static int dvb_phStb_load_dvr(struct dvb_phStb_card *card, struct phStb_capabilities *caps)
{
   int result;

   dprintk("dvb_phStb: dvb_phStb_load_dvr\n");

   if ((result = dvb_register_adapter(&card->dvb_adapter, card->card_name, THIS_MODULE, NULL, adapter_nr)) < 0)
   {
       dprintk("dvb_phStb: dvb_register_adapter failed (errno = %d)\n", result);
       return result;		
   }

   card->dvb_adapter.priv = card;
   card->pdma = 1;
   card->nfeeds = 0;
   card->ndescramblers = caps->numDescramblers;

   card->demux.dmx.capabilities = DMX_TS_FILTERING | DMX_SECTION_FILTERING | DMX_MEMORY_BASED_FILTERING;

   card->demux.priv               = card;
   card->demux.filternum          = caps->numPesFilters + caps->numSectionFilters;
   card->demux.feednum            = caps->numPesFilters + caps->numSectionFilters;
   card->demux.start_feed         = dvb_phStb_start_feed;
   card->demux.stop_feed          = dvb_phStb_stop_feed;
   card->demux.set_ts_filter      = dvb_phStb_set_ts_filter;
   card->demux.del_ts_filter      = dvb_phStb_del_ts_filter;
   card->demux.set_section_filter = dvb_phStb_set_section_filter;
   card->demux.del_section_filter = dvb_phStb_del_section_filter;
   card->demux.write_to_demux     = dvb_phStb_write_to_demux;
	
   if ((result = dvb_dmx_init(&card->demux)) < 0)
   {
      dprintk("dvb_phStb: dvb_dmx_init failed (errno = %d)\n", result);

      dvb_unregister_adapter(&card->dvb_adapter);
      return result;
   }

   card->dmxdev.filternum    = caps->numPesFilters + caps->numSectionFilters;
   card->dmxdev.demux        = &card->demux.dmx;
   card->dmxdev.capabilities = 0;
	
   if ((result = dvb_dmxdev_init(&card->dmxdev, &card->dvb_adapter)) < 0)
   {
      dprintk("dvb_phStb: dvb_dmxdev_init failed (errno = %d)\n", result);

      dvb_dmx_release(&card->demux);
      dvb_unregister_adapter(&card->dvb_adapter);
      return result;
   }

   card->fe_hw.source = DMX_MEMORY_FE;

   if ((result = card->demux.dmx.add_frontend(&card->demux.dmx, &card->fe_hw)) < 0)
   {
      dprintk("dvb_phStb: dvb_dmx_init failed (errno = %d)\n", result);

      dvb_dmxdev_release(&card->dmxdev);
      dvb_dmx_release(&card->demux);
      dvb_unregister_adapter(&card->dvb_adapter);
      return result;
   }
	
   if ((result = card->demux.dmx.connect_frontend(&card->demux.dmx, &card->fe_hw)) < 0)
   {
      dprintk("dvb_phStb: dvb_dmx_init failed (errno = %d)\n", result);

      card->demux.dmx.remove_frontend(&card->demux.dmx, &card->fe_hw);
      dvb_dmxdev_release(&card->dmxdev);
      dvb_dmx_release(&card->demux);
      dvb_unregister_adapter(&card->dvb_adapter);
      return result;
   }

   if ((result = phStbDemux_ca_register(&card->dvb_adapter, &card->ca_dev, card)) < 0)
   {
      dprintk("dvb_pnx8550: phStb_ca_register failed (errno = %d)\n", result);
   }

   return 0;
}

int dvb_phStb_register(int index, void *hw, struct phStb_demux_device *command, struct phStb_capabilities *caps)
{
   struct dvb_phStb_card *card = &phStb_demux[index];
   int ret;

   dprintk("dvb_phStb: registering card %d\n", index);

   memset(card, 0, sizeof(struct dvb_phStb_card));
   sema_init(&card->lock, 1);

   sprintf(card->card_name, "phStb%d", index);
   card->hw = hw;
   card->command = command;

   ret = dvb_phStb_load_card(card, index, caps);

   return ret;
}

int dvb_phStb_dvr_register(int index, void *hw, struct phStb_demux_device *command, struct phStb_capabilities *caps)
{
   struct dvb_phStb_card *card = &phStb_demux[index];
   int ret;

   dprintk("dvb_phStb: registering pdma %d\n", index);

   memset(card, 0, sizeof(struct dvb_phStb_card));
   sema_init(&card->lock, 1);

   sprintf(card->card_name, "phStbDvr%d", index);
   card->hw = hw;
   card->command = command;

   ret = dvb_phStb_load_dvr(card, caps);

   return ret;
}

int dvb_phStb_remove(int index)
{
   struct dvb_phStb_card *card = &phStb_demux[index];
		
   dprintk("dvb_phStb: unloading card %d\n", index);

   card->command->stop(card->hw, 0);
   phStbDemux_ca_unregister(card->ca_dev);
   dvb_net_release(&card->dvbnet);
   card->demux.dmx.remove_frontend(&card->demux.dmx, &card->fe_hw);
   dvb_dmxdev_release(&card->dmxdev);
   dvb_dmx_release(&card->demux);

   if (card->fe)
   {
      dvb_unregister_frontend(card->fe);
      dvb_unregister_adapter(&card->dvb_adapter);
   }

   return 0;
}

int dvb_phStb_dvr_remove(int index)
{
   struct dvb_phStb_card *card = &phStb_demux[index];
		
   dprintk("dvb_phStb: unloading dvr %d\n", index);

   card->command->dvr_stop(card->hw, 0);
   phStbDemux_ca_unregister(card->ca_dev);
   card->demux.dmx.remove_frontend(&card->demux.dmx, &card->fe_hw);
   dvb_dmxdev_release(&card->dmxdev);
   dvb_dmx_release(&card->demux);

   if (card->fe)
   {
      dvb_unregister_frontend(card->fe);
      dvb_unregister_adapter(&card->dvb_adapter);
   }
 
   return 0;
}

EXPORT_SYMBOL(dvb_phStb_register);
EXPORT_SYMBOL(dvb_phStb_dvr_register);
EXPORT_SYMBOL(dvb_phStb_remove);
EXPORT_SYMBOL(dvb_phStb_dvr_remove);

/* -------------------------- I2C Detection Functions ------------------------- */
#if !(defined(CONFIG_IC_EMULATION) || defined(TMFL_IC_EMULATION))
static int dvb_phStb_probe (struct i2c_client *i2c, const struct i2c_device_id *id)
{
    int rv;
    int tuner_nr = 0;

    dprintk("%s: detecting dvb phStb client on address 0x%x (adapter %d)\n",
            THIS_MODULE_DESCRIPTION, i2c->addr << 1, i2c->adapter->nr);
    while ((dvb_phStb_address_data[tuner_nr] != i2c->addr) &&
           (tuner_nr < MAX_SUPPORTED_TUNER))
    {
       tuner_nr++;
    }

    dprintk("Tuner nr: %d\n", tuner_nr);

    /* Check for the maximum number of frontends connected and that they */
    /* have been found on the currect buses */
    if ((dvb_phStb_frontend_count == MAX_SUPPORTED_TUNER) ||
        (tuner_nr >= MAX_SUPPORTED_TUNER))
    {
        dprintk("dvb_phStb_frontend_count\n");
        return 0;
    }

    /* Check if the adapter supports the needed features */
    if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_SMBUS_READ_BYTE | I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
    {
      dprintk("i2c_check_functionality\n");
        return 0;
    }

    /* Create the I2C client data structure */
    dvb_phStb_i2c_client[tuner_nr] = kmalloc(sizeof(struct i2c_client), GFP_KERNEL);
    if (dvb_phStb_i2c_client[tuner_nr] == 0)
    {
      dprintk("ENOMEM\n");
        return -ENOMEM;
    }

    memset(dvb_phStb_i2c_client[tuner_nr], 0, sizeof(struct i2c_client));

    /* Create the I2C client data structure */
    dvb_phStb_i2c_client[tuner_nr] = i2c; //XXX: Checkme
    //i2c_set_clientdata(i2c, pcf8563);

    dvb_phStb_frontend_count++;

    dprintk("%s: found device on adapter %s (0x%x)\n",
            THIS_MODULE_DESCRIPTION,
            I2C_NAME(i2c->adapter), 0/*i2c->adapter->id*/);

    return 0;
}

#endif

static int __init dvb_phStb_init(void)
{
   int retval = 0;
   int i, j;

   dvb_phStb_frontend_count = 0;
   dvb_phStb_i2c_client[0]  = NULL;
   dvb_phStb_i2c_client[1]  = NULL;

   for (i=0; i< FE_Model_COUNT; i++)
   {
        for (j=0; fe_model_name[j]!=0; j++)
        {
            fe_model_name[j] = toupper (fe_model_name[j]);
        }

        if (0 == strcmp(fe_model_name, supported_FEs[i].fe_name))
        {
            gFE_Model = supported_FEs[i].fe_model;
            dvb_phStb_address_data = supported_FEs[i].normal_i2c;
            break;
        }
   }
   dprintk("gFE_Model=%d \n", gFE_Model);


#if !(defined(CONFIG_IC_EMULATION) || defined(TMFL_IC_EMULATION))
   i2c_add_driver(&dvb_phStb_i2c_driver);
#else
   printk("IC_EMULATION : phStbDemux Skipping i2c_add_driver\n");
#endif
   printk ("%s (%s-%s)\n", THIS_MODULE_DESCRIPTION, __DATE__, __TIME__);
   return retval;
}

static void __exit dvb_phStb_exit(void)
{

#if !(defined(CONFIG_IC_EMULATION) || defined(TMFL_IC_EMULATION))
   i2c_del_driver(&dvb_phStb_i2c_driver);
#else
   printk("IC_EMULATION : phStbDemux Skipping i2c_del_driver\n");
#endif
   printk("Shutting down %s\n", THIS_MODULE_DESCRIPTION);
}

module_init(dvb_phStb_init);
module_exit(dvb_phStb_exit);

MODULE_DESCRIPTION(THIS_MODULE_DESCRIPTION);
MODULE_AUTHOR("Chris Steel");
MODULE_LICENSE("GPL");

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, " Turn on (=1) debugging. Default off (=0).");

module_param(gMin_Freq, long, 0444);
MODULE_PARM_DESC(gMin_Freq, " FrontEnd's Min. Freq. Default=174000000");

module_param(gMax_Freq, long, 0444);
MODULE_PARM_DESC(gMax_Freq, " FrontEnd's Max. Freq. Default=862000000");

module_param(fe_model_name, charp, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(fe_model_name, " FrontEnd/tuner type. Build settings: TU1216, TDA8275A ...");
