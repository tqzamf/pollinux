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

#include <pci.h>
#include <xio.h>
#include <gpio.h>
#include <int.h>
#include <cimax.h>
#include <i2c.h>

struct cimax_memblock {
	unsigned int addr;
	char *base;
	struct miscdevice device;
	char devname[12]; // need 11; using 12 for alignment
};
static struct cimax {
	struct cimax_memblock memblock[CIMAX_NUM_BLOCKS];
	unsigned int base;
	unsigned int irq;
	unsigned char i2c_addr;
	struct i2c_adapter *i2c_adapter;
	unsigned char am_timing_sel;
	unsigned char cm_timing_sel;
	unsigned char power_control;
	unsigned char module_control;
	struct work_struct irq_work;
	struct miscdevice irq_device;
	wait_queue_head_t irq_queue;
	unsigned int irq_status;
} chip;


//////////////////// memory device interface ////////////////////

static ssize_t cimax_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct cimax_memblock *block;
	unsigned int rem, off;

	block = file->private_data;
	off = *ppos;
	if (off >= CIMAX_BLOCK_SIZE)
		return 0; // EOF
	if (off + count > CIMAX_BLOCK_SIZE)
		count = CIMAX_BLOCK_SIZE - off;
	if (count >= CIMAX_BLOCK_SIZE)
		return -EFAULT;

	rem = copy_to_user(buf, block->base + off, count);
	if (rem)
		return -EFAULT;

	*ppos += count;
	return count;
}

static ssize_t cimax_write(struct file *file, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	struct cimax_memblock *block;
	unsigned int rem, off;

	block = file->private_data;
	off = *ppos;
	if (off >= CIMAX_BLOCK_SIZE)
		return 0; // EOF
	if (off + count > CIMAX_BLOCK_SIZE)
		count = CIMAX_BLOCK_SIZE - off;
	if (count >= CIMAX_BLOCK_SIZE)
		return -EFAULT;

	rem = copy_from_user(block->base + off, buf, count);
	if (rem)
		return -EFAULT;

	*ppos += count;
	return count;
}

static loff_t cimax_lseek(struct file *file, loff_t offset, int orig)
{
	loff_t ret;
	
	if (offset < 0)
		return -EOVERFLOW;

	mutex_lock(&file->f_path.dentry->d_inode->i_mutex);
	switch (orig) {
	case SEEK_CUR:
		offset += file->f_pos;
		break;
	case SEEK_END:
		offset += CIMAX_BLOCK_SIZE;
		break;
	case SEEK_SET:
		break;
	default:
		offset = -EINVAL;
	}

	if (offset >= 0 && offset < CIMAX_BLOCK_SIZE) {
		file->f_pos = offset;
		ret = file->f_pos;
	} else
		ret = -EOVERFLOW;
	mutex_unlock(&file->f_path.dentry->d_inode->i_mutex);

	return ret;
}

static int cimax_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct cimax_memblock *block;
	unsigned int off, pgstart, vsize, psize;
	
	block = file->private_data;
	pgstart = (block->addr >> PAGE_SHIFT) + vma->vm_pgoff;

	off = vma->vm_pgoff << PAGE_SHIFT;
	vsize = vma->vm_end - vma->vm_start;
	psize = CIMAX_BLOCK_SIZE - off;
	if (vsize > psize)
		return -EINVAL; /* end is out of range */
	
	/* io_remap_pfn_range sets the appropriate protection flags */
	if (remap_pfn_range(vma, vma->vm_start, pgstart, vsize,
			    pgprot_noncached(vma->vm_page_prot)))
		return -EAGAIN;
	return 0;
}

static int cimax_open(struct inode *inode, struct file *file)
{
	struct cimax_memblock *block = NULL;
	unsigned int minor, i;

	minor = iminor(inode);
	for (i = 0; i < CIMAX_NUM_BLOCKS; i++)
		if (chip.memblock[i].device.minor == minor) {
			block = &chip.memblock[i];
			break;
		}
	if (!block)
		return -ENXIO;
	
	file->private_data = block;
	
	return 0;
}

static const struct file_operations cimax_fops = {
	.open = cimax_open,
	.mmap = cimax_mmap,
	.llseek = cimax_lseek,
	.read = cimax_read,
	.write = cimax_write,
};


//////////////////// i2c helpers ////////////////////

