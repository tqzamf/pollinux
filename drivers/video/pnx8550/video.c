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
#include <linux/module.h>

#include <glb.h>
#include <pci.h>
#include <int.h>
#include <framebuffer.h>
#include <audio.h>
#include <linux/i2c.h>
#include <i2c.h>
#include <cm.h>

#define I2C_SCART_BUS                  PNX8550_I2C_IP0105_BUS1
#define I2C_SCART_ADDR                 0x11
#define I2C_ANABEL_BUS                 PNX8550_I2C_IP3203_BUS1
#define I2C_ANABEL_ADDR                0x66
#define I2C_ANABEL2_ADDR               0x6e
#define I2C_ANABEL_AUDIO1_ADDR         0x76
#define I2C_ANABEL_AUDIO2_ADDR         0x7e

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

/* commands to blank and unblank the Anabel */
static const unsigned char pnx8550fb_anabel_blank[] =
{
    0x6e,0x40
};
static const unsigned char pnx8550fb_anabel_unblank[] =
{
    0x6e,0x00
};

/* Register / Data information used to initialise SCART switch. */
static const unsigned char pnx8550fb_scart_data[] = {
    0x00, // address byte: start with first register
    0x70, // STBY, MUTE, DAPD=normal operation, manual startup, audio 24-bit I²S without de-emphasis
    0x74, // main audio = stereo DAC, VCR SCART audio = stereo DAC, main volume unmuted
    0x00, // main volume muted (until the audio driver loads and sets the default volume)
    0x27, // main volume transition = 2048ck, DAC volume = ±0dB, VCR SCART audio = stereo
    0x09, // TV SCART = encoder CVBS + RGB, VCR SCART = encoder CVBS, composite = encoder CVBS
    0x5F, // TV CVBS, R, G, B, FB = enabled, VCR CVBS = enabled, VCR C = disabled
    0x04, // RGB gain +6dB, DC restore = encoder CVBS, VCR clamp = Y/C, encoder clamp = RGB
    0x8D, // TV FB = +4V, TV SB = output +12V, VCR SB = input
    0x00, // (readonly status register)
    0x9E, // video detection masked, interrupts disabled (because nothing expects them)
};

/* Register / Data information used to place the SCART switch in auto-standby. These are simply the
 * power-up defaults according to the datasheet. */
static const unsigned char pnx8550fb_scart_standby[] = {
    0x00, // address byte: start with first register
    0x7b, // STBY, MUTE enabled, DAPD=normal operation, auto startup, audio 24-bit I²S without de-emphasis
    // the rest is unnecessary but let's restore defaults anyway
    0xd5,0x1f,0x27,0x9c,0x00,0x04,0x00,0x00,0x9E,
};

/* command to set SCART switch main volume. used by audio. */
static unsigned char pnx8550fb_scart_volume[] = {
    0x02, // address byte: main volume
    0x00, // main volume. overwritten by pnx8550fb_set_volume
};

static struct i2c_msg pnx8550fb_scart_msg = {
	.addr = I2C_SCART_ADDR,
	.flags = 0,
	.len = sizeof(pnx8550fb_scart_data),
	.buf = (unsigned char *) pnx8550fb_scart_data,
};

static struct i2c_msg pnx8550fb_shutdown_msg = {
	.addr = I2C_SCART_ADDR,
	.flags = 0,
	.len = sizeof(pnx8550fb_scart_standby),
	.buf = (unsigned char *) pnx8550fb_scart_standby,
};

static struct i2c_msg pnx8550fb_volume_msg = {
	.addr = I2C_SCART_ADDR,
	.flags = 0,
	.len = sizeof(pnx8550fb_scart_volume),
	.buf = (unsigned char *) pnx8550fb_scart_volume,
};

/* Function used to set up Scart switch */
static void pnx8550fb_setup_scart(void)
{
	struct i2c_adapter *adapter = i2c_get_adapter(I2C_SCART_BUS);
	if ((i2c_transfer(adapter, &pnx8550fb_scart_msg, 1)) != 1)
		printk(KERN_ERR "%s: write error\n", __func__);
}

