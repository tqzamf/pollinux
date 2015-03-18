/*
 * CIMaX driver for PNX8550 STB810 board.
 *
 * Partially based on:
 *   linux/drivers/char/mem.c
 *   Copyright (C) 1991, 1992  Linus Torvalds
 *
 * GPLv2.
 */

#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <asm/page.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/uio_driver.h>

#include <pci.h>
#include <xio.h>
#include <gpio.h>
#include <int.h>
#include <cimax.h>
#include <i2c.h>

struct cimax {
	spinlock_t lock;
	unsigned long flags;
	struct uio_info info;
	unsigned char am_timing_sel;
	unsigned char cm_timing_sel;
	unsigned char power_control;
	unsigned char module_control;
	struct i2c_adapter *i2c_adapter;
	unsigned char i2c_addr;
};


//////////////////// i2c helpers ////////////////////

static int cimax_read_reg(struct cimax *chip, unsigned char reg) {
	unsigned char result, res;
	struct i2c_msg cimax_read_msg[2] = {
		{
			.addr = chip->i2c_addr,
			.flags = 0,
			.len = 1,
			.buf = &reg,
		}, {
			.addr = chip->i2c_addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = &result,
		}
	};

	res = i2c_transfer(chip->i2c_adapter, cimax_read_msg, 2);
	if (res != 2) {
		printk(KERN_ERR "cimax: error reading register %02x: %d\n",
				reg, res);
		return -EIO;
	}

	return result;
}

static int cimax_write_regs(struct cimax *chip, unsigned char *buffer, int length) {
	int res;
	struct i2c_msg cimax_write_msg = {
		.addr = chip->i2c_addr,
		.flags = 0,
		.len = length,
		.buf = buffer,
	};

	res = i2c_transfer(chip->i2c_adapter, &cimax_write_msg, 1);
	if (res != 1) {
		if (length == 2)
			printk(KERN_ERR "cimax: error writing register %02x: %d\n",
					*buffer, res);
		else
			printk(KERN_ERR "cimax: error writing registers %02x..%02x: %d\n",
					*buffer, (*buffer + length - 1) & 0x1F, res);
		return -EIO;
	}

	return 0;
}

static int cimax_write_reg(struct cimax *chip, unsigned char reg, unsigned char value) {
	unsigned char buffer[2] = { reg, value };
	return cimax_write_regs(chip, buffer, 2);
}

static int cimax_set_timing(struct cimax *chip) {
	char timing = CIMAX_TIMING_AM(chip->am_timing_sel)
	            | CIMAX_TIMING_CM(chip->cm_timing_sel);
	return cimax_write_reg(chip,  CIMAX_TIMING_A, timing);
}


//////////////////// sysfs control interface ////////////////////

static ssize_t vcc_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t size)
{
	struct cimax *chip = dev_get_drvdata(dev);
	int res, len;

	len = size;
	if (len > 1 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
		len--;

	if (len == 1 && buf[0] == '0')
		chip->power_control = 0;
	else if (len == 1 && buf[0] == '1')
		chip->power_control = CIMAX_POWER_VCC_EN;
	else if (len == 2 && !strncasecmp(buf, "on", 2))
		chip->power_control = CIMAX_POWER_VCC_EN;
	else if (len == 3 && !strncasecmp(buf, "off", 3))
		chip->power_control = 0;
	else
		return -EINVAL;

	res = cimax_write_reg(chip, CIMAX_POWER, chip->power_control);
	if (res < 0)
		return res;

	return size;
}
static ssize_t vcc_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct cimax *chip = dev_get_drvdata(dev);

	if (chip->power_control & CIMAX_POWER_VCC_EN)
		strcpy(buf, "on\n");
	else
		strcpy(buf, "off\n");
	return strlen(buf);
}
static DEVICE_ATTR(vcc, 0600, vcc_show, vcc_store);