static int cimax_read_reg(unsigned char reg) {
	unsigned char result, res;
	struct i2c_msg cimax_read_msg[2] = {
		{
			.addr = chip.i2c_addr,
			.flags = 0,
			.len = 1,
			.buf = &reg,
		}, {
			.addr = chip.i2c_addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = &result,
		}
	};
	
	res = i2c_transfer(chip.i2c_adapter, cimax_read_msg, 2);
	if (res != 2) {
		printk(KERN_ERR "cimax: error reading register %02x: %d\n",
				reg, res);
		return -EIO;
	}
	
	return result;
}

static int cimax_write_regs(unsigned char *buffer, int length) {
	int res;
	struct i2c_msg cimax_write_msg = {
		.addr = chip.i2c_addr,
		.flags = 0,
		.len = length,
		.buf = buffer,
	};
	
	res = i2c_transfer(chip.i2c_adapter, &cimax_write_msg, 1);
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

static int cimax_write_reg(unsigned char reg, unsigned char value) {
	unsigned char buffer[2] = { reg, value };
	return cimax_write_regs(buffer, 2);
}

static int cimax_set_timing(void) {
	char timing = CIMAX_TIMING_AM(chip.am_timing_sel)
	            | CIMAX_TIMING_CM(chip.cm_timing_sel);
	return cimax_write_reg(CIMAX_TIMING_A, timing);
}


//////////////////// interrupt handling ////////////////////

static char cimax_irq_mask_data[] = {
	CIMAX_INT_MASK, 0x00
};
static struct i2c_msg cimax_irq_mask_msg = {
	.len = sizeof(cimax_irq_mask_data),
	.buf = cimax_irq_mask_data,
};
static char cimax_irq_reset_data[] = {
	CIMAX_CONTROL, CIMAX_CONTROL_RESET
};
static struct i2c_msg cimax_irq_reset_msg = {
	.len = sizeof(cimax_irq_reset_data),
	.buf = cimax_irq_reset_data,
};
static void cimax_irq_mask(void) {
	int res, retries;
	
	retries = 3;
	do {
		cimax_irq_mask_msg.addr = chip.i2c_addr;
		res = i2c_transfer(chip.i2c_adapter, &cimax_irq_mask_msg, 1);
		if (res == 1)
			return;
		
		printk(KERN_ERR "cimax: cannot mask interrupts: %d\n", res);
		// retry that. sometimes the adapter timeouts for no reason.
		msleep(1);
	} while (--retries);
		
	// normal masking didn't work; try to reset the chip instead. this
	// hoses the driver, but allows the main PCI IRQ to be released and
	// the system to continue working normally.
	retries = 3;
	do {
		cimax_irq_reset_msg.addr = chip.i2c_addr;
		res = i2c_transfer(chip.i2c_adapter, &cimax_irq_reset_msg, 1);
		if (res == 1)
			break;
		printk(KERN_ERR "cimax: cannot reset chip: %d\n", res);
	} while (--retries);
	
	printk(KERN_ERR "cimax: failed to mask interrupts!\n");
}

static void cimax_irq_work(struct work_struct *work)
{
	int status;
	
	// mask the IRQ at chip level and re-enable it globally
	cimax_irq_mask();
	enable_irq(chip.irq);
		
	status = cimax_read_reg(CIMAX_INT_STATUS);
	if (status < 0) {
		printk(KERN_ERR "cimax: error %d reading irq status; "
				"interrupts disabled\n", status);
		return;
	}
	
	if (status & CIMAX_INT_DET_A)
		// restore address space and high address selection. this only
		// works then the interrupt was due to a module insertion, rather
		// than a removal, but it's faster than checking what actually
		// happend.
		cimax_write_reg(CIMAX_MC_A, chip.module_control);
	// reading the register above cleared any pending detection interrupt,
	// so when it is set at this point, someone was really fast with
	// ejecting and re-inserting the module.
	if (status & CIMAX_INT_IRQ_A)
		// IRQ is asserted, so we can only re-enable module detection
		cimax_write_reg(CIMAX_INT_MASK, CIMAX_INT_DET_A);
	else
		// IRQ is clear, so we can re-enable both interrupts
		cimax_write_reg(CIMAX_INT_MASK, CIMAX_INT_DET_A | CIMAX_INT_IRQ_A);
	
	// report IRQ to userspace
	chip.irq_status = status;
	wake_up_interruptible(&chip.irq_queue);
}

static irqreturn_t cimax_isr(int irq, void *dev_id)
{
	if (PNX8550_GPIO_DATA(PNX8550_GPIO_IRQSSTAT_CIMAX))
		// wasn't us; next handler, please!
		return IRQ_NONE;

	// disable the IRQ and schedule work to mask it at the chip level
	schedule_work(&chip.irq_work);
	disable_irq_nosync(chip.irq);
	return IRQ_HANDLED;
}

static ssize_t cimax_int_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	int status;
	
	// blindly unmask the IRQ and wait for it to trigger. wasteful, but
	// effective and, most importantly, free of race conditions.
	chip.irq_status = 0;
	cimax_write_reg(CIMAX_INT_MASK, CIMAX_INT_DET_A | CIMAX_INT_IRQ_A);
	wait_event_interruptible(chip.irq_queue,
			(status = chip.irq_status) != 0);
	
	if (status & CIMAX_INT_DET_A) {
		if (count > 9)
			count = 9;
		copy_to_user(buf, "presence\n", count);
	} else { // CIMAX_INT_IRQ_A, because status != 0
		if (count > 4)
			count = 4;
		copy_to_user(buf, "irq\n", count);
	}
	return count;
}

