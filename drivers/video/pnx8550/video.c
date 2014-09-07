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
#include <dcsn.h>
#include <cm.h>

#define I2C_SCART_BUS                  PNX8550_I2C_IP0105_BUS1
#define I2C_SCART_ADDR                 0x11
#define I2C_ANABEL_BUS                 PNX8550_I2C_IP3203_BUS1
#define I2C_ANABEL_ADDR                0x66
#define I2C_ANABEL2_ADDR               0x6e
#define I2C_ANABEL_AUDIO1_ADDR         0x76
#define I2C_ANABEL_AUDIO2_ADDR         0x7e

// sets a single register over I²C
static void pnx8550fb_set_reg(unsigned char addr, unsigned char reg, unsigned char value)
{
	struct i2c_adapter *adapter;
    unsigned char data[2] = { reg, value };
	struct i2c_msg msg = {
		.addr = addr,
		.flags = 0,
		.len = 2,
		.buf = data,
	};
	
    adapter = i2c_get_adapter(I2C_ANABEL_BUS);
	if (i2c_transfer(adapter, &msg, 1) != 1)
		printk(KERN_ERR "%s: error writing device %02x register %02x\n",
				__func__, addr, reg);
}

// sets a single register in the primary video channel of ANABEL
static void pnx8550fb_anabel_set_reg(unsigned char reg, unsigned char value)
{
	pnx8550fb_set_reg(I2C_ANABEL_ADDR, reg, value);
}

// Function used to set up PAL/NTSC in Anabel
static void pnx8550fb_setup_anabel(int pal)
{
	// make sure the clocks are enabled
	pnx8550fb_set_reg(I2C_ANABEL_AUDIO1_ADDR, 0x01, 0x00);
	pnx8550fb_set_reg(I2C_ANABEL_AUDIO1_ADDR, 0x02, 0x00);
	
	// the PNX8510 datasheet lists all undefined registers as "Must be
	// initialized to zero". these are the ones that are actually
	// initialized in the NXP splash screen code; the rest probably
	// doesn't matter.
	pnx8550fb_anabel_set_reg(0x60, 0x00);
	pnx8550fb_anabel_set_reg(0x7d, 0x00);
	
	// disable insertion of wide screen signalling, copy guard,
	// video progamming system, closed captioning & teletext
	pnx8550fb_anabel_set_reg(0x27, 0x00);
	pnx8550fb_anabel_set_reg(0x2c, 0x00);
	pnx8550fb_anabel_set_reg(0x54, 0x00);
	pnx8550fb_anabel_set_reg(0x6f, 0x00);
	
    if (pal) {
		// color burst config
		pnx8550fb_anabel_set_reg(0x28, 0x21);
		pnx8550fb_anabel_set_reg(0x29, 0x1d);
		pnx8550fb_anabel_set_reg(0x5a, 0x00);
		// chroma encoding config
		pnx8550fb_anabel_set_reg(0x61, 0x06);
		pnx8550fb_anabel_set_reg(0x63, 0xcb);
		pnx8550fb_anabel_set_reg(0x64, 0x8a);
		pnx8550fb_anabel_set_reg(0x65, 0x09);
		pnx8550fb_anabel_set_reg(0x66, 0x2a);
		pnx8550fb_anabel_set_reg(0x6c, 0x02);
		pnx8550fb_anabel_set_reg(0x6d, 0x22);
		pnx8550fb_anabel_set_reg(0x6e, 0x60);
		// screen geometry
		pnx8550fb_anabel_set_reg(0x70, 0x1a);
		pnx8550fb_anabel_set_reg(0x71, 0x9f);
		pnx8550fb_anabel_set_reg(0x72, 0x61);
		pnx8550fb_anabel_set_reg(0x7a, 0x16);
		pnx8550fb_anabel_set_reg(0x7b, 0x37);
		pnx8550fb_anabel_set_reg(0x7c, 0x40);
		// YUV encoding & pedestal
		pnx8550fb_anabel_set_reg(0xa0, 0x00);
		pnx8550fb_anabel_set_reg(0xa2, 0x10);
		pnx8550fb_anabel_set_reg(0xa3, 0x80);
		pnx8550fb_anabel_set_reg(0xa4, 0x80);
		pnx8550fb_anabel_set_reg(0x5b, 0x89);
		pnx8550fb_anabel_set_reg(0x5c, 0xbe);
		pnx8550fb_anabel_set_reg(0x5d, 0x37);
		pnx8550fb_anabel_set_reg(0x5e, 0x39);
		pnx8550fb_anabel_set_reg(0x5f, 0x39);
		pnx8550fb_anabel_set_reg(0x62, 0x38);
		// DAC output levels
		pnx8550fb_anabel_set_reg(0xc2, 0x1f);
		pnx8550fb_anabel_set_reg(0xc3, 0x0A);
		pnx8550fb_anabel_set_reg(0xc4, 0x0A);
		pnx8550fb_anabel_set_reg(0xc5, 0x0A);
		pnx8550fb_anabel_set_reg(0xc6, 0x00);
	} else {
		// color burst config
		pnx8550fb_anabel_set_reg(0x28, 0x25);
		pnx8550fb_anabel_set_reg(0x29, 0x1d);
		pnx8550fb_anabel_set_reg(0x5a, 0x88);
		// chroma encoding config
		pnx8550fb_anabel_set_reg(0x61, 0x11);
		pnx8550fb_anabel_set_reg(0x63, 0x1f);
		pnx8550fb_anabel_set_reg(0x64, 0x7c);
		pnx8550fb_anabel_set_reg(0x65, 0xf0);
		pnx8550fb_anabel_set_reg(0x66, 0x21);
		pnx8550fb_anabel_set_reg(0x6c, 0xf2);
		pnx8550fb_anabel_set_reg(0x6d, 0x03);
		pnx8550fb_anabel_set_reg(0x6e, 0xD0);
		// screen geometry
		pnx8550fb_anabel_set_reg(0x70, 0xfb);
		pnx8550fb_anabel_set_reg(0x71, 0x90);
		pnx8550fb_anabel_set_reg(0x72, 0x60);
		pnx8550fb_anabel_set_reg(0x7a, 0x00);
		pnx8550fb_anabel_set_reg(0x7b, 0x05);
		pnx8550fb_anabel_set_reg(0x7c, 0x40);
		// YUV encoding & pedestal
		pnx8550fb_anabel_set_reg(0xa0, 0x00);
		pnx8550fb_anabel_set_reg(0xa2, 0x0d);
		pnx8550fb_anabel_set_reg(0xa3, 0x80);
		pnx8550fb_anabel_set_reg(0xa4, 0x80);
		pnx8550fb_anabel_set_reg(0x5b, 0x7d);
		pnx8550fb_anabel_set_reg(0x5c, 0xaf);
		pnx8550fb_anabel_set_reg(0x5d, 0x3e);
		pnx8550fb_anabel_set_reg(0x5e, 0x32);
		pnx8550fb_anabel_set_reg(0x5f, 0x32);
		pnx8550fb_anabel_set_reg(0x62, 0x4c);
		// DAC output levels
		pnx8550fb_anabel_set_reg(0xc2, 0x1e);
		pnx8550fb_anabel_set_reg(0xc3, 0x0A);
		pnx8550fb_anabel_set_reg(0xc4, 0x0A);
		pnx8550fb_anabel_set_reg(0xc5, 0x0A);
		pnx8550fb_anabel_set_reg(0xc6, 0x01);
	}
	
	// don't perform signature analysis
	pnx8550fb_anabel_set_reg(0xba, 0x70);
	// DAC configured for CVBS + RGB (instead of Y/C)
	pnx8550fb_anabel_set_reg(0x2d, 0x40);
	// set SD (not HD) mode; interface config
	pnx8550fb_anabel_set_reg(0x3a, 0x48);
	pnx8550fb_anabel_set_reg(0x95, 0x00);
	// unblank the screen
	pnx8550fb_anabel_set_reg(0x6e, 0x00);
}

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