static ssize_t reset_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t size)
{
	struct cimax *chip = dev_get_drvdata(dev);
	int res, len;

	len = size;
	if (len > 1 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
		len--;

	if (len == 1 && buf[0] == '0')
		chip->module_control &= ~CIMAX_MC_RESET;
	else if (len == 1 && buf[0] == '1')
		chip->module_control |= CIMAX_MC_RESET;
	else if (len == 3 && !strncasecmp(buf, "res", 3))
		chip->module_control |= CIMAX_MC_RESET;
	else if (len == 3 && !strncasecmp(buf, "run", 3))
		chip->module_control &= ~CIMAX_MC_RESET;
	else if (len == 5 && !strncasecmp(buf, "reset", 5))
		chip->module_control |= CIMAX_MC_RESET;
	else if (len == 7 && !strncasecmp(buf, "running", 7))
		chip->module_control &= ~CIMAX_MC_RESET;
	else
		return -EINVAL;

	res = cimax_write_reg(chip, CIMAX_MC_A, chip->module_control);
	if (res < 0)
		return res;

	return size;
}
static ssize_t reset_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct cimax *chip = dev_get_drvdata(dev);
	int res;

	res = cimax_read_reg(chip, CIMAX_MC_A);
	if (res < 0)
		return res;

	if (res & CIMAX_MC_RESET)
		strcpy(buf, "reset\n");
	else
		strcpy(buf, "running\n");
	return strlen(buf);
}
static DEVICE_ATTR(reset, 0600, reset_show, reset_store);

static const struct cimax_timing {
	char name[4];
	char sel;
} cimax_timings[] = {
#define TIMING(x) { .name = #x, .sel = CIMAX_TIMING_##x##NS }
	TIMING(100),
	TIMING(150),
	TIMING(200),
	TIMING(250),
	TIMING(600),
};
static struct device_attribute dev_attr_am_timing;
static struct device_attribute dev_attr_cm_timing;
static ssize_t timing_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t size)
{
	struct cimax *chip = dev_get_drvdata(dev);
	int res, len, i;
	char sel = -1;

	len = size;
	if (len > 1 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
		len--;
	if (len > 2 && ((buf[len-2] == 'n' || buf[len-2] == 'N')
	             && (buf[len-1] == 's' || buf[len-1] == 'S')))
		len -= 2;
	if (len != 3)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(cimax_timings); i++)
		if (!strncmp(buf, cimax_timings[i].name, 3)) {
			sel = cimax_timings[i].sel;
			break;
		}
	if (sel < 0)
		return -EINVAL;

	if (attr == &dev_attr_am_timing)
		chip->am_timing_sel = sel;
	else if (attr == &dev_attr_cm_timing)
		chip->cm_timing_sel = sel;
	else
		BUG();

	res = cimax_set_timing(chip);
	if (res != 0)
		return res;

	return size;
}
static ssize_t timing_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct cimax *chip = dev_get_drvdata(dev);
	int i;
	char sel;

	if (attr == &dev_attr_am_timing)
		sel = chip->am_timing_sel;
	else if (attr == &dev_attr_cm_timing)
		sel = chip->cm_timing_sel;
	else
		BUG();

	buf[0] = 0;
	for (i = 0; i < ARRAY_SIZE(cimax_timings); i++) {
		if (i != 0)
			strcat(buf, " ");
		if (cimax_timings[i].sel == sel)
			strcat(buf, "[");
		strcat(buf, cimax_timings[i].name);
		strcat(buf, "ns");
		if (cimax_timings[i].sel == sel)
			strcat(buf, "]");
	}
	strcat(buf, "\n");

	return strlen(buf);
}
static DEVICE_ATTR(am_timing, 0600, timing_show, timing_store);
static DEVICE_ATTR(cm_timing, 0600, timing_show, timing_store);