static int cimax_int_open(struct inode *inode, struct file *file)
{
	if (file->f_flags & O_NONBLOCK)
		return -EINVAL;
	//file->private_data = (void *) chip.last_det;
	// interrupt will be unmasked when we read from the device for the
	// first time, so there is nothing to do here.
	return 0;
}

static int cimax_int_release(struct inode *inode, struct file *file)
{
	// if the interrupt is still enabled, it will be masked when it
	// occurs. no reason to mask it here.
	return 0;
}

static const struct file_operations cimax_int_fops = {
	.open = cimax_int_open,
	.release = cimax_int_release,
	.read = cimax_int_read,
};


//////////////////// sysfs control interface ////////////////////

static ssize_t power_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t size)
{
	int res, len;
	
	len = size;
	if (len > 1 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
		len--;
		
	if (len == 1 && buf[0] == '0')
		chip.power_control = 0;
	else if (len == 1 && buf[0] == '1')
		chip.power_control = CIMAX_POWER_VCC_EN;
	else if (len == 2 && !strncasecmp(buf, "on", 2))
		chip.power_control = CIMAX_POWER_VCC_EN;
	else if (len == 3 && !strncasecmp(buf, "off", 3))
		chip.power_control = 0;
	else
		return -EINVAL;
	
	res = cimax_write_reg(CIMAX_POWER, chip.power_control);
	if (res < 0)
		return res;
	
	return size;
}
static ssize_t power_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	buf[0] = 0;
	if (chip.power_control & CIMAX_POWER_VCC_EN)
		strcpy(buf, "on\n");
	else
		strcpy(buf, "off\n");
	return strlen(buf);
}
static DEVICE_ATTR(power, 0600, power_show, power_store);

static ssize_t reset_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t size)
{
	int res, len;
	
	len = size;
	if (len > 1 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
		len--;
		
	if (len == 1 && buf[0] == '0')
		chip.module_control &= ~CIMAX_MC_RESET;
	else if (len == 1 && buf[0] == '1')
		chip.module_control |= CIMAX_MC_RESET;
	else if (len == 3 && !strncasecmp(buf, "res", 3))
		chip.module_control |= CIMAX_MC_RESET;
	else if (len == 3 && !strncasecmp(buf, "run", 3))
		chip.module_control &= ~CIMAX_MC_RESET;
	else if (len == 5 && !strncasecmp(buf, "reset", 5))
		chip.module_control |= CIMAX_MC_RESET;
	else if (len == 7 && !strncasecmp(buf, "running", 7))
		chip.module_control &= ~CIMAX_MC_RESET;
	else
		return -EINVAL;
	
	res = cimax_write_reg(CIMAX_MC_A, chip.module_control);
	if (res < 0)
		return res;
	
	return size;
}
static ssize_t reset_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	buf[0] = 0;
	if (chip.module_control & CIMAX_MC_RESET)
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
		chip.am_timing_sel = sel;
	else if (attr == &dev_attr_cm_timing)
		chip.cm_timing_sel = sel;
	else
		BUG();
	
	res = cimax_set_timing();
	if (res != 0)
		return res;
	
	return size;
}
static ssize_t timing_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	int i;
	char sel;
	
	if (attr == &dev_attr_am_timing)
		sel = chip.am_timing_sel;
	else if (attr == &dev_attr_cm_timing)
		sel = chip.cm_timing_sel;
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
	chip.module_control = (chip.module_control & ~CIMAX_MC_ACS_MASK) | sel;
	
	res = cimax_write_reg(CIMAX_MC_A, chip.module_control);
	if (res != 0)
		return res;
	
	return size;
}
static ssize_t address_space_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	int i;
	char sel;
	
	sel = chip.module_control & CIMAX_MC_ACS_MASK;
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
	char status;
	
	status = cimax_read_reg(CIMAX_MC_A);
	if (status < 0)
		return status;
	
	buf[0] = 0;
	if (status & CIMAX_MC_DET)
		strcat(buf, "inserted\n");
	else
		strcat(buf, "ejected\n");
	
	return strlen(buf);
}
static DEVICE_ATTR(presence, 0400, presence_show, NULL);


