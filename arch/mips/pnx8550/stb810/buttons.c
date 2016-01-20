/*
 * Frontpanel buttons and IR remote control driver.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * (C) Copyright TOSHIBA CORPORATION 2005-2007
 * All Rights Reserved.
 */
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <gpio.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <asm/mach-pnx8550/i2c.h>

#define STANDBY_MICRO_BUS  PNX8550_I2C_IP0105_BUS0
#define STANDBY_MICRO_ADDR 0x30
#define IR_MIN_SCANCODE 0x000
#define IR_MAX_SCANCODE 0x0ff
#define FP_MIN_SCANCODE 0x100
#define FP_MAX_SCANCODE 0x104
#define NUM_SCANCODES   (FP_MAX_SCANCODE + 1)
#define DEBOUNCE_TIMEOUT_MS 300

static bool ir = 1;
module_param(ir, bool, 0644);
MODULE_PARM_DESC(boot, "enable IR remote control decoding");

struct pnx8550_frontpanel_buttons_data {
	struct timer_list timer;
	struct i2c_adapter *adap;
	uint16_t prev_key;
	uint16_t keymap[NUM_SCANCODES];
};

static int16_t pnx8550_frontpanel_default_keymap[NUM_SCANCODES] = {
	// 0x00 .. 0xff: infrared commands. most of these have clear
	// equivalents, but some obscure buttons don't have any direct
	// equivalent, or have more than one possible mapping.
	// 0x00 .. 0x0f
	KEY_NUMERIC_0,    KEY_NUMERIC_1,    KEY_NUMERIC_2,   KEY_NUMERIC_3,
	KEY_NUMERIC_4,    KEY_NUMERIC_5,    KEY_NUMERIC_6,   KEY_NUMERIC_7,
	KEY_NUMERIC_8,    KEY_NUMERIC_9,    0,               KEY_POWER,
	KEY_MUTE,         0,                KEY_INFO,        KEY_VOLUMEUP,
	// 0x10 .. 0x1f
	KEY_VOLUMEDOWN,   0,                KEY_CHANNELUP,   KEY_CHANNELDOWN,
	KEY_ARCHIVE,      0,                0,               0,
	0,                0,                0,               0,
	0,                KEY_PLAY,         KEY_FASTFORWARD, KEY_PAUSE,
	// 0x20 .. 0x2f
	KEY_STOP,         KEY_REWIND,       KEY_HELP,        KEY_TEXT,
	KEY_TV,           KEY_UP,           KEY_DOWN,        KEY_MENU,
	KEY_LEFT,         KEY_RIGHT,        0,               0,
	0,                KEY_RED,          KEY_GREEN,       KEY_YELLOW,
	// 0x30 .. 0x3f
	KEY_BLUE,         0,                0,               0,
	0,                0,                0,               0,
	0,                KEY_PROGRAM,      KEY_VCR,         KEY_EXIT,
	KEY_OK,           KEY_DISPLAYTOGGLE,KEY_NUMERIC_STAR,KEY_RECORD,
	// 0x40 .. 0x4f
	KEY_TIME,         0,                0,               0,
	0,                0,                0,               KEY_NEXT,
	KEY_PREVIOUS,     0,                KEY_WWW,         KEY_FAVORITES,
	KEY_NUMERIC_POUND,KEY_SLOW,         0,               0,
	// the rest is unused and zero-filled; we just provide them in case
	// the standby micro also maps some IR commands that don't have
	// buttons on the standard remote.
	// 0x50 .. 0x7f
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	// 0x80 .. 0xbf
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	// 0xc0 .. 0xff
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	// 0x100 .. 0x103: frontpanel buttons. these are clearly labeled,
	// but remapping might be useful in some special situations. power
	// is included but its function cannot be fully remapped: it will
	// always be the button that brings the board out of power-down.
	// 0x100: 04 01 fa LEFT
	KEY_LEFT,
	// 0x101: 04 03 f8 UP
	KEY_UP,
	// 0x102: 04 05 f6 DOWN
	KEY_DOWN,
	// 0x103: 04 07 f4 RIGHT
	KEY_RIGHT,
	// 0x104: 08 f7 POWER
	KEY_POWER,
};