static struct cimax_address_space {
	char shortname[3];
	char *name;
	char sel;
} cimax_address_spaces[] = {
	{ "am", "attribute-memory", CIMAX_MC_ACS_AM },
	{ "cm", "common-memory",    CIMAX_MC_ACS_CM },
	{ "io", "io-space",         CIMAX_MC_ACS_IO },
	{ "ec", "extended-channel", CIMAX_MC_ACS_EC },
};
static ssize_t address_space_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t size)
{
	struct cimax *chip = dev_get_drvdata(dev);
	int res, len, i, sz;
	char sel = -1;

	len = size;
	if (len > 1 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
		len--;

	for (i = 0; i < ARRAY_SIZE(cimax_address_spaces); i++) {
		sz = strlen(cimax_address_spaces[i].name);
		if (len == sz && !strncmp(buf, cimax_address_spaces[i].name, sz)) {
			sel = cimax_address_spaces[i].sel;
			break;
		}
		sz = strlen(cimax_address_spaces[i].shortname);
		if (len == sz && !strncmp(buf, cimax_address_spaces[i].shortname, sz)) {
			sel = cimax_address_spaces[i].sel;
			break;
		}
	}
	if (sel < 0)
		return -EINVAL;
	chip->module_control = (chip->module_control & ~CIMAX_MC_ACS_MASK) | sel;

	res = cimax_write_reg(chip, CIMAX_MC_A, chip->module_control);
	if (res != 0)
		return res;

	return size;
}
static ssize_t address_space_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct cimax *chip = dev_get_drvdata(dev);
	int i;
	char sel;
	int res;

	res = cimax_read_reg(chip, CIMAX_MC_A);
	if (res < 0)
		return res;

	sel = res & CIMAX_MC_ACS_MASK;
	buf[0] = 0;
	for (i = 0; i < ARRAY_SIZE(cimax_address_spaces); i++) {
		if (i != 0)
			strcat(buf, " ");
		if (cimax_address_spaces[i].sel == sel)
			strcat(buf, "[");
		strcat(buf, cimax_address_spaces[i].name);
		if (cimax_address_spaces[i].sel == sel)
			strcat(buf, "]");
	}
	strcat(buf, "\n");

	return strlen(buf);
}
static DEVICE_ATTR(address_space, 0600, address_space_show, address_space_store);

static ssize_t presence_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct cimax *chip = dev_get_drvdata(dev);
	int status;

	status = cimax_read_reg(chip, CIMAX_MC_A);
	if (status < 0)
		return status;

	if (status & CIMAX_MC_DET)
		strcpy(buf, "present\n");
	else
		strcpy(buf, "empty\n");

	return strlen(buf);
}
static DEVICE_ATTR(presence, 0400, presence_show, NULL);

static ssize_t irq_status_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct cimax *chip = dev_get_drvdata(dev);
	int status;

	status = cimax_read_reg(chip, CIMAX_INT_STATUS);
	if (status < 0)
		return status;

	if (status & CIMAX_INT_DET_A) {
		status = cimax_read_reg(chip, CIMAX_MC_A);
		if (status < 0)
			return status;

		if (status & CIMAX_MC_DET)
			strcpy(buf, "insert\n");
		else
			strcpy(buf, "eject\n");
	} else if (status & CIMAX_INT_IRQ_A)
		strcpy(buf, "irq\n");
	else
		*buf = 0;

	return strlen(buf);
}
static DEVICE_ATTR(irq_status, 0400, irq_status_show, NULL);


//////////////////// driver initialization and shutdown ////////////////////

static irqreturn_t cimax_isr(int irq, struct uio_info *info)
{
	struct cimax *chip = info->priv;
	// because this is a dedicated IRQ, we know that it was us, and we also
	// know that it's safe to just disable the interrupt.
	if (!test_and_set_bit(0, &chip->flags))
		disable_irq_nosync(chip->info.irq);
	return IRQ_HANDLED;
}

static int cimax_set_irq(struct uio_info *info, s32 enabled)
{
	struct cimax *chip = info->priv;
	unsigned long flags;

	// locking and flags to prevent user space from messing up IRQ config,
	// as in uio_pdrv_genirq.
	spin_lock_irqsave(&chip->lock, flags);
	if (enabled) {
		if (test_and_clear_bit(0, &chip->flags))
			enable_irq(chip->info.irq);
	} else {
		if (!test_and_set_bit(0, &chip->flags))
			disable_irq(chip->info.irq);
	}
	spin_unlock_irqrestore(&chip->lock, flags);

	return 0;
}

