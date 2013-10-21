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


/***********************************************
* INCLUDE FILES                                *
************************************************/
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/timer.h>
#include <linux/poll.h>
#include <linux/dvb/ca.h>

#include "phStbDemux_dvb.h"
#include "phStbDemux_ca.h"

/***********************************************
* LOCAL MACROS                                 *
* recommendation only: <MODULE><_WORD>+        *
************************************************/

/******************************************************************
* LOCAL TYPEDEFS                                                  *
* recommendation only:                     [p]phStbDbg_<Word>+_t  *
*******************************************************************/

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

/******************************************************************
* STATIC DATA                  g[k|p|kp|pk|kpk]<Module>_<Word>+   *
*******************************************************************/

/******************************************************************
* EXPORTED DATA                  g[k|p|kp|pk|kpk]phStbDbg_<Word>+ *
*******************************************************************/

/*******************************************************************************
* FUNCTION IMPLEMENTATION  <Module>[_<Word>+] for static functions             *
*                          tm[<layer>]<Module>[_<Word>+] for exported functions*
********************************************************************************/

static int dvb_ca_open(struct inode *inode, struct file *file)
{
   int err = dvb_generic_open(inode, file);

   if (err < 0)
   {
      return err;
   }

   return 0;
}

static unsigned int dvb_ca_poll (struct file *file, poll_table *wait)
{
   return 0;
}

static int dvb_ca_ioctl(struct inode *inode, struct file *file, unsigned int cmd, void *parg)
{
   struct dvb_device     *dvbdev  = (struct dvb_device *) file->private_data;
   struct dvb_phStb_card *card    = (struct dvb_phStb_card *) dvbdev->priv;

   switch (cmd)
   {
      case CA_RESET:
         break;

      case CA_GET_CAP:
      {
         ca_caps_t *cap  = (ca_caps_t *)parg;

         cap->slot_num   = 1;
         cap->slot_type  = CA_SC | CA_DESCR;
         cap->descr_num  = card->ndescramblers;
         cap->descr_type = CA_ECD;
         break;
      }

      case CA_GET_SLOT_INFO:
      {
         ca_slot_info_t *info=(ca_slot_info_t *)parg;

         if (info->num > 0)
         {
            return -EINVAL;
         }
         info->type  = CA_SC | CA_DESCR;
         info->flags = CA_CI_MODULE_PRESENT | CA_CI_MODULE_READY;
         break;
      }

      case CA_GET_MSG:
         break;

      case CA_SEND_MSG:
         break;

      case CA_GET_DESCR_INFO:
      {
         ca_descr_info_t *info = (ca_descr_info_t *)parg;

         info->num  = card->ndescramblers;
         info->type = CA_ECD;
         break;
      }

      case CA_SET_DESCR:
      {
         ca_descr_t *descr = (ca_descr_t*) parg;
         int         index;

         if (descr->index >= card->ndescramblers)
         {
            return -EINVAL;
         }

         if (descr->parity > 1)
         {
            return -EINVAL;
         }

         if (card->pdma)
         {
            index = descr->index + card->ndescramblers;
         }
         else
         {
            index = descr->index;
         }

         card->command->set_desc_word(card->hw, index, descr->parity, descr->cw);
         break;
      }

      case CA_SET_PID:
      {
         ca_pid_t *pid = (ca_pid_t*)parg;
         int       index;

         if (pid->index >= card->ndescramblers)
         {
            return -EINVAL;
         }

         if (card->pdma)
         {
            index = pid->index + card->ndescramblers;
         }
         else
         {
            index = pid->index;
         }

         if (card->command->set_desc_pid(card->hw, index, pid->pid))
         {
            return -EINVAL;
         }
         break;
      }

      default:
         return -EINVAL;
   }
   return 0;
}

static ssize_t dvb_ca_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
   return -EINVAL;
}

static ssize_t dvb_ca_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
   return -EINVAL;
}

static struct file_operations dvb_ca_fops = {
   .owner   = THIS_MODULE,
   .read    = dvb_ca_read,
   .write   = dvb_ca_write,
   .unlocked_ioctl   = dvb_generic_ioctl,
   .open    = dvb_ca_open,
   .release = dvb_generic_release,
   .poll    = dvb_ca_poll,
};

static struct dvb_device dvbdev_ca = {
   .priv         = NULL,
   .users        = 1,
   .writers      = 1,
   .fops         = &dvb_ca_fops,
   .kernel_ioctl = dvb_ca_ioctl,
};

int phStbDemux_ca_register(struct dvb_adapter *adapter, struct dvb_device **ca, struct dvb_phStb_card *card)
{
   return dvb_register_device(adapter, ca, &dvbdev_ca, card, DVB_DEVICE_CA);
}

void phStbDemux_ca_unregister(struct dvb_device *ca)
{
   dvb_unregister_device(ca);
}

EXPORT_SYMBOL(phStbDemux_ca_register);
EXPORT_SYMBOL(phStbDemux_ca_unregister);
