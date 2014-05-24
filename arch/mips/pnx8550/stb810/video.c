/* This program is free software; you can redistribute it and/or modify
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
 * Copyright (C) 2006 Koninklijke Philips Electronics N.V.
 * All rights reserved
 *
 */
#include <linux/pci.h>

#include <glb.h>
#include <pci.h>
#include <int.h>
#include <framebuffer.h>
#include <linux/i2c.h>
#include <i2c.h>

#define I2C_ANABEL_ADDR                0x66
#define I2C_SCART_ADDR                 0x11

/* Register / Data pairs used to initialise Anabel into PAL mode */
static const unsigned char pnx8550fb_anabel_pal[] =
{
    0x27,0x00,
    0x28,0x21,
    0x29,0x1d,
    0x2b,0xb8,
    0x2c,0x00,
    0x54,0x00,
    0x5a,0x00,
    0x60,0x00,
    0x61,0x06,
    0x63,0xcb,
    0x64,0x8a,
    0x65,0x09,
    0x66,0x2a,
    0x67,0x00,
    0x68,0x00,
    0x69,0x00,
    0x6a,0x00,
    0x6c,0x02,
    0x6d,0x22,
    0x6e,0x60,
    0x6f,0x00,
    0x70,0x1a,
    0x71,0x9f,
    0x72,0x61,
    0x7a,0x16,
    0x7b,0x37,
    0x7c,0x40,
    0x7d,0x00,
    0x7f,0x00,
    0x7f,0x00,
    0xa0,0x00,
    0xa2,0x10,
    0xa3,0x80,
    0xa4,0x80,
    0xba,0x70,
    0x5b,0x89,
    0x5c,0xbe,
    0x5d,0x37,
    0x5e,0x39,
    0x5f,0x39,
    0x62,0x38,
    0xc2,0x1f,
    0xc3,0x0A,
    0xc4,0x0A,
    0xc5,0x0A,
    0xc6,0x00,
    0x2d,0x40,
    0x3a,0x48,
    0x95,0x00,
    0x15,0x30,
    0xa0,0x00,
    0x20,0x00,
    0x6e,0x00
};

/* Register / Data pairs used to initialise Anabel into NTSC mode */
static const unsigned char pnx8550fb_anabel_ntsc[] =
{
    0x27,0x00,
    0x28,0x25,
    0x29,0x1d,
    0x2c,0x00,
    0x54,0x00,
    0x5a,0x88,
    0x60,0x00,
    0x61,0x11,
    0x63,0x1f,
    0x64,0x7c,
    0x65,0xf0,
    0x66,0x21,
    0x6c,0xf2,
    0x6d,0x03,
    0x6e,0xD0,
    0x6f,0x00,
    0x70,0xfb,
    0x71,0x90,
    0x72,0x60,
    0x7a,0x00,
    0x7b,0x05,
    0x7c,0x40,
    0x7d,0x00,
    0xa0,0x00,
    0xa2,0x0d,
    0xa3,0x80,
    0xa4,0x80,
    0xba,0x70,
    0x5b,0x7d,
    0x5c,0xaf,
    0x5d,0x3e,
    0x5e,0x32,
    0x5f,0x32,
    0x62,0x4c,
    0xc2,0x1e,
    0xc3,0x0A,
    0xc4,0x0A,
    0xc5,0x0A,
    0xc6,0x01,
    0x2d,0x40,
    0x3a,0x48,
    0x95,0x00,
    0x15,0x30,
    0xa0,0x00,
    0x20,0x00,
    0x6e,0x00
};

/* Register / Data information used to initialise SCART switch */
static const unsigned char pnx8550fb_scart_data[] = {
    0x00,0x70,0x24,0x19,0x27,0x11,0x5F,0x04,0x3D,0x00,
};

static struct i2c_msg pnx8550fb_scart_msg = {
	.addr = I2C_SCART_ADDR,
	.flags = 0,
	.len = sizeof(pnx8550fb_scart_data),
	.buf = (unsigned char *) pnx8550fb_scart_data,
};

/* Function used to set up Scart switch */
static void pnx8550fb_setup_scart(void)
{
	struct i2c_adapter *adapter = i2c_get_adapter(PNX8550_I2C_IP0105_BUS1);
	if ((i2c_transfer(adapter, &pnx8550fb_scart_msg, 1)) != 1)
		printk(KERN_ERR "%s: write error\n", __func__);
}

static struct i2c_msg pnx8550fb_anabel_msg = {
	.addr = I2C_ANABEL_ADDR,
	.flags = 0,
	.len = 2,
};