static int cimax_open(struct uio_info *info, struct inode *inode)
{
	struct cimax *chip = info->priv;
	int status;

	// only allow open if card is present
	status = cimax_read_reg(chip, CIMAX_MC_A);
	if (status < 0)
		return status;
	if (!(status & CIMAX_MC_DET))
		return -ENODEV;

	// restore address space and high address selection. this is only
	// necessary if the card has been ejected since last open, but checking
	// for that situarion is actually slower than simply restoring the
	// state every time.
	cimax_write_reg(chip, CIMAX_MC_A, chip->module_control);
	return 0;
}

#define CHECK(x) { \
	int res = x; \
	if (res < 0) \
		return res; \
}
static int cimax_setup(struct cimax *chip) {
	// reset the chip. if it is locked, that's required to make it
	// configurable again.
	CHECK(cimax_write_reg(chip, CIMAX_CONTROL, CIMAX_CONTROL_RESET));

	// configure bus interface (the cimax is connected to the PNX8550 XIO
	// in the obvious way)
	CHECK(cimax_write_reg(chip, CIMAX_POWER, CIMAX_POWER_VCCC_PP
			| CIMAX_POWER_VCCC_AL));
	CHECK(cimax_write_reg(chip, CIMAX_INT_CONFIG, CIMAX_INT_AL | CIMAX_INT_PP));
	CHECK(cimax_write_reg(chip, CIMAX_BUS, CIMAX_BUS_MODE_DIRSTR
			| CIMAX_BUS_CS_AL | CIMAX_BUS_DIR_WR_LOW | CIMAX_BUS_STR_AL));
	CHECK(cimax_write_reg(chip, CIMAX_WAIT, CIMAX_WAIT_MODE_ACK | CIMAX_ACK_AL
			| CIMAX_ACK_PP));
	// lock the chip setup (needed for some registers to be accessible)
	CHECK(cimax_write_reg(chip, CIMAX_CONTROL, CIMAX_CONTROL_LOCK));

	// always select module A; there is nothing else connected anyway
	CHECK(cimax_write_reg(chip, CIMAX_DEST, CIMAX_DEST_SEL_MOD_A));

	// set default status: 600ns timing, attribute memory, VCC enabled
	chip->am_timing_sel = CIMAX_TIMING_600NS;
	chip->cm_timing_sel = CIMAX_TIMING_600NS;
	CHECK(cimax_set_timing(chip));
	chip->power_control = CIMAX_POWER_VCC_EN;
	CHECK(cimax_write_reg(chip, CIMAX_POWER, chip->power_control));
	// selecting attribute memory is pointless when there is no module,
	// but checking for a module is slower than just trying
	chip->module_control = CIMAX_MC_HAD | CIMAX_MC_ACS_AM;
	CHECK(cimax_write_reg(chip, CIMAX_MC_A, chip->module_control));

	// enable IRQ but not module detection. checking which of the two
	// happend requires a slow I²C transaction, so it's probably better to
	// detect a yanked-out module by an expected IRQ that never happens,
	// ie. times out.
	CHECK(cimax_write_reg(chip, CIMAX_INT_MASK, CIMAX_INT_IRQ_A));

	return 0;
}

static void cimax_shutdown(struct platform_device *pdev)
{
	struct cimax *chip = platform_get_drvdata(pdev);

	// disable XIO aperture for CIMaX
	PNX8550_XIO_SEL1 = 0;

	// don't reset the chip here. it's better to leave it configured
	// properly, so it doesn't mess with the bus.
	// instead, just turn off power to the module and disable interrupts.
	cimax_write_reg(chip, CIMAX_POWER, 0);
	cimax_set_irq(&chip->info, 0);
}