// enable or disable blanking
void pnx8550fb_set_blanking(int blank)
{
    if (blank)
		pnx8550fb_anabel_set_reg(0x6e, 0x40);
	else
		pnx8550fb_anabel_set_reg(0x6e, 0x00);
}

/* Function used to set up PAL/NTSC using the QVCP */
static void pnx8550fb_setup_qvcp(unsigned int buffer, int pal)
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
static void pnx8550fb_shutdown_qvcp(void)
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

/* shuts down the secondary QVCP and the unused parts of Anabel */
static void pnx8550fb_shutdown_unused(void)
{
	// shutdown PNX8510 second video channel, including clocks
	pnx8550fb_set_reg(I2C_ANABEL2_ADDR, 0xa5, 0x01);
	pnx8550fb_set_reg(I2C_ANABEL_AUDIO2_ADDR, 0x01, 0x01);
	pnx8550fb_set_reg(I2C_ANABEL_AUDIO2_ADDR, 0x02, 0x01);
	// shutdown both PNX8510 audio channels
	pnx8550fb_set_reg(I2C_ANABEL_AUDIO1_ADDR, 0xfe, 0x00);
	pnx8550fb_set_reg(I2C_ANABEL_AUDIO2_ADDR, 0xfe, 0x00);

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
    pnx8550fb_setup_qvcp(base, pal);

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
    pnx8550fb_shutdown_qvcp();

    /* Shut down the Scart switch */
    pnx8550fb_shutdown_scart();
}

EXPORT_SYMBOL(pnx8550fb_set_volume);
