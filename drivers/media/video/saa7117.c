/*
 * saa7117 - Philips SAA7117H video decoder driver version 0.0.1
 *
 * Copyright (C) 2006 Mike Neill <mike.neill@philips.com>
 *
 * Based on saa7111 driver by Dave Perks
 *
 * Copyright (C) 1998 Dave Perks <dperks@ibm.net>
 *
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
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/miscdevice.h>

#include <linux/slab.h>

#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/signal.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <linux/sched.h>
#include <asm/segment.h>
#include <linux/types.h>

#include <linux/videodev.h>
#include <asm/uaccess.h>

MODULE_DESCRIPTION("Philips SAA7117H video decoder driver");
MODULE_AUTHOR("Mike Neill");
MODULE_LICENSE("GPL");

#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#define I2C_NAME(x) (x)->name
#define DEVNAME "saa7117"

#include <linux/video_decoder.h>

static int debug = 0;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

#define dprintk(num, format, args...) \
    do { \
        if (debug >= num) \
            printk(format, ##args); \
    } while (0)

/* ----------------------------------------------------------------------- */

struct saa7117 {
    unsigned char reg[0x100 * 2];

    int norm;
    int input;
    int enable;
    int bright;
    int contrast;
    int hue;
    int sat;
    int playback;
};

static ssize_t saa7117_fileread(struct file *file, char __user *buf, size_t count, loff_t *pos);

static int saa7117_command ( struct inode  *inode, struct file   *file,
                              unsigned int   cmd,   unsigned long  arg);

static struct file_operations   saa7117_fops    =   {
    .owner          =   THIS_MODULE,
    .read           =   saa7117_fileread,
    .open           =   NULL,
    .release        =   NULL,
    .ioctl          =   saa7117_command,
};

static struct miscdevice saa7117_MiscDev = {
     .minor =   MISC_DYNAMIC_MINOR,
     .name  =   DEVNAME,
     .fops  =   &saa7117_fops
};

static struct i2c_client *saa7117_client;

#define   I2C_SAA7117        0x42
#define   I2C_SAA7117A       0x40

#define   I2C_DELAY          10

#define SAA_7117_NTSC_HSYNC_START       (-17)
#define SAA_7117_NTSC_HSYNC_STOP        (-32)

#define SAA_7117_NTSC_HOFFSET           (6)
#define SAA_7117_NTSC_VOFFSET           (10)
#define SAA_7117_NTSC_WIDTH             (720)
#define SAA_7117_NTSC_HEIGHT            (250)

#define SAA_7117_SECAM_HSYNC_START      (-17)
#define SAA_7117_SECAM_HSYNC_STOP       (-32)

#define SAA_7117_SECAM_HOFFSET          (2)
#define SAA_7117_SECAM_VOFFSET          (10)
#define SAA_7117_SECAM_WIDTH            (720)
#define SAA_7117_SECAM_HEIGHT           (300)

#define SAA_7117_PAL_HSYNC_START        (-17)
#define SAA_7117_PAL_HSYNC_STOP         (-32)

#define SAA_7117_PAL_HOFFSET            (2)
#define SAA_7117_PAL_VOFFSET            (10)
#define SAA_7117_PAL_WIDTH              (720)
#define SAA_7117_PAL_HEIGHT             (300)

#define SAA_7117_VERTICAL_CHROMA_OFFSET (0)
#define SAA_7117_VERTICAL_LUMA_OFFSET   (0)

#define REG_ADDR(x) (((x) << 1) + 1)
#define LOBYTE(x) ((unsigned char)((x) & 0xff))
#define HIBYTE(x) ((unsigned char)(((x) >> 8) & 0xff))
#define LOWORD(x) ((unsigned short int)((x) & 0xffff))
#define HIWORD(x) ((unsigned short int)(((x) >> 16) & 0xffff))

#define BUFFER_LENGTH (10)

/* In Status byte 2 (Reg 0x1f) FIDT bit = 0 for 50Hz, 1 for 60Hz */
#define	FIDT 0x20

/* ----------------------------------------------------------------------- */
static ssize_t saa7117_fileread(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
    ssize_t retval = 0;

    if (*pos == 0)
    {
        char kernBuf[10] = {0,0,0,0,0,0,0,0,0,0};

        if (count > BUFFER_LENGTH)
        {
            count = BUFFER_LENGTH;
        }

        if ( copy_to_user(buf, kernBuf, count) )
        {
            printk(KERN_WARNING "%s: Unable to copy_to_user all data during read.\n", __FILE__);
            retval = -EFAULT;
        }
        else
        {
            *pos += count;
            retval = count;
        }
    }

    return retval;
}
/* ----------------------------------------------------------------------- */

static inline int
saa7117_write (struct i2c_client *client,
           u8                 reg,
           u8                 value)
{
    /*struct saa7117 *decoder = i2c_get_clientdata(client);*/
    /*decoder->reg[reg] = value;*/
    return i2c_smbus_write_byte_data(client, reg, value);
}

static int
saa7117_write_block (struct i2c_client *client,
             const u8          *data,
             unsigned int       len)
{
    int ret = -1;
    u8 reg;

    /* the saa7117 has an autoincrement function, use it if
     * the adapter understands raw I2C */
    if (i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        /* do raw I2C, not smbus compatible */
        /*struct saa7117 *decoder = i2c_get_clientdata(client);*/
        struct i2c_msg msg;
        u8 block_data[32];

        msg.addr = client->addr;
        msg.flags = 0;
        while (len >= 2) {
            msg.buf = (char *) block_data;
            msg.len = 0;
            block_data[msg.len++] = reg = data[0];
            do {
                block_data[msg.len++] =
                    /*decoder->reg[reg++] =*/ data[1];
                len -= 2;
                data += 2;
            } while (len >= 2 && data[0] == reg &&
                 msg.len < 32);
            if ((ret = i2c_transfer(client->adapter,
                        &msg, 1)) < 0)
                break;
        }
    } else {
        /* do some slow I2C emulation kind of thing */
        while (len >= 2) {
            reg = *data++;
            if ((ret = saa7117_write(client, reg,
                         *data++)) < 0)
                break;
            len -= 2;
        }
    }

    return ret;
}