static int __devexit cimax_remove(struct platform_device *pdev);
static int __devinit cimax_probe(struct platform_device *pdev)
{
	unsigned int iobase;
	struct cimax *chip;
	int res;

	chip = kzalloc(sizeof(struct cimax), GFP_KERNEL);
	if (!chip) {
		printk(KERN_ERR "cimax: cannot allocate control structure\n");
		return -ENOMEM;
	}
	chip->info.name = "cimax";
	chip->info.version = "0.1";
	chip->info.irq = CIMAX_IRQ;
	chip->info.irq_flags = 0; // not share-able because we just disable it
	chip->info.handler = cimax_isr;
	chip->info.open = cimax_open;
	chip->info.irqcontrol = cimax_set_irq;
	chip->info.priv = chip;
	platform_set_drvdata(pdev, chip);

	// get i2c adapter
	chip->i2c_addr = CIMAX_I2C_ADDR;
	chip->i2c_adapter = i2c_get_adapter(PNX8550_I2C_IP3203_BUS1);
	if (!chip->i2c_adapter) {
		printk(KERN_ERR "cimax: cannot find i2c bus %d\n",
				PNX8550_I2C_IP3203_BUS1);
		cimax_shutdown(pdev);
		return -ENXIO;
	}

	// configure XIO aperture for CIMaX
	PNX8550_XIO_SEL1 = PNX8550_XIO_SEL_ENAB | PNX8550_XIO_SEL_SIZE_64MB
			| PNX8550_XIO_SEL_TYPE_68360 | PNX8550_XIO_SEL_USE_ACK
			| (CIMAX_OFFSET_MB / 8) * PNX8550_XIO_SEL_OFFSET;
	iobase = PNX8550_BASE18_ADDR + 1024*1024*CIMAX_OFFSET_MB;
	#define _DECLARE_BLOCK(i) do { \
			chip->info.mem[i].name = "block-" #i; \
			chip->info.mem[i].memtype = UIO_MEM_PHYS; \
			chip->info.mem[i].addr = CIMAX_BLOCK_BASE(iobase, i); \
			chip->info.mem[i].size = CIMAX_BLOCK_SIZE; \
		} while (0)
	_DECLARE_BLOCK(0);
	_DECLARE_BLOCK(1);
	_DECLARE_BLOCK(2);
	_DECLARE_BLOCK(3);

	// configure chip
	res = cimax_setup(chip);
	if (res < 0) {
		printk(KERN_ERR "cimax: cannot initialize chip\n");
		cimax_shutdown(pdev);
		return -EIO;
	}

	// create the sysfs files (on the platform device, so relative to the
	// UIO device, it's in the device/ subdirectory)
	#define ADD_SYSFS_FILE(name) \
		res = device_create_file(&pdev->dev, &dev_attr_##name); \
		if (res) { \
			printk(KERN_ERR "CIMaX failed to register sysfs file %s\n", \
					#name); \
			cimax_shutdown(pdev); \
			return res; \
		}
	ADD_SYSFS_FILE(vcc);
	ADD_SYSFS_FILE(reset);
	ADD_SYSFS_FILE(am_timing);
	ADD_SYSFS_FILE(cm_timing);
	ADD_SYSFS_FILE(address_space);
	ADD_SYSFS_FILE(presence);
	ADD_SYSFS_FILE(irq_status);

	// register as UIO device
	res = uio_register_device(&pdev->dev, &chip->info);
	if (res) {
		printk(KERN_ERR "CIMaX failed to create UIO device\n");
		cimax_shutdown(pdev);
		return res;
	}
	printk(KERN_INFO "CIMaX at %08x, i2c address %02x, irq %d\n",
			iobase, chip->i2c_addr, (int) chip->info.irq);
	return 0;
}

static int __devexit cimax_remove(struct platform_device *pdev)
{
	cimax_shutdown(pdev);
	return 0;
}

static struct platform_driver cimax_driver = {
	.probe		= cimax_probe,
	.remove		= __devexit_p(cimax_remove),
	.shutdown   = cimax_shutdown,
	.driver		= {
		.name	= "cimax",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(cimax_driver);

MODULE_DESCRIPTION("PNX8550 CIMaX driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:cimax");