static int16_t parse_scancode(const unsigned char *data, unsigned int len) {
	if (len == 2 && data[0] == 0x08 && data[1] == 0xf7)
		// power button: 08 f7
		return 0x104;
	if (len == 3 && data[0] == 0x04 && (data[1] & 1) == 1
			&& data[1] <= 7 && data[2] == 0xfb - data[1])
		// frontpanel buttons: 04 xx yy
		// where xx = {0x01, 0x03, 0x05, 0x07} and yy = 0xFB - xx
		return 0x100 + (data[1] >> 1);
	if (len == 4 && data[0] == 0x02 && data[1] == 0x00
			&& data[3] == 0xfd - data[2])
		// remote commands: 02 00 xx yy
		// where xx = 0x00 .. 0x4D and yy = 0xFD - xx
		// we're mapping all 256 values, just in case
		return data[2];
	return -1;
}

static int16_t get_scancode(struct input_dev *input,
					const struct input_keymap_entry *ke)
{
	if (ke->flags & INPUT_KEYMAP_BY_INDEX)
		return ke->index;
	return parse_scancode(ke->scancode, ke->len);
}

static int pnx8550_frontpanel_buttons_getkeycode(struct input_dev *dev,
			       struct input_keymap_entry *ke)
{
	struct pnx8550_frontpanel_buttons_data *data = input_get_drvdata(dev);
	int16_t scancode;

	scancode = get_scancode(dev, ke);
	if (scancode >= 0 && scancode < NUM_SCANCODES) {
		ke->keycode = data->keymap[scancode];
		ke->index = scancode;
		ke->len = sizeof(scancode);
		memcpy(ke->scancode, &scancode, sizeof(scancode));
		return 0;
	}

	return -EINVAL;
}

static int pnx8550_frontpanel_buttons_setkeycode(struct input_dev *dev,
			       const struct input_keymap_entry *ke,
			       unsigned int *old_keycode)
{
	struct pnx8550_frontpanel_buttons_data *data = input_get_drvdata(dev);
	int16_t scancode, i;
	bool need_clear_bit = 1;

	scancode = get_scancode(dev, ke);
	if (scancode >= 0 && scancode < NUM_SCANCODES) {
		// update keymap table
		*old_keycode = data->keymap[scancode];
		data->keymap[scancode] = ke->keycode;
		// update declared keys
		for (i = 0; i < NUM_SCANCODES; i++)
			if (data->keymap[i] == ke->keycode) {
				need_clear_bit = 0;
				break;
			}
		if (need_clear_bit)
			clear_bit(*old_keycode, dev->keybit);
		set_bit(ke->keycode, dev->keybit);
		return 0;
	}

	return -EINVAL;
}

static void pnx8550_frontpanel_buttons_timeout(unsigned long device)
{
	struct input_dev *input = (void *) device;
	struct pnx8550_frontpanel_buttons_data *data = input_get_drvdata(input);

	// release whichever key we are currently reporting as down. this
	// shouldn't fire with a zero key, ever.
	if (data->prev_key != 0) {
		input_report_key(input, data->prev_key, 0);
		input_sync(input);
	}
	data->prev_key = 0;
}

static void pnx8550_frontpanel_buttons_callback(struct device *device,
	unsigned char *code, unsigned int len)
{
	int16_t scancode;
	uint16_t key;
	struct input_dev *input = dev_get_drvdata(device);
	struct pnx8550_frontpanel_buttons_data *data = input_get_drvdata(input);

	scancode = parse_scancode(code, len);
	if (scancode >= FP_MIN_SCANCODE && scancode <= FP_MAX_SCANCODE)
		// always map the frontpanel buttons
		key = data->keymap[scancode];
	else if (scancode >= IR_MIN_SCANCODE && scancode <= IR_MAX_SCANCODE)
		// only map IR commands if enabled. if disabled, map them to
		// zero so that they act as key release events.
		key = ir ? data->keymap[scancode] : 0;
	else {
		dev_dbg(device, "frontpanel: unknown event"
			" %02x %02x %02x %02x %02x %02x len=%d\n",
			code[0], code[1], code[2], code[3], code[4], code[5], len);
		key = 0;
	}

	// get rid of the timer. we have a new event, so whatever key
	// releasing will now be done below, not in the delayed release
	// timer.
	del_timer(&data->timer);

	// if a new key has been pressed, we need to report the old one as
	// released and the new one as pressed.
	// note that switching from a non-mapped to a mapped key (and vice
	// versa) will cause this rule to apply as well. therefore, we may
	// end up skipping the release, press, or both.
	if (key != data->prev_key) {
		if (data->prev_key != 0) {
			input_report_key(input, data->prev_key, 0);
			input_sync(input);
		}
		data->prev_key = key;
		if (key != 0) {
			input_report_key(input, key, 1);
			input_sync(input);
		}
	}

	// if some key is still down, we need to release it in 300ms. this
	// applies regardless of whether it's the same key as last time.
	if (key != 0)
		mod_timer(&data->timer,
				jiffies + msecs_to_jiffies(DEBOUNCE_TIMEOUT_MS));
}