static inline int
saa7117_read (struct i2c_client *client,
          u8                 reg)
{
    return i2c_smbus_read_byte_data(client, reg);
}

/* ----------------------------------------------------------------------- */

// initially set PAL, composite


static const unsigned char init[] = {
    0x00, 0x00,
    0x01, 0x17,
    0x02, 0x00,
    0x03, 0x40,
    0x04, 0xe0,
    0x05, 0xe0,
    0x06, 0xeb,
    0x07, 0xe0,
    0x08, 0xb0,
    0x09, 0x40,
    0x0a, 0x80,
    0x0b, 0x44,
    0x0c, 0x40,
    0x0d, 0x00,
    0x0e, 0x83,
    0x0f, 0x52,
    0x10, 0x06,
    0x11, 0x00,
    0x12, 0x8f,
    0x13, 0x80,
    0x14, 0x00,
    0x15, 0x10,
    0x16, 0xfe,
    0x17, 0x28,
    0x18, 0x40,
    0x19, 0x80,
    0x1a, 0x77,
    0x1b, 0x42,
    0x1c, 0xdd,
    0x1d, 0x02,
    0x1e, 0x00,
    0x1f, 0x00,
    0x20, 0x00,
    0x21, 0x00,
    0x22, 0xe0,
    0x23, 0xe0,
    0x24, 0x6e,
    0x25, 0x07,
    0x26, 0x80,
    0x27, 0x00,
    0x28, 0x10,
    0x29, 0x00,
    0x2a, 0x80,
    0x2b, 0x40,
    0x2c, 0x40,
    0x2d, 0x00,
    0x2e, 0x00,
    0x2f, 0x00,
    0x30, 0xbc,
    0x31, 0xdf,
    0x32, 0x02,
    0x33, 0x00,
    0x34, 0xcd,
    0x35, 0xcc,
    0x36, 0x3a,
    0x37, 0x00,
    0x38, 0x03,
    0x39, 0x20,
    0x3a, 0x00,
    0x3b, 0x3f,
    0x3c, 0xd1,
    0x3d, 0x31,
    0x3e, 0x03,
    0x3f, 0x00,
    0x40, 0xc0,
    0x41, 0x00,
    0x42, 0x00,
    0x43, 0x00,
    0x44, 0x11,
    0x45, 0x11,
    0x46, 0x11,
    0x47, 0x11,
    0x48, 0x11,
    0x49, 0x11,
    0x4a, 0x11,
    0x4b, 0x11,
    0x4c, 0x11,
    0x4d, 0x11,
    0x4e, 0x11,
    0x4f, 0x00,
    0x50, 0x11,
    0x51, 0x11,
    0x52, 0x11,
    0x53, 0x11,
    0x54, 0x11,
    0x55, 0x11,
    0x56, 0xff,
    0x57, 0xff,
    0x58, 0x40,
    0x59, 0x51,
    0x5a, 0x03,
    0x5b, 0x03,
    0x5c, 0x00,
    0x5d, 0x44,
    0x5e, 0x15,
    0x5f, 0x00,
    0x60, 0x00,
    0x61, 0x00,
    0x62, 0x00,
    0x63, 0x00,
    0x64, 0x00,
    0x65, 0x00,
    0x66, 0x00,
    0x67, 0x00,
    0x68, 0x00,
    0x69, 0x00,
    0x6a, 0x00,
    0x6b, 0x00,
    0x6c, 0x00,
    0x6d, 0x00,
    0x6e, 0x00,
    0x6f, 0x00,
    0x70, 0x00,
    0x71, 0x00,
    0x72, 0x00,
    0x73, 0x00,
    0x74, 0x00,
    0x75, 0x00,
    0x76, 0x00,
    0x77, 0x00,
    0x78, 0x00,
    0x79, 0x00,
    0x7a, 0x00,
    0x7b, 0x00,
    0x7c, 0x00,
    0x7d, 0x00,
    0x7e, 0x00,
    0x7f, 0x00,
    0x80, 0x10,
    0x81, 0x00,
    0x82, 0x00,
    0x83, 0x11,
    0x84, 0x00,
    0x85, 0x00,
    0x86, 0x40,
    0x87, 0x31,
    0x88, 0x30,
    0x89, 0x00,
    0x8a, 0x00,
    0x8b, 0x00,
    0x8c, 0x00,
    0x8d, 0x00,
    0x8e, 0x00,
    0x8f, 0x00,
    0x90, 0x00,
    0x91, 0x08,
    0x92, 0x00,
    0x93, 0xc0,
    0x94, 0x00,
    0x95, 0x00,
    0x96, 0xd0,
    0x97, 0x02,
    0x98, 0x01,
    0x99, 0x00,
    0x9a, 0x00,
    0x9b, 0x80,
    0x9c, 0xd0,
    0x9d, 0x02,
    0x9e, 0x39,
    0x9f, 0x01,
    0xa0, 0x01,
    0xa1, 0x00,
    0xa2, 0x00,
    0xa3, 0x00,
    0xa4, 0x60,
    0xa5, 0x35,
    0xa6, 0x70,
    0xa7, 0x00,
    0xa8, 0x00,
    0xa9, 0x04,
    0xaa, 0x00,
    0xab, 0x00,
    0xac, 0x00,
    0xad, 0x02,
    0xae, 0x00,
    0xaf, 0x00,
    0xb0, 0x00,
    0xb1, 0x04,
    0xb2, 0x00,
    0xb3, 0x00,
    0xb4, 0x40,
    0xb5, 0x15,
    0xb6, 0x30,
    0xb7, 0xe0,
    0xb8, 0x00,
    0xb9, 0x00,
    0xba, 0x00,
    0xbb, 0x00,
    0xbc, 0x00,
    0xbd, 0x00,
    0xbe, 0x00,
    0xbf, 0x00,
    0xc0, 0x03,
    0xc1, 0x08,
    0xc2, 0x00,
    0xc3, 0x80,
    0xc4, 0x03,
    0xc5, 0x00,
    0xc6, 0xd0,
    0xc7, 0x02,
    0xc8, 0x18,
    0xc9, 0x00,
    0xca, 0x18,
    0xcb, 0x01,
    0xcc, 0xce,
    0xcd, 0x02,
    0xce, 0x18,
    0xcf, 0x01,
    0xd0, 0x01,
    0xd1, 0x00,
    0xd2, 0x00,
    0xd3, 0x00,
    0xd4, 0x80,
    0xd5, 0x40,
    0xd6, 0x40,
    0xd7, 0x00,
    0xd8, 0x00,
    0xd9, 0x04,
    0xda, 0x00,
    0xdb, 0x00,
    0xdc, 0x00,
    0xdd, 0x02,
    0xde, 0x00,
    0xdf, 0x00,
    0xe0, 0x00,
    0xe1, 0x04,
    0xe2, 0x00,
    0xe3, 0x00,
    0xe4, 0x40,
    0xe5, 0x17,
    0xe6, 0x00,
    0xe7, 0x00,
    0xe8, 0x00,
    0xe9, 0x00,
    0xea, 0x00,
    0xeb, 0x00,
    0xec, 0x00,
    0xed, 0x00,
    0xee, 0x00,
    0xef, 0x00,
    0xf0, 0x00,
    0xf1, 0x00,
    0xf2, 0x00,
    0xf3, 0x00,
    0xf4, 0x00,
    0xf5, 0x00,
    0xf6, 0x00,
    0xf7, 0x00,
    0xf8, 0x00,
    0xf9, 0x00,
    0xfa, 0x00,
    0xfb, 0x00,
    0xfc, 0x00,
    0xfd, 0x00,
    0xfe, 0x00,
    0xff, 0x00
};

