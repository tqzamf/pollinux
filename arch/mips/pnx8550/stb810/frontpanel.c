/*
 * 7 Segment LED routines
 * Based on RBTX49xx patch from CELF patch archive.
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
#include <linux/map_to_7segment.h>
#include <gpio.h>
#include <linux/platform_device.h>
#include <linux/leds.h>

static bool flip = 0;
module_param(flip, bool, 0644);
MODULE_PARM_DESC(flip, "correct display for panel mounted upside down");

static char* boot = "boot";
module_param(boot, charp, 0);
MODULE_PARM_DESC(boot, "string to display during boot");

// sends a raw command to the chip
static void pnx8550_frontpanel_send(unsigned char *data, int len) {
	int i, j;
	
	for (j = 0; j < len; j++) {
		unsigned int byte = data[j];
		for (i = 0; i < 8; i++) {
			PNX8550_GPIO_SET_VALUE(PNX8550_GPIO_PT6955_DATA, byte & 1);
			byte >>= 1;
			PNX8550_GPIO_SET_HIGH(PNX8550_GPIO_PT6955_CLOCK);
			PNX8550_GPIO_SET_LOW(PNX8550_GPIO_PT6955_CLOCK);
		}
	}

	PNX8550_GPIO_SET_HIGH(PNX8550_GPIO_PT6955_STROBE);
	PNX8550_GPIO_SET_LOW(PNX8550_GPIO_PT6955_STROBE);
}

// raw cached display data (digits)
static unsigned char pnx8550_frontpanel_digits[4] = { 255, 255, 255, 255, };
// the dots: left dot = 0x01, right dot = 0x08
static unsigned char pnx8550_frontpanel_dots;
// the LEDs: upper = 0x01, lower = 0x02
static unsigned char pnx8550_frontpanel_leds;

// corrects for a 7segment display rotated 180 degrees
unsigned char flip_table[128] = {
	0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0x01, 0x09, 0x11, 0x19, 0x21, 0x29, 0x31, 0x39,
	0x02, 0x0a, 0x12, 0x1a, 0x22, 0x2a, 0x32, 0x3a, 0x03, 0x0b, 0x13, 0x1b, 0x23, 0x2b, 0x33, 0x3b,
	0x04, 0x0c, 0x14, 0x1c, 0x24, 0x2c, 0x34, 0x3c, 0x05, 0x0d, 0x15, 0x1d, 0x25, 0x2d, 0x35, 0x3d,
	0x06, 0x0e, 0x16, 0x1e, 0x26, 0x2e, 0x36, 0x3e, 0x07, 0x0f, 0x17, 0x1f, 0x27, 0x2f, 0x37, 0x3f,
	0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x78, 0x41, 0x49, 0x51, 0x59, 0x61, 0x69, 0x71, 0x79,
	0x42, 0x4a, 0x52, 0x5a, 0x62, 0x6a, 0x72, 0x7a, 0x43, 0x4b, 0x53, 0x5b, 0x63, 0x6b, 0x73, 0x7b,
	0x44, 0x4c, 0x54, 0x5c, 0x64, 0x6c, 0x74, 0x7c, 0x45, 0x4d, 0x55, 0x5d, 0x65, 0x6d, 0x75, 0x7d,
	0x46, 0x4e, 0x56, 0x5e, 0x66, 0x6e, 0x76, 0x7e, 0x47, 0x4f, 0x57, 0x5f, 0x67, 0x6f, 0x77, 0x7f,
};

// reformat pnx8550_frontpanel_{digits,dots} into display format
// and send it to the chip
static void pnx8550_frontpanel_update_display(void) {
	unsigned char buffer[8] = { 0xc0, 0, 0, 0, 0, 0, 0, 0, };
	if (flip) {
		buffer[1] = flip_table[pnx8550_frontpanel_digits[0] & 0x7f];
		buffer[3] = flip_table[pnx8550_frontpanel_digits[1] & 0x7f];
		buffer[5] = flip_table[pnx8550_frontpanel_digits[2] & 0x7f];
		buffer[7] = flip_table[pnx8550_frontpanel_digits[3] & 0x7f];
		if (pnx8550_frontpanel_dots & 8)
			buffer[7] |= 0x80;
		if (pnx8550_frontpanel_dots & 4)
			buffer[5] |= 0x80;
		if (pnx8550_frontpanel_dots & 2)
			buffer[3] |= 0x80;
		if (pnx8550_frontpanel_dots & 1)
			buffer[1] |= 0x80;
	} else {
		buffer[7] = (pnx8550_frontpanel_digits[0] & 0x7f);
		buffer[5] = (pnx8550_frontpanel_digits[1] & 0x7f);
		buffer[3] = (pnx8550_frontpanel_digits[2] & 0x7f);
		buffer[1] = (pnx8550_frontpanel_digits[3] & 0x7f);
		if (pnx8550_frontpanel_dots & 1)
			buffer[7] |= 0x80;
		if (pnx8550_frontpanel_dots & 2)
			buffer[5] |= 0x80;
		if (pnx8550_frontpanel_dots & 4)
			buffer[3] |= 0x80;
		if (pnx8550_frontpanel_dots & 8)
			buffer[1] |= 0x80;
	}
	pnx8550_frontpanel_send(buffer, 8);
}

// send pnx8550_frontpanel_leds to the chip, swapping them if necessary
static void pnx8550_frontpanel_update_leds(void) {
	unsigned char buffer[2] = { 0xc8, 0, };
	if (flip)
		buffer[1] = pnx8550_frontpanel_leds;
	else
		buffer[1] = (pnx8550_frontpanel_leds >> 1) | (pnx8550_frontpanel_leds << 1);
	pnx8550_frontpanel_send(buffer, 2);
}


// brightness attribute (0..7, but it isn't visually linear)
static unsigned char pnx8550_frontpanel_brightness;

static ssize_t bright_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t size)
{
	unsigned char buffer;
	if (size != 1 && !(size == 2 && (buf[1] == '\n' || buf[1] == '\r')))
		return -EINVAL;
	buffer = buf[0] - '0';
	if (buffer > 7 || buffer < 0)
		return -EINVAL;
	pnx8550_frontpanel_brightness = buffer;
	
	buffer |= 0x88;
	pnx8550_frontpanel_send(&buffer, 1);
	return size;
}

static ssize_t bright_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	buf[0] = pnx8550_frontpanel_brightness + '0';
	return 1;
}

static DEVICE_ATTR(brightness, 0600, bright_show, bright_store);


// allows raw commands to be sent to the chip, up to 16 bytes. the
// highest useful amount is 14 because then the address wraps around.
static struct bin_attribute command;

static ssize_t command_write(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr,
		char *buf, loff_t off, size_t count)
{
	if (off > command.size)
		return 0;
	if (count + off > command.size)
		count = command.size - off;
	pnx8550_frontpanel_send((unsigned char *) buf, count);
	return count;
}

static struct bin_attribute command = {
	.attr = {
		.name = "command",
		.mode = 0200,
	},
	.size = 16,
	.write = command_write,
};


// allows access to "raw" display memory. this is formatted differently
// than in the chip though.
static struct bin_attribute raw;

static ssize_t raw_write(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr,
		char *buf, loff_t off, size_t count)
{
	if (off > raw.size)
		return 0;
	if (off + count > raw.size)
		count = raw.size - off;

	memcpy(&pnx8550_frontpanel_digits[off], buf, count);
	pnx8550_frontpanel_update_display();
	return count;
}

static ssize_t raw_read(struct file *filp, struct kobject *kobj,
			   struct bin_attribute *bin_attr,
			   char *buf, loff_t off, size_t count)
{
	if (off > raw.size)
		return -ENOSPC;
	if (off + count > raw.size)
		count = raw.size - off;

	memcpy(buf, &pnx8550_frontpanel_digits[off], count);
	return count;
}

static struct bin_attribute raw = {
	.attr = {
		.name = "raw",
		.mode = 0600,
	},
	.size = 4,
	.write = raw_write,
	.read = raw_read,
};

// accesses the "dots" byte of display memory. this is presented as a row
// of asterisks or spaces, for ideal usability. on write, anything except
// space sets the corresponding dot, and newline terminates input.
static ssize_t dots_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t size)
{
	int i, bit;

	for (i = 0, bit = 0x01; i < 4 && i < size; i++, bit <<= 1) {
		if (buf[i] == '\n' || buf[i] == '\r')
			break;
		if (buf[i] == ' ')
			pnx8550_frontpanel_dots &= ~bit;
		else
			pnx8550_frontpanel_dots |= bit;
	}

	pnx8550_frontpanel_update_display();
	return size;
}

static ssize_t dots_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	int i, bit;
	
	for (i = 0, bit = 0x01; i < 4; i++, bit <<= 1)
		if (pnx8550_frontpanel_dots & bit)
			buf[i] = '*';
		else
			buf[i] = ' ';
	buf[4] = '\n';

	return 5;
}

static DEVICE_ATTR(dots, 0600, dots_show, dots_store);


// character conversion table. this defaults to something sensible, but can be
// changed by modifying the charmap file
static SEG7_CONVERSION_MAP(pnx8550_frontpanel_charmap, MAP_ASCII7SEG_ALPHANUM_LC);
static struct bin_attribute charmap;

static ssize_t charmap_write(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr,
		char *buf, loff_t off, size_t count)
{
	if (off > charmap.size)
		return 0;
	if (off + count > charmap.size)
		count = charmap.size - off;

	memcpy(&pnx8550_frontpanel_charmap.table[off], buf, count);
	return count;
}

static ssize_t charmap_read(struct file *filp, struct kobject *kobj,
			   struct bin_attribute *bin_attr,
			   char *buf, loff_t off, size_t count)
{
	if (off > charmap.size)
		return -ENOSPC;
	if (off + count > charmap.size)
		count = charmap.size - off;

	memcpy(buf, &pnx8550_frontpanel_charmap.table[off], count);
	return count;
}

static struct bin_attribute charmap = {
	.attr = {
		.name = "charmap",
		.mode = 0600,
	},
	.size = 128,
	.write = charmap_write,
	.read = charmap_read,
};

// ASCII-mapped output. write-only; writing just stores to the raw memory
// going through the character map. only writes to the digits part of
// display memory. remaining digits are filled up with spaces.
static ssize_t ascii_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t size)
{
	int i;
	memset(pnx8550_frontpanel_digits, 0, 4);
	for (i = 0; i < 4 && i < size; i++)
		pnx8550_frontpanel_digits[i] = map_to_seg7(&pnx8550_frontpanel_charmap, buf[i]);
	pnx8550_frontpanel_update_display();
	return size;
}

static DEVICE_ATTR(ascii, 0200, NULL, ascii_store);


static void pnx8550_frontpanel_led_set(struct led_classdev *led_cdev,
	enum led_brightness value);
struct led_classdev pnx8550_frontpanel_led_upper = {
	.name = "fp-upper",
	.max_brightness = LED_FULL,
	.brightness_set = pnx8550_frontpanel_led_set,
};
struct led_classdev pnx8550_frontpanel_led_lower = {
	.name = "fp-lower",
	.max_brightness = LED_FULL,
	.brightness_set = pnx8550_frontpanel_led_set,
};

static void pnx8550_frontpanel_led_set(struct led_classdev *led,
	enum led_brightness value)
{
	int bit;

	if (led == &pnx8550_frontpanel_led_upper)
		bit = 0x01;
	else if (led == &pnx8550_frontpanel_led_lower)
		bit = 0x02;
	else
		// unknown LED; we cannot set that
		BUG();
	
	if (value == LED_OFF)
		pnx8550_frontpanel_leds &= ~bit;
	else
		pnx8550_frontpanel_leds |= bit;
	
	pnx8550_frontpanel_update_leds();
}


// driver shutdown. clears the display
static void pnx8550_frontpanel_shutdown(struct platform_device *pdev)
{
	// clear display at shutdown. this might be a reboot, so the chip need
	// not be powered down immediately after
	pnx8550_frontpanel_dots = 0;
	ascii_store(0, 0, NULL, 0);
	
	// clear LEDs too
	pnx8550_frontpanel_leds = 0;
	pnx8550_frontpanel_update_leds();

	// blank the chip. this is the easiest way to make sure the display
	// and LEDs are off during reboot.
	pnx8550_frontpanel_send("\x80", 1);
}

// driver initialization. uses GPIOLIB to reserve the GPIOs, but doesn't
// otherwise use it and accesses the hardware registers directly
static int __devexit pnx8550_frontpanel_remove(struct platform_device *pdev);
static int __devinit pnx8550_frontpanel_probe(struct platform_device *pdev)
{
	int res;
	int *base = pdev->dev.platform_data;
	
	// reserve the pins. for performance, we don't actually go through the
	// GPIO driver at all
	gpio_request(*base + 0, "frontpanel display");
	gpio_request(*base + 1, "frontpanel display");
	gpio_request(*base + 2, "frontpanel display");
	PNX8550_GPIO_SET_LOW(PNX8550_GPIO_PT6955_CLOCK);
	PNX8550_GPIO_SET_LOW(PNX8550_GPIO_PT6955_STROBE);
	// configure for push-pull operation to allow faster timing
	PNX8550_GPIO_MODE_PUSHPULL(PNX8550_GPIO_PT6955_DATA);
	PNX8550_GPIO_MODE_PUSHPULL(PNX8550_GPIO_PT6955_CLOCK);
	PNX8550_GPIO_MODE_PUSHPULL(PNX8550_GPIO_PT6955_STROBE);
	
	// clear display memory. if the driver shutdown didn't run properly,
	// the display memory may not have been cleared.
	pnx8550_frontpanel_shutdown(pdev);

	// make sure scanning is enabled. the chip powers up that way,
	// but may not have been reset properly on reboot.
	pnx8550_frontpanel_send("\x40", 1);
	// set brightness to maximum
	pnx8550_frontpanel_send("\x8f", 1);
	
	res = led_classdev_register(&pdev->dev, &pnx8550_frontpanel_led_upper);
	res += led_classdev_register(&pdev->dev, &pnx8550_frontpanel_led_lower);
	if (res) {
		printk(KERN_ERR "PNX8550 frontpanel failed to register LEDs\n");
		pnx8550_frontpanel_remove(pdev);
		return 1;
	}

	res = sysfs_create_bin_file(&pdev->dev.kobj, &raw);
	res += sysfs_create_bin_file(&pdev->dev.kobj, &charmap);
	res += sysfs_create_bin_file(&pdev->dev.kobj, &command);
	res += device_create_file(&pdev->dev, &dev_attr_ascii);
	res += device_create_file(&pdev->dev, &dev_attr_brightness);
	res += device_create_file(&pdev->dev, &dev_attr_dots);
	if (res) {
		printk(KERN_ERR "PNX8550 frontpanel failed to register sysfs files\n");
		pnx8550_frontpanel_remove(pdev);
		return 1;
	}

	// send boot string to display. by default, this is just "boot", and
	// userspace can later change this to reflect the new system status.
	ascii_store(NULL, NULL, boot, strlen(boot));
	return 0;
}

// driver shutdown. clears the display and formally releases the GPIOs
static int __devexit pnx8550_frontpanel_remove(struct platform_device *pdev)
{
	int *base = pdev->dev.platform_data;

	// remove the LEDs
	led_classdev_unregister(&pnx8550_frontpanel_led_upper);
	led_classdev_unregister(&pnx8550_frontpanel_led_lower);

	// clear display; we shouldn't leave debris lying around
	pnx8550_frontpanel_shutdown(pdev);

	// release GPIOs
	gpio_free(*base + 0);
	gpio_free(*base + 1);
	gpio_free(*base + 2);

	return 0;
}

static struct platform_driver pnx8550_frontpanel_driver = {
	.probe		= pnx8550_frontpanel_probe,
	.remove		= __devexit_p(pnx8550_frontpanel_remove),
	.shutdown   = pnx8550_frontpanel_shutdown,
	.driver		= {
		.name	= "frontpanel",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(pnx8550_frontpanel_driver);

MODULE_AUTHOR("Raphael Assenat <raph@8d.com>, Trent Piepho <tpiepho@freescale.com>");
MODULE_DESCRIPTION("PNX8550 frontpanel driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:frontpanel");