//////////////////// driver initialization and shutdown ////////////////////

static int cimax_devs_create(void)
{
	int res, i;
	
	for (i = 0; i < CIMAX_NUM_BLOCKS; i++) {
		sprintf(chip.memblock[i].devname, "cimax-mem%d", i);
		chip.memblock[i].device.name = chip.memblock[i].devname;
		chip.memblock[i].device.minor = MISC_DYNAMIC_MINOR;
		chip.memblock[i].device.fops = &cimax_fops;
		
		res = misc_register(&chip.memblock[i].device);
		if (res != 0)
			break;
	}
	if (res == 0) {
		chip.irq_device.name = "cimax-interrupt";
		chip.irq_device.minor = MISC_DYNAMIC_MINOR;
		chip.irq_device.fops = &cimax_int_fops;
		res = misc_register(&chip.irq_device);
	}
		
	if (res != 0)
		for (; i >= 0; i--)
			misc_deregister(&chip.memblock[i].device);

	return res;
}

static int cimax_devs_remove(void)
{
	int i;
	
	for (i = CIMAX_NUM_BLOCKS - 1; i >= 0; i--)
		misc_deregister(&chip.memblock[i].device);
	misc_deregister(&chip.irq_device);
	
	return 0;
}

#define CHECK(x) { \
	int res = x; \
	if (res < 0) \
		return res; \
}
static int cimax_setup(void) {
	// reset the chip. if it is locked, that's required to make it
	// configurable again.
	CHECK(cimax_write_reg(CIMAX_CONTROL, CIMAX_CONTROL_RESET));
	
	// configure bus interface (the cimax is connected to the PNX8550 XIO
	// in the obvious way)
	CHECK(cimax_write_reg(CIMAX_POWER, CIMAX_POWER_VCCC_PP
			| CIMAX_POWER_VCCC_AL));
	CHECK(cimax_write_reg(CIMAX_INT_CONFIG, CIMAX_INT_AL | CIMAX_INT_PP));
	CHECK(cimax_write_reg(CIMAX_BUS, CIMAX_BUS_MODE_DIRSTR
			| CIMAX_BUS_CS_AL | CIMAX_BUS_DIR_WR_LOW | CIMAX_BUS_STR_AL));
	CHECK(cimax_write_reg(CIMAX_WAIT, CIMAX_WAIT_MODE_ACK | CIMAX_ACK_AL
			| CIMAX_ACK_PP));
	// lock the chip setup (needed for some registers to be accessible)
	CHECK(cimax_write_reg(CIMAX_CONTROL, CIMAX_CONTROL_LOCK));
	
	// always select module A; there is nothing else connected anyway
	CHECK(cimax_write_reg(CIMAX_DEST, CIMAX_DEST_SEL_MOD_A));

	// set default status: 600ns timing, attribute memory, VCC enabled
	chip.am_timing_sel = CIMAX_TIMING_600NS;
	chip.cm_timing_sel = CIMAX_TIMING_600NS;
	CHECK(cimax_set_timing());
	chip.power_control = CIMAX_POWER_VCC_EN;
	CHECK(cimax_write_reg(CIMAX_POWER, chip.power_control));
	// selecting attribute memory is pointless when there is no module,
	// but checking for a module is slower than just trying
	chip.module_control = CIMAX_MC_HAD | CIMAX_MC_ACS_AM;
	CHECK(cimax_write_reg(CIMAX_MC_A, chip.module_control));
	
	// enable interrupts
	CHECK(cimax_write_reg(CIMAX_INT_MASK, CIMAX_INT_DET_A
			| CIMAX_INT_IRQ_A));
	
	return 0;
}