static int saa7117_command ( struct inode  *inode, struct file   *file,
                              unsigned int   cmd,   unsigned long  arg)
{
    struct saa7117 *decoder;

    decoder = i2c_get_clientdata(saa7117_client);

    switch (cmd) {

    case 0:
        //dprintk(1, KERN_INFO "%s: writing init\n", I2C_NAME(saa7117_client));
        //saa7117_write_block(saa7117_client, init, sizeof(init));
        break;

    case DECODER_DUMP:
    {
        int i;

        dprintk(1, KERN_INFO "%s: decoder dump\n", I2C_NAME(saa7117_client));

        for (i = 0; i < 32; i += 16) {
            int j;

            printk(KERN_DEBUG "%s: %03x", I2C_NAME(saa7117_client), i);
            for (j = 0; j < 16; ++j) {
                printk(" %02x",
                       saa7117_read(saa7117_client, i + j));
            }
            printk("\n");
        }
    }
        break;

    case DECODER_GET_CAPABILITIES:
    {
        struct video_decoder_capability *cap = (struct video_decoder_capability *)arg;

        dprintk(1, KERN_DEBUG "%s: decoder get capabilities\n",
            I2C_NAME(saa7117_client));

        cap->flags = VIDEO_DECODER_PAL |
                 VIDEO_DECODER_NTSC |
                 VIDEO_DECODER_AUTO |
                 VIDEO_DECODER_CCIR;
        cap->inputs = 8;
        cap->outputs = 1;
    }
        break;

    case DECODER_GET_STATUS:
    {
        int *iarg = (int *)arg;
        int status1;
        int status2;
        int res;

        status1 = saa7117_read(saa7117_client, 0x1e);
        status2 = saa7117_read(saa7117_client, 0x1f);

        dprintk(1, KERN_DEBUG "%s status: 0x%02x/0x%02x\n", I2C_NAME(saa7117_client),
            status1, status2);
        res = 0;
        if ((status2 & (1 << 6)) == 0)  {
            res |= DECODER_STATUS_GOOD;
        }
        switch (decoder->norm) {
        case VIDEO_MODE_NTSC:
            res |= DECODER_STATUS_NTSC;
            break;
        case VIDEO_MODE_PAL:
            res |= DECODER_STATUS_PAL;
            break;
        case VIDEO_MODE_SECAM:
            res |= DECODER_STATUS_SECAM;
            break;
        default:
        case VIDEO_MODE_AUTO:
            switch (status1 & 0x03) {
            case (0) :
                res |= DECODER_STATUS_PAL;
                break;
            case (1) :
                res |= DECODER_STATUS_NTSC | DECODER_STATUS_COLOR;
                break;
            case (2) :
                /*  for PAL-M, add test for 60Hz sync and treat as NTSC */
                if ((status2 & FIDT)==0)
                {
                    res |= DECODER_STATUS_PAL | DECODER_STATUS_COLOR; /* 50Hz */
                }
                else
                {
                    res |= DECODER_STATUS_NTSC | DECODER_STATUS_COLOR; /* 60Hz, ie PAL-M, treat like NTSC */
                }
                break;
            case (3) :
                res |= DECODER_STATUS_SECAM | DECODER_STATUS_COLOR;
                break;
            }
            break;
        }
        *iarg = res;
    }
        break;

    case DECODER_SET_NORM:
    {
        int *iarg = (int *)arg;

        short int hoff = 0, voff = 0, w = 0, h = 0;

        dprintk(1, KERN_DEBUG "%s: decoder set norm ",
            I2C_NAME(saa7117_client));
        switch (*iarg) {

        case VIDEO_MODE_NTSC:
            dprintk(1, "NTSC\n");
            decoder->reg[REG_ADDR(0x06)] = SAA_7117_NTSC_HSYNC_START;
            decoder->reg[REG_ADDR(0x07)] = SAA_7117_NTSC_HSYNC_STOP;

            decoder->reg[REG_ADDR(0x08)] = decoder->playback ? 0x7c : 0xb8; // PLL free when playback, PLL close when capture

            decoder->reg[REG_ADDR(0x0e)] = 0x85;
            decoder->reg[REG_ADDR(0x0f)] = 0x24;

            hoff = SAA_7117_NTSC_HOFFSET;
            voff = SAA_7117_NTSC_VOFFSET;
            w = SAA_7117_NTSC_WIDTH;
            h = SAA_7117_NTSC_HEIGHT;

            break;

        case VIDEO_MODE_PAL:
            dprintk(1, "PAL\n");
            decoder->reg[REG_ADDR(0x06)] = SAA_7117_PAL_HSYNC_START;
            decoder->reg[REG_ADDR(0x07)] = SAA_7117_PAL_HSYNC_STOP;

            decoder->reg[REG_ADDR(0x08)] = decoder->playback ? 0x7c : 0xb8; // PLL free when playback, PLL close when capture

            decoder->reg[REG_ADDR(0x0e)] = 0x81;
            decoder->reg[REG_ADDR(0x0f)] = 0x24;

            hoff = SAA_7117_PAL_HOFFSET;
            voff = SAA_7117_PAL_VOFFSET;
            w = SAA_7117_PAL_WIDTH;
            h = SAA_7117_PAL_HEIGHT;

            break;

        case VIDEO_MODE_SECAM:
            dprintk(1, "SECAM\n");
            decoder->reg[REG_ADDR(0x06)] = SAA_7117_SECAM_HSYNC_START;
            decoder->reg[REG_ADDR(0x07)] = SAA_7117_SECAM_HSYNC_STOP;

            decoder->reg[REG_ADDR(0x08)] = decoder->playback ? 0x7c : 0xb8; // PLL free when playback, PLL close when capture

            decoder->reg[REG_ADDR(0x0e)] = 0xD1;
            decoder->reg[REG_ADDR(0x0f)] = 0x24;

            hoff = SAA_7117_SECAM_HOFFSET;
            voff = SAA_7117_SECAM_VOFFSET;
            w = SAA_7117_SECAM_WIDTH;
            h = SAA_7117_SECAM_HEIGHT;

            break;

        case VIDEO_MODE_AUTO:
            dprintk(1, "Auto\n");
            decoder->reg[REG_ADDR(0x06)] = SAA_7117_PAL_HSYNC_START;
            decoder->reg[REG_ADDR(0x07)] = SAA_7117_PAL_HSYNC_STOP;

            decoder->reg[REG_ADDR(0x08)] = decoder->playback ? 0x7c : 0xb8; // PLL free when playback, PLL close when capture

            decoder->reg[REG_ADDR(0x0e)] = 0x83;
            decoder->reg[REG_ADDR(0x0f)] = 0x24;

            hoff = SAA_7117_PAL_HOFFSET;
            voff = SAA_7117_PAL_VOFFSET;
            w = SAA_7117_PAL_WIDTH;
            h = SAA_7117_PAL_HEIGHT;

            break;

        default:
            dprintk(1, " Unknown video mode!!!\n");
            return -EINVAL;

        }


        decoder->reg[REG_ADDR(0x94)] = LOBYTE(hoff);    // hoffset low
        decoder->reg[REG_ADDR(0x95)] = HIBYTE(hoff) & 0x0f; // hoffset high
        decoder->reg[REG_ADDR(0x96)] = LOBYTE(w);   // width low
        decoder->reg[REG_ADDR(0x97)] = HIBYTE(w) & 0x0f;    // width high
        decoder->reg[REG_ADDR(0x98)] = LOBYTE(voff);    // voffset low
        decoder->reg[REG_ADDR(0x99)] = HIBYTE(voff) & 0x0f; // voffset high
        decoder->reg[REG_ADDR(0x9a)] = LOBYTE(h + 2);   // height low
        decoder->reg[REG_ADDR(0x9b)] = HIBYTE(h + 2) & 0x0f;    // height high
        decoder->reg[REG_ADDR(0x9c)] = LOBYTE(w);   // out width low
        decoder->reg[REG_ADDR(0x9d)] = HIBYTE(w) & 0x0f;    // out width high
        decoder->reg[REG_ADDR(0x9e)] = LOBYTE(h);   // out height low
        decoder->reg[REG_ADDR(0x9f)] = HIBYTE(h) & 0x0f;    // out height high

        decoder->reg[REG_ADDR(0xc4)] = LOBYTE(hoff);    // hoffset low
        decoder->reg[REG_ADDR(0xc5)] = HIBYTE(hoff) & 0x0f; // hoffset high
        decoder->reg[REG_ADDR(0xc6)] = LOBYTE(w);   // width low
        decoder->reg[REG_ADDR(0xc7)] = HIBYTE(w) & 0x0f;    // width high
        decoder->reg[REG_ADDR(0xc8)] = LOBYTE(voff);    // voffset low
        decoder->reg[REG_ADDR(0xc9)] = HIBYTE(voff) & 0x0f; // voffset high
        decoder->reg[REG_ADDR(0xca)] = LOBYTE(h + 2);   // height low
        decoder->reg[REG_ADDR(0xcb)] = HIBYTE(h + 2) & 0x0f;    // height high
        decoder->reg[REG_ADDR(0xcc)] = LOBYTE(w);   // out width low
        decoder->reg[REG_ADDR(0xcd)] = HIBYTE(w) & 0x0f;    // out width high
        decoder->reg[REG_ADDR(0xce)] = LOBYTE(h);   // out height low
        decoder->reg[REG_ADDR(0xcf)] = HIBYTE(h) & 0x0f;    // out height high


        saa7117_write(saa7117_client, 0x80, 0x00);  // i-port and scaler back end clock selection, task A&B off
        saa7117_write(saa7117_client, 0x3C, 0x91);
        saa7117_write(saa7117_client, 0x3C, 0xD1);
        saa7117_write(saa7117_client, 0x88, 0x10);  // sw reset scaler
        saa7117_write(saa7117_client, 0x88, 0x30);  // sw reset scaler release

        saa7117_write_block(saa7117_client, decoder->reg + (0x06 << 1), 3 << 1);
        saa7117_write_block(saa7117_client, decoder->reg + (0x0e << 1), 2 << 1);
        saa7117_write_block(saa7117_client, decoder->reg + (0x5a << 1), 2 << 1);

        saa7117_write_block(saa7117_client, decoder->reg + (0x94 << 1), (0x9f + 1 - 0x94) << 1);
        saa7117_write_block(saa7117_client, decoder->reg + (0xc4 << 1), (0xcf + 1 - 0xc4) << 1);

        saa7117_write(saa7117_client, 0x3C, 0x91);
        saa7117_write(saa7117_client, 0x3C, 0xD1);
        saa7117_write(saa7117_client, 0x88, 0x10);  // sw reset scaler
        saa7117_write(saa7117_client, 0x88, 0x30);  // sw reset scaler release
        saa7117_write(saa7117_client, 0x80, 0xB0);  // i-port and scaler back end clock selection

        decoder->norm = *iarg;
    }
        break;

    case DECODER_SET_INPUT:
    {
        int *iarg = (int *)arg;

        dprintk(1, KERN_DEBUG "%s: decoder set input (%d)\n",
            I2C_NAME(saa7117_client), *iarg);
        if (*iarg < 0 || *iarg > 8) {
            return -EINVAL;
        }

        if (decoder->input != *iarg) {
            dprintk(1, KERN_DEBUG "%s: now setting %s input\n",
                I2C_NAME(saa7117_client),
                *iarg == 0 ? "Composite" :
                *iarg == 1 ? "Composite 2" :
                *iarg == 6 ? "S-Video" :
                *iarg == 7 ? "YPrPb" :
                "RGB");
            decoder->input = *iarg;

            /* select mode */
            decoder->reg[REG_ADDR(0x02)] =
                (decoder->
                 reg[REG_ADDR(0x02)] & 0xC0) |
                 (decoder->input == 0 ? 0x00 :
                  decoder->input == 1 ? 0x01 :
                  decoder->input == 6 ? 0x0a :
                  decoder->input == 7 ? 0x20 :
                0x30);
            saa7117_write(saa7117_client, 0x02, decoder->reg[REG_ADDR(0x02)]);

            /* bypass chrominance trap for modes 6..9 */
            decoder->reg[REG_ADDR(0x09)] =
                (decoder->
                 reg[REG_ADDR(0x09)] & 0x7f) |
                 (decoder->input == 6 ? 0x80 : 0x00);
            saa7117_write(saa7117_client, 0x09, decoder->reg[REG_ADDR(0x09)]);

            decoder->reg[REG_ADDR(0x0e)] =
                decoder->input == 6 ?
                  decoder->reg[REG_ADDR(0x0e)] & ~1 :
                  decoder->reg[REG_ADDR(0x0e)] | 1;
            saa7117_write(saa7117_client, 0x0e, decoder->reg[REG_ADDR(0x0e)]);

            decoder->reg[REG_ADDR(0x27)] = decoder->input > 7 ? 0x02 : 0x00;
            saa7117_write(saa7117_client, 0x27, decoder->reg[REG_ADDR(0x27)]);
        }
    }
        break;

    case DECODER_SET_OUTPUT:
    {
        int *iarg = (int *)arg;

        dprintk(1, KERN_DEBUG "%s: decoder set output\n",
            I2C_NAME(saa7117_client));

        /* not much choice of outputs */
        if (*iarg != 0) {
            return -EINVAL;
        }
    }
        break;

    case DECODER_ENABLE_OUTPUT:
    {
        int *iarg = (int *)arg;
        int enable = (*iarg != 0);

        dprintk(1, KERN_DEBUG "%s: decoder %s output\n",
            I2C_NAME(saa7117_client), enable ? "enable" : "disable");

        decoder->playback = !enable;

        if (decoder->enable != enable) {
            decoder->enable = enable;

            /* RJ: If output should be disabled (for
             * playing videos), we also need a open PLL.
             * The input is set to 0 (where no input
             * source is connected), although this
             * is not necessary.
             *
             * If output should be enabled, we have to
             * reverse the above.
             */

            if (decoder->enable) {
                decoder->reg[REG_ADDR(0x08)] = 0xb8;
                decoder->reg[REG_ADDR(0x12)] = 0xc9;
                decoder->reg[REG_ADDR(0x13)] = 0x80;
                decoder->reg[REG_ADDR(0x87)] = 0x11;
            } else {
                decoder->reg[REG_ADDR(0x08)] = 0x7c;
                decoder->reg[REG_ADDR(0x12)] = 0x00;
                decoder->reg[REG_ADDR(0x13)] = 0x00;
                decoder->reg[REG_ADDR(0x87)] = 0x00;
            }

            saa7117_write_block(saa7117_client, decoder->reg + (0x12 << 1), 2 << 1);
            saa7117_write(saa7117_client, 0x08, decoder->reg[REG_ADDR(0x08)]);
            saa7117_write(saa7117_client, 0x87, decoder->reg[REG_ADDR(0x87)]);
            saa7117_write(saa7117_client, 0x3C, 0x91);
            saa7117_write(saa7117_client, 0x3C, 0xD1);
            saa7117_write(saa7117_client, 0x88, 0x10);  // sw reset scaler
            saa7117_write(saa7117_client, 0x88, 0x30);  // sw reset scaler release
            saa7117_write(saa7117_client, 0x80, 0xB0);

        }
    }
        break;

    case DECODER_SET_PICTURE:
    {
        struct video_picture *pic = (struct video_picture *)arg;

        dprintk(1,
            KERN_DEBUG
            "%s: decoder set picture bright=%d contrast=%d saturation=%d hue=%d\n",
            I2C_NAME(saa7117_client), pic->brightness, pic->contrast,
            pic->colour, pic->hue);

        if (decoder->bright != pic->brightness) {
            /* We want 0 to 255 we get 0-65535 */
            decoder->bright = pic->brightness;
            saa7117_write(saa7117_client, 0x0a, decoder->bright >> 8);
        }
        if (decoder->contrast != pic->contrast) {
            /* We want 0 to 127 we get 0-65535 */
            decoder->contrast = pic->contrast;
            saa7117_write(saa7117_client, 0x0b, decoder->contrast >> 9);
        }
        if (decoder->sat != pic->colour) {
            /* We want 0 to 127 we get 0-65535 */
            decoder->sat = pic->colour;
            saa7117_write(saa7117_client, 0x0c, decoder->sat >> 9);
        }
        if (decoder->hue != pic->hue) {
            /* We want -128 to 127 we get 0-65535 */
            decoder->hue = pic->hue;
            saa7117_write(saa7117_client, 0x0d, (decoder->hue - 32768) >> 8);
        }
    }
        break;

    default:
        return -EINVAL;
    }

    return 0;
}

