/*
 *  PNX8550 framebuffer driver.
 * 
 *  (Derived from the virtual frame buffer device.)
 *
 *      Copyright (C) 2002 James Simmons
 *
 *	Copyright (C) 1997 Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/fb.h>
#include <linux/init.h>

#include <framebuffer.h>
#include <audio.h>
#include <i2c.h>
#include <dcsn.h>
#include <cm.h>
#include <ak4705.h>

static struct i2c_client *ak4705;

// sets a single register in the SCART switch (AK4705)
static int ak4705_set_reg(unsigned char reg, unsigned char value)
{
    unsigned char data[2] = { reg, value };
    int err;
    
    if (ak4705 == NULL) {
		printk(KERN_ERR
			"%s: device not present writing register %02x = %02x\n",
			__func__, reg, value);
		return -ENODEV;
	}

	err = i2c_master_send(ak4705, data, sizeof(data));
	if (err != sizeof(data)) {
		dev_err(&ak4705->dev,
			"%s: error %d writing register %02x = %02x\n",
			__func__, err, reg, value);
		return -EIO;
	}

	return 0;
}

// suspends AK4705, ie. disables all video outputs
void ak4705_set_mode(int mode)
{
	switch (mode) {
	case AK4705_SUSPEND:
		// de-assert slow & fast blanking on the SCART ports
		ak4705_set_reg(0x07, 0x80);
		// set video outputs to Hi-Z
		ak4705_set_reg(0x04, 0xC0);
		// video output enable: all disabled
		ak4705_set_reg(0x05, 0x00);
		break;
	case AK4705_CVBS_RGB:
		// video routing: TV SCART = encoder CVBS + RGB, VCR SCART = encoder
		//     CVBS, composite = encoder CVBS
		ak4705_set_reg(0x04, 0x09);
		// blanking: TV FB = +4V, TV SB = output +12V, VCR SB = input
		ak4705_set_reg(0x07, 0x8D);
		// video output enable: TV CVBS, R, G, B, FB = enabled, VCR CVBS =
		//     enabled, VCR C = disabled
		ak4705_set_reg(0x05, 0x5F);
		break;
	case AK4705_YUV:
		// de-assert slow & fast blanking on the SCART ports. there is a
		//     signal on the connector but SCART blanking is meaningless
		//     for HD YUV.
		ak4705_set_reg(0x07, 0x80);
		// video routing: TV SCART = CVBS + YUV, VCR SCART = shutdown,
		//     composite = shutdown
		ak4705_set_reg(0x04, 0xc1);
		// video output enable: TV R, G, B = enabled, VCR CVBS = disabled,
		//     TV CVBS, FB = disabled, VCR C = disabled
		ak4705_set_reg(0x05, 0x0E);
		break;
	}
}

// configures AK4705 to output RGB + CVBS on all outputs
static void ak4705_setup(void)
{
    // control: STBY, MUTE, DAPD=normal operation, chip enabled, audio
    //     24-bit I²S without de-emphasis
    ak4705_set_reg(0x00, 0x70);
    // audio routing: main audio = stereo DAC, VCR SCART audio = stereo
    //     DAC, main volume unmuted
	ak4705_set_reg(0x01, 0x74);
    // main volume: muted (until the audio driver loads and sets the
    //     default volume)
    ak4705_set_reg(0x02, 0x00);
    // audio config: main volume transition = 2048ck, DAC volume = ±0dB,
    //     VCR SCART audio = stereo
    ak4705_set_reg(0x03, 0x27);
    // video gain: RGB gain +6dB, DC restore = encoder CVBS, VCR clamp =
    //     Y/C, encoder clamp = RGB
    ak4705_set_reg(0x06, 0x04); 
    // monitor mask: video detection masked, interrupts disabled
    //     (because nothing expects them)
    ak4705_set_reg(0x09, 0x9E);
    // configure routing for suspended operation. this leaves the
    // display off until the framebuffer driver explicitly enables it,
    // avoiding flicker.
    ak4705_set_mode(AK4705_SUSPEND);
}

// places the AK4705 back into auto-startup standby mode, ie. bypassing
// from SCART2 to SCART1 if a signal is detected.
static void ak4705_shutdown(struct i2c_client *client)
{
	if (ak4705 == NULL)
		ak4705 = client;
	dev_info(&client->dev, "AK4705 SCART switch shutdown\n");

    // control: STBY, MUTE enabled, DAPD=normal operation, auto startup
    //     mode, audio 24-bit I²S without de-emphasis
    ak4705_set_reg(0x00, 0x7b);
    // the rest is just the defaults from the datasheet.
	ak4705_set_reg(0x01, 0xd5);
    ak4705_set_reg(0x02, 0x1f);
    ak4705_set_reg(0x03, 0x27);
    ak4705_set_reg(0x04, 0x9c);
    ak4705_set_reg(0x05, 0x00);
    ak4705_set_reg(0x06, 0x04); 
    ak4705_set_reg(0x07, 0x00); 
    ak4705_set_reg(0x09, 0x9E);
}

// sets the main volume in the AK4705
void ak4705_set_volume(int volume)
{
	char vol = volume;
	if (vol < 0)
		vol = 0;
	if (vol > AK4705_VOL_MAX)
		vol = AK4705_VOL_MAX;

	ak4705_set_reg(0x02, vol);
}
EXPORT_SYMBOL(ak4705_set_volume);

static int ak4705_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	dev_dbg(&client->dev, "%s at bus %d addr %02x\n", __func__,
			client->adapter->nr, client->addr);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;
	if (ak4705 != NULL) {
		dev_err(&client->dev,
			"AK4705 SCART switch driver already present\n");
		return -EBUSY;
	}

	dev_info(&client->dev, "AK4705 SCART switch driver v0.1\n");
	ak4705 = client;
	ak4705_setup();

	return 0;
}

static int ak4705_remove(struct i2c_client *client)
{
	ak4705_shutdown(client);
	ak4705 = NULL;
	return 0;
}

static const struct i2c_device_id ak4705_id[] = {
	{ "ak4705", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ak4705_id);

static struct i2c_driver ak4705_driver = {
	.driver		= {
		.name	= "ak4705",
	},
	.probe		= ak4705_probe,
	.remove		= ak4705_remove,
	.shutdown	= ak4705_shutdown,
	.id_table	= ak4705_id,
};

module_i2c_driver(ak4705_driver);

MODULE_AUTHOR("Matthias <tqzamf@gmail.com>");
MODULE_DESCRIPTION("AK4705 SCART switch driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