/* Function used to switch the Scart switch back to power-up defaults */
static void pnx8550fb_shutdown_scart(void)
{
	struct i2c_adapter *adapter = i2c_get_adapter(I2C_SCART_BUS);
	if ((i2c_transfer(adapter, &pnx8550fb_shutdown_msg, 1)) != 1)
		printk(KERN_ERR "%s: write error\n", __func__);
}

/* Function to set the main volume in the Scart switch / DAC chip */
void pnx8550fb_set_volume(int volume)
{
	struct i2c_adapter *adapter;
	char vol;
	
	vol = volume;
	if (vol < 0)
		vol = 0;
	if (vol > AK4705_VOL_MAX)
		vol = AK4705_VOL_MAX;
	pnx8550fb_scart_volume[1] = vol;
	
	adapter = i2c_get_adapter(I2C_SCART_BUS);
	if ((i2c_transfer(adapter, &pnx8550fb_volume_msg, 1)) != 1)
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
    struct i2c_adapter *adapter = i2c_get_adapter(I2C_ANABEL_BUS);

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

/* Function used to set up PAL/NTSC using Anabel */
void pnx8550fb_set_blanking(int blank)
{
    struct i2c_adapter *adapter = i2c_get_adapter(I2C_ANABEL_BUS);

    if (blank)
		pnx8550fb_anabel_msg.buf = (unsigned char *) pnx8550fb_anabel_blank;
	else
		pnx8550fb_anabel_msg.buf = (unsigned char *) pnx8550fb_anabel_unblank;

	if ((i2c_transfer(adapter, &pnx8550fb_anabel_msg, 1)) != 1)
		printk(KERN_ERR "%s: write error\n", __func__);
}

/* Function used to set up PAL/NTSC using the QVCP */
static void pnx8550fb_setup_QVCP(unsigned int buffer, int pal)
{
    // start PLL & DDS
    PNX8550_CM_PLL2_CTL = PNX8550_CM_PLL_27MHZ;
    PNX8550_CM_DDS0_CTL = PNX8550_CM_DDS_27MHZ;
    // enable clocks
    PNX8550_CM_QVCP1_OUT_CTL = PNX8550_CM_QVCP_CLK_ENABLE
			| PNX8550_CM_QVCP_CLK_FCLOCK;
    PNX8550_CM_QVCP1_PIX_CTL = PNX8550_CM_QVCP_CLK_ENABLE
			| PNX8550_CM_QVCP_CLK_FCLOCK | PNX8550_CM_QVCP_CLK_DIV_2;
    // process layers at 17MHz. slow but absolutely sufficient with a
    // single layer.
    PNX8550_CM_QVCP1_PROC_CTL = PNX8550_CM_QVCP_CLK_ENABLE
			| PNX8550_CM_QVCP_CLK_FCLOCK | PNX8550_CM_QVCP_CLK_PROC17;
    // disable power-down mode
    PNX8550_DCSN_POWERDOWN_CTL(PNX8550FB_QVCP1_BASE) = 0;

    // setup screen geometry
    if (pal)
    {
        PNX8550FB_QVCP1_REG(0x000) = 0x035f0137;
        PNX8550FB_QVCP1_REG(0x004) = 0x02d0035f;
        PNX8550FB_QVCP1_REG(0x008) = 0x01390016;
        PNX8550FB_QVCP1_REG(0x00c) = 0x02dd0350;
        PNX8550FB_QVCP1_REG(0x010) = 0x0001000b;
        PNX8550FB_QVCP1_REG(0x014) = 0x00AF0137;
        PNX8550FB_QVCP1_REG(0x028) = 0x012D02DD;
        PNX8550FB_QVCP1_REG(0x02c) = 0x012C02DC;
        PNX8550FB_QVCP1_REG(0x234) = 0x012002d0;
    }
    else
    {
        PNX8550FB_QVCP1_REG(0x000) = 0x03590105;
        PNX8550FB_QVCP1_REG(0x004) = 0x02d00359;
        PNX8550FB_QVCP1_REG(0x008) = 0x01070012;
        PNX8550FB_QVCP1_REG(0x00c) = 0x02dd034a;
        PNX8550FB_QVCP1_REG(0x010) = 0x0004000b;
        PNX8550FB_QVCP1_REG(0x014) = 0x00950105;
        PNX8550FB_QVCP1_REG(0x028) = 0x013002DD;
        PNX8550FB_QVCP1_REG(0x02c) = 0x012F02DC;
        PNX8550FB_QVCP1_REG(0x234) = 0x00F002d0;
    }
    // configure and enable timing generator and video output
    PNX8550FB_QVCP1_REG(0x020) = 0x20050005;
    PNX8550FB_QVCP1_REG(0x03c) = 0x0fc01401;
    // disable VBI generation
    PNX8550FB_QVCP1_REG(0x034) = 0x00000000;
    PNX8550FB_QVCP1_REG(0x038) = 0x00000000;
    // pedestals off
    PNX8550FB_QVCP1_REG(0x05c) = 0x0;
    PNX8550FB_QVCP1_REG(0x060) = 0x0;
    // noise shaping on
    PNX8550FB_QVCP1_REG(0x070) = 0x00130013;
    PNX8550FB_QVCP1_REG(0x074) = 0x803F3F3F;
    // set layer base address and size
    PNX8550FB_QVCP1_REG(0x200) = buffer;
    PNX8550FB_QVCP1_REG(0x204) = PNX8550FB_STRIDE*4;
    PNX8550FB_QVCP1_REG(0x208) = PNX8550FB_STRIDE*2;
    PNX8550FB_QVCP1_REG(0x20c) = buffer + (PNX8550FB_STRIDE*2);
    PNX8550FB_QVCP1_REG(0x210) = PNX8550FB_STRIDE*4;
    PNX8550FB_QVCP1_REG(0x214) = 8;
    PNX8550FB_QVCP1_REG(0x230) = 0x80000000 | (16<<16)|(0x30);
    PNX8550FB_QVCP1_REG(0x2b4) = PNX8550FB_WIDTH;
    // set pixel format
    PNX8550FB_QVCP1_REG(0x23c) = 0x20;
    PNX8550FB_QVCP1_REG(0x238) = 0x0;
    // disable chroma keying
    PNX8550FB_QVCP1_REG(0x25c) = 0x0;
    PNX8550FB_QVCP1_REG(0x26c) = 0x0;
    PNX8550FB_QVCP1_REG(0x27c) = 0x0;
    PNX8550FB_QVCP1_REG(0x28c) = 0x0;
    // color mode = ARGB 8888
    PNX8550FB_QVCP1_REG(0x2bc) = 0xec;
    PNX8550FB_QVCP1_REG(0x2c4) = 0xffe7eff7;
    // brightness / contrast / gamma
    PNX8550FB_QVCP1_REG(0x2b8) = 0xe00;
    PNX8550FB_QVCP1_REG(0x2cc) = 0x100f100;
    // matrix coefficients
    PNX8550FB_QVCP1_REG(0x2d0) = 0x004d0096;
    PNX8550FB_QVCP1_REG(0x2d4) = 0x001d07da;
    PNX8550FB_QVCP1_REG(0x2d8) = 0x07b60070;
    PNX8550FB_QVCP1_REG(0x2dc) = 0x009d077c;
    PNX8550FB_QVCP1_REG(0x2e0) = 0x07e60100;
	// enable the layer
    PNX8550FB_QVCP1_REG(0x240) = 0x1;
}

/* Shuts down the QVCP by powering it down */
static void pnx8550fb_shutdown_QVCP(void)
{
	// disable timing generator, and thus all layers
    PNX8550FB_QVCP1_REG(0x020) = 0x00000000;
    // power-down
    PNX8550_DCSN_POWERDOWN_CTL(PNX8550FB_QVCP1_BASE) = PNX8550_DCSN_POWERDOWN_CMD;
    // stop clock
    PNX8550_CM_QVCP1_OUT_CTL = 0;
    PNX8550_CM_QVCP1_PIX_CTL = 0;
    PNX8550_CM_QVCP1_PROC_CTL = 0;
    PNX8550_CM_PLL2_CTL = PNX8550_CM_PLL_POWERDOWN;
}

/* command to shut down the unused second video channel on the Anabel */
static const unsigned char pnx8550fb_anabel2_shutdown[] =
{
    0xa5,0x01
};
/* command to shut down the unused audio channels on the Anabel */
static const unsigned char pnx8550fb_anabel_audio_shutdown[] =
{
    0xfe,0x00
};
/* command to shut down the clocks to the second video channel on the Anabel */
static const unsigned char pnx8550fb_anabel_clock_shutdown[] =
{
    0x01,0x01,0x01
};

static struct i2c_msg pnx8550fb_anabel2_msg = {
	.flags = 0,
};

/* shuts down the secondary QVCP and the unused parts of Anabel */
static void pnx8550fb_shutdown_unused(void)
{
    struct i2c_adapter *adapter = i2c_get_adapter(I2C_ANABEL_BUS);

	// shutdown PNX8510 second video channel
	pnx8550fb_anabel2_msg.buf = (unsigned char *) pnx8550fb_anabel2_shutdown;
	pnx8550fb_anabel2_msg.addr = I2C_ANABEL2_ADDR;
	if ((i2c_transfer(adapter, &pnx8550fb_anabel2_msg, 1)) != 1)
		printk(KERN_ERR "%s: write error for VIDEO2\n", __func__);

	// shutdown both PNX8510 audio channels
	pnx8550fb_anabel2_msg.buf = (unsigned char *) pnx8550fb_anabel_audio_shutdown;
	pnx8550fb_anabel2_msg.len = 2;
	pnx8550fb_anabel2_msg.addr = I2C_ANABEL_AUDIO1_ADDR;
	if ((i2c_transfer(adapter, &pnx8550fb_anabel2_msg, 1)) != 1)
		printk(KERN_ERR "%s: write error for AUDIO1\n", __func__);
	pnx8550fb_anabel2_msg.addr = I2C_ANABEL_AUDIO2_ADDR;
	if ((i2c_transfer(adapter, &pnx8550fb_anabel2_msg, 1)) != 1)
		printk(KERN_ERR "%s: write error for AUDIO2\n", __func__);

	// shutdown clocks to PNX8510 second video channel
	pnx8550fb_anabel2_msg.buf = (unsigned char *) pnx8550fb_anabel_clock_shutdown;
	pnx8550fb_anabel2_msg.len = 3;
	pnx8550fb_anabel2_msg.addr = I2C_ANABEL_AUDIO2_ADDR;
	if ((i2c_transfer(adapter, &pnx8550fb_anabel2_msg, 1)) != 1)
		printk(KERN_ERR "%s: write error for CLOCK2\n", __func__);

    // Make sure the QVCP's output clock is enabled. If it isn't, any
    // access to the DACs will hang the system, regardless of any
    // configured bus timeouts!
    PNX8550_CM_QVCP2_OUT_CTL = PNX8550_CM_TM_CLK_ENABLE | PNX8550_CM_TM_CLK_PLL;
    // put the DACs into power-down mode
    PNX8550FB_QVCP2_DAC_REG(0xa5) = 0x01;
	// disable timing generator, and thus all layers
    PNX8550FB_QVCP2_REG(0x020) = 0x00000000;
    // power-down the module
    PNX8550_DCSN_POWERDOWN_CTL(PNX8550FB_QVCP2_BASE) = 0;
    // Stop the unnecessary clocks to the QVCP to shut it down
    // completely. The output clock to the DACs must remain running.
    PNX8550_CM_QVCP2_PIX_CTL = 0;
    PNX8550_CM_QVCP2_PROC_CTL = 0;
    // Power down the PLL as well. This is safe even though it is used
    // as the clock source to the DACs, because it still outputs a
    // standby clock (~1.6MHz) when powered down.
    PNX8550_CM_PLL3_CTL = PNX8550_CM_PLL_POWERDOWN;
}

/* Function used to initialise the screen. */
void pnx8550fb_setup_display(unsigned int base, int pal)
{
    /* Set up the QVCP registers */
    pnx8550fb_setup_QVCP(base, pal);

    /* Set up Anabel using I2C */
    pnx8550fb_setup_anabel(pal);

    /* Set up the Scart switch */
    pnx8550fb_setup_scart();
    
    /* power-down unused hardware */
    pnx8550fb_shutdown_unused();
}

/* Function used to shutdown the video system. */
void pnx8550fb_shutdown_display(void)
{
    /* power down the QVCP */
    pnx8550fb_shutdown_QVCP();

    /* Shut down the Scart switch */
    pnx8550fb_shutdown_scart();
}

EXPORT_SYMBOL(pnx8550fb_set_volume);