/* ----------------------------------------------------------------------- */

/*
 * Generic i2c probe
 * concerning the addresses: i2c wants 7 bit (without the r/w bit), so '>>1'
 */
static unsigned short normal_i2c[] = { I2C_SAA7117 >> 1, I2C_SAA7117A >> 1, I2C_CLIENT_END };
static unsigned short ignore = I2C_CLIENT_END;

static struct i2c_client_address_data addr_data = {
    .normal_i2c     = normal_i2c,
    .probe          = &ignore,
    .ignore         = &ignore,
};

static struct i2c_driver i2c_driver_saa7117;

static int
saa7117_detect_client (struct i2c_adapter *adapter,
               int                 address,
               int                 kind)
{
    int i, err[30];
    short int hoff = SAA_7117_PAL_HOFFSET;
    short int voff = SAA_7117_PAL_VOFFSET;
    short int w = SAA_7117_PAL_WIDTH;
    short int h = SAA_7117_PAL_HEIGHT;
    struct saa7117 *decoder;

    dprintk(1,
        KERN_INFO
        "saa7117.c: detecting saa7117 client on address 0x%x\n",
        address << 1);

    /* Check if the adapter supports the needed features */
    if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
        return 0;

    saa7117_client = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
    if (saa7117_client == 0)
        return -ENOMEM;
    saa7117_client->addr = address;
    saa7117_client->adapter = adapter;
    saa7117_client->driver = &i2c_driver_saa7117;
    strlcpy(I2C_NAME(saa7117_client), "saa7110", sizeof(I2C_NAME(saa7117_client)));

    decoder = kzalloc(sizeof(struct saa7117), GFP_KERNEL);
    if (decoder == NULL) {
        kfree(saa7117_client);
        return -ENOMEM;
    }
    decoder->norm = VIDEO_MODE_PAL;
    decoder->input = -1;
    decoder->enable = 1;
    decoder->bright = 32768;
    decoder->contrast = 32768;
    decoder->hue = 32768;
    decoder->sat = 32768;
    decoder->playback = 0;  // initially capture mode useda
    i2c_set_clientdata(saa7117_client, decoder);

    memcpy(decoder->reg, init, sizeof(init));

    decoder->reg[REG_ADDR(0x94)] = LOBYTE(hoff);    // hoffset low
    decoder->reg[REG_ADDR(0x95)] = HIBYTE(hoff) & 0x0f; // hoffset high
    decoder->reg[REG_ADDR(0x96)] = LOBYTE(w);   // width low
    decoder->reg[REG_ADDR(0x97)] = HIBYTE(w) & 0x0f;    // width high
    decoder->reg[REG_ADDR(0x98)] = LOBYTE(voff);    // voffset low
    decoder->reg[REG_ADDR(0x99)] = HIBYTE(voff) & 0x0f; // voffset high
    decoder->reg[REG_ADDR(0x9a)] = LOBYTE(h + 2);   // height low
    decoder->reg[REG_ADDR(0x9b)] = HIBYTE(h + 2) & 0x0f;    // height high
    decoder->reg[REG_ADDR(0x9c)] = LOBYTE(w);   // out width low
    decoder->reg[REG_ADDR(0x9d)] = HIBYTE(w) & 0x0f;    // out width high
    decoder->reg[REG_ADDR(0x9e)] = LOBYTE(h);   // out height low
    decoder->reg[REG_ADDR(0x9f)] = HIBYTE(h) & 0x0f;    // out height high

    decoder->reg[REG_ADDR(0xc4)] = LOBYTE(hoff);    // hoffset low
    decoder->reg[REG_ADDR(0xc5)] = HIBYTE(hoff) & 0x0f; // hoffset high
    decoder->reg[REG_ADDR(0xc6)] = LOBYTE(w);   // width low
    decoder->reg[REG_ADDR(0xc7)] = HIBYTE(w) & 0x0f;    // width high
    decoder->reg[REG_ADDR(0xc8)] = LOBYTE(voff);    // voffset low
    decoder->reg[REG_ADDR(0xc9)] = HIBYTE(voff) & 0x0f; // voffset high
    decoder->reg[REG_ADDR(0xca)] = LOBYTE(h + 2);   // height low
    decoder->reg[REG_ADDR(0xcb)] = HIBYTE(h + 2) & 0x0f;    // height high
    decoder->reg[REG_ADDR(0xcc)] = LOBYTE(w);   // out width low
    decoder->reg[REG_ADDR(0xcd)] = HIBYTE(w) & 0x0f;    // out width high
    decoder->reg[REG_ADDR(0xce)] = LOBYTE(h);   // out height low
    decoder->reg[REG_ADDR(0xcf)] = HIBYTE(h) & 0x0f;    // out height high

    decoder->reg[REG_ADDR(0xb8)] = LOBYTE(LOWORD(SAA_7117_VERTICAL_CHROMA_OFFSET));
    decoder->reg[REG_ADDR(0xb9)] = HIBYTE(LOWORD(SAA_7117_VERTICAL_CHROMA_OFFSET));
    decoder->reg[REG_ADDR(0xba)] = LOBYTE(HIWORD(SAA_7117_VERTICAL_CHROMA_OFFSET));
    decoder->reg[REG_ADDR(0xbb)] = HIBYTE(HIWORD(SAA_7117_VERTICAL_CHROMA_OFFSET));

    decoder->reg[REG_ADDR(0xbc)] = LOBYTE(LOWORD(SAA_7117_VERTICAL_LUMA_OFFSET));
    decoder->reg[REG_ADDR(0xbd)] = HIBYTE(LOWORD(SAA_7117_VERTICAL_LUMA_OFFSET));
    decoder->reg[REG_ADDR(0xbe)] = LOBYTE(HIWORD(SAA_7117_VERTICAL_LUMA_OFFSET));
    decoder->reg[REG_ADDR(0xbf)] = HIBYTE(HIWORD(SAA_7117_VERTICAL_LUMA_OFFSET));

    decoder->reg[REG_ADDR(0xe8)] = LOBYTE(LOWORD(SAA_7117_VERTICAL_CHROMA_OFFSET));
    decoder->reg[REG_ADDR(0xe9)] = HIBYTE(LOWORD(SAA_7117_VERTICAL_CHROMA_OFFSET));
    decoder->reg[REG_ADDR(0xea)] = LOBYTE(HIWORD(SAA_7117_VERTICAL_CHROMA_OFFSET));
    decoder->reg[REG_ADDR(0xeb)] = HIBYTE(HIWORD(SAA_7117_VERTICAL_CHROMA_OFFSET));

    decoder->reg[REG_ADDR(0xec)] = LOBYTE(LOWORD(SAA_7117_VERTICAL_LUMA_OFFSET));
    decoder->reg[REG_ADDR(0xed)] = HIBYTE(LOWORD(SAA_7117_VERTICAL_LUMA_OFFSET));
    decoder->reg[REG_ADDR(0xee)] = LOBYTE(HIWORD(SAA_7117_VERTICAL_LUMA_OFFSET));
    decoder->reg[REG_ADDR(0xef)] = HIBYTE(HIWORD(SAA_7117_VERTICAL_LUMA_OFFSET));


    decoder->reg[REG_ADDR(0x13)] = 0x00;    // RTC0 on
    decoder->reg[REG_ADDR(0x87)] = 0x11;    // I-Port
    decoder->reg[REG_ADDR(0x12)] = 0x00;    // RTS0

    decoder->reg[REG_ADDR(0x09)] = 0x40;    // chrominance trap
    decoder->reg[REG_ADDR(0x0e)] |= 1;  // combfilter on

    dprintk(1, KERN_DEBUG "%s_attach: starting decoder init\n",
        I2C_NAME(saa7117_client));

    err[0] = saa7117_write_block(saa7117_client, decoder->reg + (0x20 << 1), 0x10 << 1);
    err[1] = saa7117_write_block(saa7117_client, decoder->reg + (0x30 << 1), 0x10 << 1);
    err[2] = saa7117_write_block(saa7117_client, decoder->reg + (0x63 << 1), (0x7f + 1 - 0x63) << 1);
    err[3] = saa7117_write_block(saa7117_client, decoder->reg + (0x89 << 1), 6 << 1);
    err[4] = saa7117_write_block(saa7117_client, decoder->reg + (0xb8 << 1), 8 << 1);
    err[5] = saa7117_write_block(saa7117_client, decoder->reg + (0xe8 << 1), 8 << 1);

    for (i = 0; i <= 5; i++) {
        if (err[i] < 0) {
            dprintk(1,
                KERN_ERR
                "%s_attach: init error %d at stage %d, leaving attach.\n",
                I2C_NAME(saa7117_client), i, err[i]);
            kfree(decoder);
            kfree(saa7117_client);
            return 0;
        }
    }

    for (i = 6; i < 8; i++) {
        dprintk(1,
            KERN_DEBUG
            "%s_attach: reg[0x%02x] = 0x%02x (0x%02x)\n",
            I2C_NAME(saa7117_client), i, saa7117_read(saa7117_client, i),
            decoder->reg[REG_ADDR(i)]);
    }

    dprintk(1,
        KERN_DEBUG
        "%s_attach: performing decoder reset sequence\n",
        I2C_NAME(saa7117_client));

    err[6] = saa7117_write(saa7117_client, 0x80, 0x00); // i-port and scaler backend clock selection, task A&B off
    err[6] = saa7117_write(saa7117_client, 0x3C, 0x91);
    err[6] = saa7117_write(saa7117_client, 0x3C, 0xD1);
    err[7] = saa7117_write(saa7117_client, 0x88, 0x10); // sw reset scaler
    err[8] = saa7117_write(saa7117_client, 0x88, 0x30); // sw reset scaler release

    for (i = 6; i <= 8; i++) {
        if (err[i] < 0) {
            dprintk(1,
                KERN_ERR
                "%s_attach: init error %d at stage %d, leaving attach.\n",
                I2C_NAME(saa7117_client), i, err[i]);
            kfree(decoder);
            kfree(saa7117_client);
            return 0;
        }
    }

    dprintk(1, KERN_INFO "%s_attach: performing the rest of init\n",
        I2C_NAME(saa7117_client));

    err[9] = saa7117_write(saa7117_client, 0x01, decoder->reg[REG_ADDR(0x01)]);
    err[10] = saa7117_write_block(saa7117_client, decoder->reg + (0x03 << 1), (0x1e + 1 - 0x03) << 1);  // big seq
    err[11] = saa7117_write_block(saa7117_client, decoder->reg + (0x40 << 1), (0x5f + 1 - 0x40) << 1);  // slicer
    err[12] = saa7117_write_block(saa7117_client, decoder->reg + (0x81 << 1), 2 << 1);  // ?
    err[13] = saa7117_write_block(saa7117_client, decoder->reg + (0x83 << 1), 5 << 1);  // ?
    err[14] = saa7117_write_block(saa7117_client, decoder->reg + (0x90 << 1), 4 << 1);  // Task A
    err[15] = saa7117_write_block(saa7117_client, decoder->reg + (0x94 << 1), 12 << 1);
    err[16] = saa7117_write_block(saa7117_client, decoder->reg + (0xa0 << 1), 8 << 1);
    err[17] = saa7117_write_block(saa7117_client, decoder->reg + (0xa8 << 1), 8 << 1);
    err[18] = saa7117_write_block(saa7117_client, decoder->reg + (0xb0 << 1), 8 << 1);
    err[19] = saa7117_write_block(saa7117_client, decoder->reg + (0xc0 << 1), 4 << 1);  // Task B
    err[15] = saa7117_write_block(saa7117_client, decoder->reg + (0xc4 << 1), 12 << 1);
    err[16] = saa7117_write_block(saa7117_client, decoder->reg + (0xd0 << 1), 8 << 1);
    err[17] = saa7117_write_block(saa7117_client, decoder->reg + (0xd8 << 1), 8 << 1);
    err[18] = saa7117_write_block(saa7117_client, decoder->reg + (0xe0 << 1), 8 << 1);

    for (i = 9; i <= 18; i++) {
        if (err[i] < 0) {
            dprintk(1,
                KERN_ERR
                "%s_attach: init error %d at stage %d, leaving attach.\n",
                I2C_NAME(saa7117_client), i, err[i]);
            kfree(decoder);
            kfree(saa7117_client);
            return 0;
        }
    }


    for (i = 6; i < 8; i++) {
        dprintk(1,
            KERN_DEBUG
            "%s_attach: reg[0x%02x] = 0x%02x (0x%02x)\n",
            I2C_NAME(saa7117_client), i, saa7117_read(saa7117_client, i),
            decoder->reg[REG_ADDR(i)]);
    }


    for (i = 0x11; i <= 0x13; i++) {
        dprintk(1,
            KERN_DEBUG
            "%s_attach: reg[0x%02x] = 0x%02x (0x%02x)\n",
            I2C_NAME(saa7117_client), i, saa7117_read(saa7117_client, i),
            decoder->reg[REG_ADDR(i)]);
    }


    dprintk(1, KERN_DEBUG "%s_attach: setting video input\n",
        I2C_NAME(saa7117_client));

    err[19] = saa7117_write(saa7117_client, 0x02, decoder->reg[REG_ADDR(0x02)]);
    err[20] = saa7117_write(saa7117_client, 0x09, decoder->reg[REG_ADDR(0x09)]);
    err[21] = saa7117_write(saa7117_client, 0x0e, decoder->reg[REG_ADDR(0x0e)]);

    for (i = 19; i <= 21; i++) {
        if (err[i] < 0) {
            dprintk(1,
                KERN_ERR
                "%s_attach: init error %d at stage %d, leaving attach.\n",
                I2C_NAME(saa7117_client), i, err[i]);
            kfree(decoder);
            kfree(saa7117_client);
            return 0;
        }
    }

    dprintk(1,
        KERN_DEBUG
        "%s_attach: performing decoder reset sequence\n",
        I2C_NAME(saa7117_client));

    err[6] = saa7117_write(saa7117_client, 0x3C, 0x91);
    err[6] = saa7117_write(saa7117_client, 0x3C, 0xD1);
    err[22] = saa7117_write(saa7117_client, 0x88, 0x10);    // sw reset scaler
    err[23] = saa7117_write(saa7117_client, 0x88, 0x30);    // sw reset scaler release
    err[24] = saa7117_write(saa7117_client, 0x80, 0xB0);    // i-port and scaler backend clock selection, task A&B off


    for (i = 22; i <= 24; i++) {
        if (err[i] < 0) {
            dprintk(1,
                KERN_ERR
                "%s_attach: init error %d at stage %d, leaving attach.\n",
                I2C_NAME(saa7117_client), i, err[i]);
            kfree(decoder);
            kfree(saa7117_client);
            return 0;
        }
    }

    err[25] = saa7117_write(saa7117_client, 0x06, init[REG_ADDR(0x06)]);
    err[26] = saa7117_write(saa7117_client, 0x07, init[REG_ADDR(0x07)]);
    err[27] = saa7117_write(saa7117_client, 0x10, init[REG_ADDR(0x10)]);

    dprintk(1,
        KERN_INFO
        "%s_attach: chip version %x, decoder status 0x%02x\n",
        I2C_NAME(saa7117_client), saa7117_read(saa7117_client, 0x00) >> 4,
        saa7117_read(saa7117_client, 0x1f));
    dprintk(1,
        KERN_DEBUG
        "%s_attach: power save control: 0x%02x, scaler status: 0x%02x\n",
        I2C_NAME(saa7117_client), saa7117_read(saa7117_client, 0x88),
        saa7117_read(saa7117_client, 0x8f));


    for (i = 0x94; i < 0x96; i++) {
        dprintk(1,
            KERN_DEBUG
            "%s_attach: reg[0x%02x] = 0x%02x (0x%02x)\n",
            I2C_NAME(saa7117_client), i, saa7117_read(saa7117_client, i),
            decoder->reg[REG_ADDR(i)]);
    }

    i = i2c_attach_client(saa7117_client);
    if (i) {
        kfree(saa7117_client);
        kfree(decoder);
        return i;
    }

    //i = saa7117_write_block(saa7117_client, init, sizeof(init));
    i = 0;
    if (i < 0) {
        dprintk(1, KERN_ERR "%s_attach error: init status %d\n",
            I2C_NAME(saa7117_client), i);
    } else {
        dprintk(1,
            KERN_INFO
            "%s_attach: chip version %x at address 0x%x\n",
            I2C_NAME(saa7117_client), saa7117_read(saa7117_client, 0x00) >> 4,
            saa7117_client->addr << 1);
    }

    /* Register a misc device called "saa7117". */
    err[28] = misc_register( &saa7117_MiscDev );
    if (err[28] < 0)
    {
        dprintk(1, KERN_INFO "can't register misc device (minor %d)!\n", saa7117_MiscDev.minor );
        return err[28];
    }

    return 0;
}