/* Function used to set up PAL/NTSC using Anabel */
static void pnx8550fb_setup_anabel(int pal)
{
    int i, len;
    const unsigned char *data;
    struct i2c_adapter *adapter = i2c_get_adapter(PNX8550_I2C_IP3203_BUS1);

    if (pal) {
		data = pnx8550fb_anabel_pal;
		len = sizeof(pnx8550fb_anabel_pal);
	} else {
		data = pnx8550fb_anabel_ntsc;
		len = sizeof(pnx8550fb_anabel_ntsc);
	}

	for(i=0; i<len; i+=2)
	{
		pnx8550fb_anabel_msg.buf = (unsigned char *) &data[i];
		if ((i2c_transfer(adapter, &pnx8550fb_anabel_msg, 1)) != 1) {
			printk(KERN_ERR "%s: write error at %d\n", __func__, i);
			break;
		}
	}
}

/* Function used to set up PAL/NTSC using the QVCP */
static void pnx8550fb_setup_QVCP(unsigned int buffer, int pal)
{
    outl(0x03, PCI_BASE | 0x047a00);
    outl(0x0b, PCI_BASE | 0x047a04);
    outl(0x39, PCI_BASE | 0x047a18);

    if (pal)
    {
        outl(0x035f0137, PCI_BASE | 0x10e000);
        outl(0x02d0035f, PCI_BASE | 0x10e004);
        outl(0x01390016, PCI_BASE | 0x10e008);
        outl(0x02dd0350, PCI_BASE | 0x10e00c);
        outl(0x0001000b, PCI_BASE | 0x10e010);
        outl(0x00AF0137, PCI_BASE | 0x10e014);
        outl(0x012D02DD, PCI_BASE | 0x10e028);
        outl(0x012C02DC, PCI_BASE | 0x10e02c);
        outl(0x012002d0, PCI_BASE | 0x10e234);
    }
    else
    {
        outl(0x03590105, PCI_BASE | 0x10e000);
        outl(0x02d00359, PCI_BASE | 0x10e004);
        outl(0x01070012, PCI_BASE | 0x10e008);
        outl(0x02dd034a, PCI_BASE | 0x10e00c);
        outl(0x0004000b, PCI_BASE | 0x10e010);
        outl(0x00950105, PCI_BASE | 0x10e014);
        outl(0x013002DD, PCI_BASE | 0x10e028);
        outl(0x012F02DC, PCI_BASE | 0x10e02c);
        outl(0x00F002d0, PCI_BASE | 0x10e234);
    }
    outl(0x20050005, PCI_BASE | 0x10e020);
    outl(0x00000000, PCI_BASE | 0x10e034);
    outl(0x00000000, PCI_BASE | 0x10e038);
    outl(0x0fc01401, PCI_BASE | 0x10e03c);
    outl(0x0, PCI_BASE | 0x10e05c);
    outl(0x0, PCI_BASE | 0x10e060);
    outl(0x00130013, PCI_BASE | 0x10e070);
    outl(0x803F3F3F, PCI_BASE | 0x10e074);
    outl(buffer, PCI_BASE | 0x10e200);
    outl(PNX8550_FRAMEBUFFER_STRIDE*4, PCI_BASE | 0x10e204);
    outl(PNX8550_FRAMEBUFFER_STRIDE*2, PCI_BASE | 0x10e208);
    outl(buffer + (PNX8550_FRAMEBUFFER_STRIDE*2), PCI_BASE | 0x10e20c);
    outl(PNX8550_FRAMEBUFFER_STRIDE*4, PCI_BASE | 0x10e210);
    outl(8, PCI_BASE | 0x10e214);
    outl(0x80000000 | (16<<16)|(0x30), PCI_BASE | 0x10e230);
    outl(PNX8550_FRAMEBUFFER_WIDTH, PCI_BASE | 0x10e2b4);
    outl(0xec, PCI_BASE | 0x10e2bc);
    outl(0x20, PCI_BASE | 0x10e23c);
    outl(0x0, PCI_BASE | 0x10e238);
    outl(0x0, PCI_BASE | 0x10e25c);
    outl(0x0, PCI_BASE | 0x10e26c);
    outl(0x0, PCI_BASE | 0x10e27c);
    outl(0x0, PCI_BASE | 0x10e28c);
    outl(0xffe7eff7, PCI_BASE | 0x10e2c4);
    outl(0xe00, PCI_BASE | 0x10e2b8);
    outl(0x100f100, PCI_BASE | 0x10e2cc);
    outl(0x004d0096, PCI_BASE | 0x10e2d0);
    outl(0x001d07da, PCI_BASE | 0x10e2d4);
    outl(0x07b60070, PCI_BASE | 0x10e2d8);
    outl(0x009d077c, PCI_BASE | 0x10e2dc);
    outl(0x07e60100, PCI_BASE | 0x10e2e0);

    outl(0x1, PCI_BASE | 0x10e240);
}

/* Function used to initialise the splash screen */
void pnx8550fb_setup_display(int pal)
{
    /* Set up the QVCP registers */
    pnx8550fb_setup_QVCP(pnx8550_fb_base, pal);

    /* Set up Anabel using I2C */
    pnx8550fb_setup_anabel(pal);

    /* Set up the Scart switch (if present) */
    pnx8550fb_setup_scart();
}