static void cimax_shutdown(struct platform_device *pdev)
{
	// disable XIO aperture for CIMaX
	PNX8550_XIO_SEL1 = 0;
	
	// don't reset the chip here. it's better to leave it configured
	// properly, so it doesn't mess with the bus.
	// instead, just turn off power to the module and disable interrupts.
	cimax_write_reg(CIMAX_POWER, 0);
	cimax_irq_mask();
}

static int __devexit cimax_remove(struct platform_device *pdev);
static int __devinit cimax_probe(struct platform_device *pdev)
{
	int res, i;
	
	chip.i2c_addr = CIMAX_I2C_ADDR;
	chip.i2c_adapter = i2c_get_adapter(PNX8550_I2C_IP3203_BUS1);
	if (!chip.i2c_adapter) {
		printk(KERN_ERR "cimax: cannot find i2c bus %d\n",
				PNX8550_I2C_IP3203_BUS1);
		return -ENXIO;
	}

	// configure XIO aperture for CIMaX
	PNX8550_XIO_SEL1 = PNX8550_XIO_SEL_ENAB | PNX8550_XIO_SEL_SIZE_64MB
			| PNX8550_XIO_SEL_TYPE_68360 | PNX8550_XIO_SEL_USE_ACK
			| (CIMAX_OFFSET_MB / 8) * PNX8550_XIO_SEL_OFFSET;
	chip.base = PNX8550_BASE18_ADDR + 1024*1024*CIMAX_OFFSET_MB;
	
	for (i = 0; i < CIMAX_NUM_BLOCKS; i++) {
		chip.memblock[i].addr = chip.base + (i << CIMAX_BLOCK_SEL_SHIFT);
		// this trick really only works on MIPS, but that's true for the
		// entire driver
		chip.memblock[i].base = UNCAC_ADDR(phys_to_virt(chip.memblock[i].addr));
	}
	
	// allocate IRQ
	INIT_WORK(&chip.irq_work, cimax_irq_work);
	init_waitqueue_head(&chip.irq_queue);
	if (request_irq(CIMAX_IRQ, cimax_isr, IRQF_SHARED, "cimax", &chip)) {
		cimax_shutdown(pdev);
		printk(KERN_ERR "cimax: cannot allocate irq %d\n", chip.irq);
		return -EBUSY;
	}
	chip.irq = CIMAX_IRQ;
	
	res = cimax_setup();
	if (res < 0) {
		printk(KERN_ERR "cimax: cannot initialize chip\n");
		cimax_shutdown(pdev);
		return -EIO;
	}
	
	#define ADD_SYSFS_FILE(name) \
		res = device_create_file(&pdev->dev, &dev_attr_##name); \
		if (res) { \
			printk(KERN_ERR "CIMaX failed to register sysfs file %s\n", \
					#name); \
			cimax_remove(pdev); \
			return res; \
		}
	ADD_SYSFS_FILE(power);
	ADD_SYSFS_FILE(reset);
	ADD_SYSFS_FILE(am_timing);
	ADD_SYSFS_FILE(cm_timing);
	ADD_SYSFS_FILE(address_space);
	ADD_SYSFS_FILE(presence);
	
	res = cimax_devs_create();
	if (res < 0) {
		printk(KERN_ERR "%s: failed to register devices: %d\n", __func__,
				res);
		cimax_shutdown(pdev);
		return res;
	}

	printk(KERN_INFO "CIMaX at %08x, i2c address %02x, irq %d\n",
			chip.base, chip.i2c_addr, chip.irq);
	return 0;
}

static int __devexit cimax_remove(struct platform_device *pdev)
{
	cimax_shutdown(pdev);

	// release IRQ if necessary
	if (chip.irq) {
		free_irq(chip.irq, &chip);
		// re-enable IRQ, in case that's necessary
		cancel_work_sync(&chip.irq_work);
		enable_irq(chip.irq);
	}

	cimax_devs_remove();
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