static int
saa7117_attach_adapter (struct i2c_adapter *adapter)
{
    dprintk(1,
        KERN_INFO
        "saa7117.c: starting probe for adapter %s (0x%x)\n",
        I2C_NAME(adapter), adapter->id);
    return i2c_probe(adapter, &addr_data, &saa7117_detect_client);
}

static int
saa7117_detach_client (struct i2c_client *client)
{
    struct saa7117 *decoder = i2c_get_clientdata(client);
    int err;

    err = i2c_detach_client(client);
    if (err) {
        return err;
    }

    kfree(decoder);
    kfree(client);

    return 0;
}

/* ----------------------------------------------------------------------- */

static struct i2c_driver i2c_driver_saa7117 = {
    .driver = {
		.name = "saa7117",
	},
    .id = I2C_DRIVERID_SAA711X,
    .attach_adapter = saa7117_attach_adapter,
    .detach_client = saa7117_detach_client,
};

static int __init
saa7117_init (void)
{
    return i2c_add_driver(&i2c_driver_saa7117);
}

static void __exit
saa7117_exit (void)
{
    if (misc_deregister(&saa7117_MiscDev)!=0)
    {
        dprintk(1, KERN_INFO "saa7117: could not misc_deregister the device\n");
    }
    i2c_del_driver(&i2c_driver_saa7117);
}

module_init(saa7117_init);
module_exit(saa7117_exit);