static int __devinit pnx8550_frontpanel_buttons_probe(struct platform_device *pdev)
{
	struct input_dev *input;
	struct pnx8550_frontpanel_buttons_data *data;
	int i;
	int res;

	// create a local copy of the keymap, pretending that there could be
	// more than a single instance of the driver.
	data = kzalloc(sizeof(struct pnx8550_frontpanel_buttons_data), GFP_KERNEL);
	if (!data) {
		res = -ENOMEM;
		goto err0;
	}
	memcpy(data->keymap, pnx8550_frontpanel_default_keymap,
			NUM_SCANCODES * sizeof(uint16_t));
	// set up the input device
	input = input_allocate_device();
	if (!input) {
		res = -ENOMEM;
		goto err1;
	}
	input_set_drvdata(input, data);
	platform_set_drvdata(pdev, input);
	input->dev.parent = &pdev->dev;
	input->name = pdev->name;
	input->phys = "frontpanel/input0";
	input->id.bustype = BUS_I2C;
	input->setkeycode = pnx8550_frontpanel_buttons_setkeycode;
	input->getkeycode = pnx8550_frontpanel_buttons_getkeycode;
	// declare the events we fire
	set_bit(EV_KEY, input->evbit);
	for (i = 0; i < NUM_SCANCODES; i++)
		set_bit(data->keymap[i], input->keybit);
	res = input_register_device(input);
	if (res)
		goto err2;
	// timer init
	init_timer(&data->timer);
	data->timer.data = (unsigned long) input;
	data->timer.function = pnx8550_frontpanel_buttons_timeout;

	// tell the I²C driver to forward all data from the standby micro to
	// us. there appears to be no API for doing this cleanly.
	data->adap = i2c_get_adapter(STANDBY_MICRO_BUS);
	if (!data->adap)
		goto err3;
	res = ip0105_set_slave_callback(data->adap, STANDBY_MICRO_ADDR,
			&pdev->dev, pnx8550_frontpanel_buttons_callback);
	if (res)
		goto err3;

	// set standby pin low. this indicates to the standby micro that we
	// are ready to start receiving frontpanel button and IR command
	// reports.
	// it also means that we cannot reboot successfully unless the boot
	// script set GPIO12 low; a reset releases GPIO12 and thus causes
	// the board to power down after ~1sec. that, however, is exactly
	// the behavior that all other OSes exhibit on the board, so it's
	// probably either fine or at least consistent.
	// nevertheless, don't touch the GPIO until everything else is
	// finished, because we'd have no way to undo it when we run across
	// an error afterwards.
	PNX8550_GPIO_SET_LOW(PNX8550_GPIO_STANDBY);
	return 0;

err3:
	input_unregister_device(input);
err2:
	input_free_device(input);
err1:
	kfree(data);
err0:
	return res;
}

static int __devexit pnx8550_frontpanel_buttons_remove(struct platform_device *pdev)
{
	struct input_dev *input = platform_get_drvdata(pdev);
	struct pnx8550_frontpanel_buttons_data *data = input_get_drvdata(input);

	// get rid of callback, if registered
	if (data->adap != NULL)
		ip0105_set_slave_callback(data->adap, 0, NULL, NULL);
	// remove input device
	input_unregister_device(input);
	input_free_device(input);
	kfree(data);

	return 0;
}

static struct platform_driver pnx8550_frontpanel_buttons_driver = {
	.probe		= pnx8550_frontpanel_buttons_probe,
	.remove		= __devexit_p(pnx8550_frontpanel_buttons_remove),
	.driver		= {
		.name	= "buttons",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(pnx8550_frontpanel_buttons_driver);

MODULE_DESCRIPTION("PNX8550 frontpanel buttons and IR remote control driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:buttons");
