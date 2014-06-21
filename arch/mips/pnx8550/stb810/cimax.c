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

#include <pci.h>
#include <xio.h>
#include <cimax.h>
#include <i2c.h>

static struct miscdevice cimax_devices[CIMAX_NUM_BLOCKS];
static int cimax_base;

static ssize_t cimax_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	unsigned int rem, block, base, off;
	char *ptr;

	// block is selected by A24/25
	block = (unsigned int) file->private_data;
	if (block >= CIMAX_NUM_BLOCKS)
		return -ENXIO;
	base = cimax_base + (block << CIMAX_BLOCK_SEL_SHIFT);
	off = *ppos;

	if (off >= CIMAX_BLOCK_SIZE || count >= CIMAX_BLOCK_SIZE
			|| off + count >= CIMAX_BLOCK_SIZE)
		return -EFAULT;

	// this really only works on MIPS, but that's true for the entire driver
	ptr = UNCAC_ADDR(phys_to_virt(base + off));
	rem = copy_to_user(buf, ptr, count);
	if (rem)
		return -EFAULT;

	*ppos += count;
	return count;
}

static ssize_t cimax_write(struct file *file, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	unsigned int rem, block, base, off;
	char *ptr;

	// block is selected by A24/25
	block = (unsigned int) file->private_data;
	if (block >= CIMAX_NUM_BLOCKS)
		return -ENXIO;
	base = cimax_base + (block << CIMAX_BLOCK_SEL_SHIFT);
	off = *ppos;

	if (off >= CIMAX_BLOCK_SIZE || count >= CIMAX_BLOCK_SIZE
			|| off + count >= CIMAX_BLOCK_SIZE)
		return -EFAULT;

	// this really only works on MIPS, but that's true for the entire driver
	ptr = UNCAC_ADDR(phys_to_virt(base + off));
	rem = copy_from_user(ptr, buf, count);
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
	unsigned int off, base, pgstart, vsize, psize, block;
	
	// block is selected by A24/25
	block = (unsigned int) file->private_data;
	if (block >= CIMAX_NUM_BLOCKS)
		return -ENXIO;
	base = cimax_base + (block << CIMAX_BLOCK_SEL_SHIFT);
	
	off = vma->vm_pgoff << PAGE_SHIFT;
	pgstart = (base >> PAGE_SHIFT) + vma->vm_pgoff;
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
	unsigned int minor, i, block;

	minor = iminor(inode);
	for (i = 0; i < CIMAX_NUM_BLOCKS; i++)
		if (cimax_devices[i].minor == minor) {
			block = i;
			break;
		}
	if (i == CIMAX_NUM_BLOCKS)
		return -ENXIO;
	
	file->private_data = (void*) block;
	
	return 0;
}

static const struct file_operations cimax_fops = {
	.open = cimax_open,
	.mmap = cimax_mmap,
	.llseek = cimax_lseek,
	.read = cimax_read,
	.write = cimax_write,
};

static struct miscdevice cimax_devices[CIMAX_NUM_BLOCKS] = {
	{ .minor = MISC_DYNAMIC_MINOR, .name = "cimax-mem0", .fops = &cimax_fops },
	{ .minor = MISC_DYNAMIC_MINOR, .name = "cimax-mem1", .fops = &cimax_fops },
	{ .minor = MISC_DYNAMIC_MINOR, .name = "cimax-mem2", .fops = &cimax_fops },
	{ .minor = MISC_DYNAMIC_MINOR, .name = "cimax-mem3", .fops = &cimax_fops },
};

static int cimax_devs_create(void)
{
	int res, i;
	
	for (i = 0; i < CIMAX_NUM_BLOCKS; i++) {
		res = misc_register(&cimax_devices[i]);
		if (res < 0)
			break;
	}
	
	if (res < 0)
		for (; i >= 0; i--)
			misc_deregister(&cimax_devices[i]);

	return res;
}

static int cimax_devs_remove(void)
{
	int i;
	
	for (i = CIMAX_NUM_BLOCKS - 1; i >= 0; i--)
		misc_deregister(&cimax_devices[i]);
	
	return 0;
}

static int cimax_send_control(char *msg);
static char cimax_module_control[];

static char cimax_irq_status_regsel = {
	CIMAX_INT_STATUS
};
static unsigned char cimax_irq_status_buf;
static struct i2c_msg cimax_irq_status_msg[] = {
	{
		.addr = CIMAX_I2C_ADDR,
		.len = 1,
		.buf = &cimax_irq_status_regsel,
	}, {
		.addr = CIMAX_I2C_ADDR,
		.flags = I2C_M_RD,
		.len = 1,
		.buf = &cimax_irq_status_buf,
	}
};
static int cimax_irq_status(void) {
	struct i2c_adapter *adapter;
	int res;
	
	adapter = i2c_get_adapter(PNX8550_I2C_IP3203_BUS1);
	res = i2c_transfer(adapter, cimax_irq_status_msg,
			ARRAY_SIZE(cimax_irq_status_msg));
	if (res != ARRAY_SIZE(cimax_irq_status_msg)) {
		printk(KERN_ERR "%s: i2c read error %d\n", __func__, res);
		return -1;
	}
	
	if (cimax_irq_status_buf & CIMAX_INT_DET_A) {
		res = cimax_send_control(cimax_module_control);
		if (res < 0)
			printk(KERN_ERR "%s: i2c write error %d\n", __func__, res);
	}
	
	return cimax_irq_status_buf;
}

static char cimax_presence_status_regsel = {
	CIMAX_MC_A
};
static unsigned char cimax_presence_status_buf;
static struct i2c_msg cimax_presence_status_msg[] = {
	{
		.addr = CIMAX_I2C_ADDR,
		.len = 1,
		.buf = &cimax_presence_status_regsel,
	}, {
		.addr = CIMAX_I2C_ADDR,
		.flags = I2C_M_RD,
		.len = 1,
		.buf = &cimax_presence_status_buf,
	}
};
static int cimax_presence_status(void) {
	struct i2c_adapter *adapter;
	int res;
	
	adapter = i2c_get_adapter(PNX8550_I2C_IP3203_BUS1);
	res = i2c_transfer(adapter, cimax_presence_status_msg,
			ARRAY_SIZE(cimax_presence_status_msg));
	if (res != ARRAY_SIZE(cimax_presence_status_msg)) {
		printk(KERN_ERR "%s: i2c read error %d\n", __func__, res);
		return -1;
	}
	
	res = cimax_send_control(cimax_module_control);
	if (res < 0)
		printk(KERN_ERR "%s: i2c write error %d\n", __func__, res);
	
	return cimax_presence_status_buf;
}

//static char[] cimax_irq_mask_data = {
	//CIMAX_INT_MASK, 0x00
//};
//static char[] cimax_irq_unmask_data = {
	//CIMAX_INT_MASK, CIMAX_INT_DET_A | CIMAX_INT_IRQ_A
//};
//static struct i2c_msg cimax_irq_mask_msg = {
 	//.addr = CIMAX_I2C_ADDR,
	//.len = sizeof(cimax_irq_mask_data),
	//.buf = cimax_irq_mask_data,
//};
//static struct i2c_msg cimax_irq_unmask_msg = {
 	//.addr = CIMAX_I2C_ADDR,
	//.len = sizeof(cimax_irq_unmask_data),
	//.buf = cimax_irq_unmask_data,
//};
//static int cimax_irq_mask(void) {
	//struct i2c_adapter *adapter;
	//int res;
	
	//adapter = i2c_get_adapter(PNX8550_I2C_IP3203_BUS1);
	//res = i2c_transfer(adapter, &cimax_irq_mask_msg, 1);
	//if (res != 1)
		//printk(KERN_ERR "%s: i2c write error %d\n", __func__, res);
	
	//return res;
//}
//static int cimax_irq_unmask(void) {
	//struct i2c_adapter *adapter;
	//int res;
	
	//adapter = i2c_get_adapter(PNX8550_I2C_IP3203_BUS1);
	//res = i2c_transfer(adapter, &cimax_irq_unmask_msg, 1);
	//if (res != 1)
		//printk(KERN_ERR "%s: i2c write error %d\n", __func__, res);
	
	//return res;
//}

static char cimax_power_control[] = {
	CIMAX_POWER, CIMAX_POWER_VCC_EN
};
static char cimax_timing_control[] = {
	CIMAX_TIMING_A, CIMAX_TIMING_AM(CIMAX_TIMING_600NS) 
	              | CIMAX_TIMING_CM(CIMAX_TIMING_600NS)
};
static char cimax_module_control[] = {
	CIMAX_MC_A, CIMAX_MC_HAD | CIMAX_MC_ACS_AM
};
static struct i2c_msg cimax_msg = {
 	.addr = CIMAX_I2C_ADDR,
	.len = 2,
};
static int cimax_send_control(char *msg) {
	struct i2c_adapter *adapter;
	int res;
	
	adapter = i2c_get_adapter(PNX8550_I2C_IP3203_BUS1);
	cimax_msg.buf = msg;
	res = i2c_transfer(adapter, &cimax_msg, 1);
	if (res != 1) {
		printk(KERN_ERR "%s: i2c write error %d\n", __func__, res);
		return res;
	}
	
	return 0;
}

static char power_status = 1;
static ssize_t power_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t size)
{
	int res, len;
	
	len = size;
	if (len > 1 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
		len--;
		
	if (len == 1 && buf[0] == '0')
		power_status = 0;
	else if (len == 1 && buf[0] == '1')
		power_status = 1;
	else if (len == 2 && !strncasecmp(buf, "on", 2))
		power_status = 1;
	else if (len == 3 && !strncasecmp(buf, "off", 3))
		power_status = 0;
	else
		return -EINVAL;
	
	cimax_power_control[1] = power_status ? CIMAX_POWER_VCC_EN : 0;
	res = cimax_send_control(cimax_power_control);
	if (res != 0)
		return res;
	
	return size;
}
static ssize_t power_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	buf[0] = 0;
	if (power_status)
		strcpy(buf, "on\n");
	else
		strcpy(buf, "off\n");
	return strlen(buf);
}
static DEVICE_ATTR(power, 0600, power_show, power_store);

static struct cimax_timing {
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
static char am_timing_sel = CIMAX_TIMING_600NS;
static char cm_timing_sel = CIMAX_TIMING_600NS;
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
		am_timing_sel = sel;
	else if (attr == &dev_attr_cm_timing)
		cm_timing_sel = sel;
	else
		BUG();
	
	cimax_timing_control[1] = CIMAX_TIMING_AM(am_timing_sel)
	                        | CIMAX_TIMING_CM(cm_timing_sel);
	res = cimax_send_control(cimax_timing_control);
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
		sel = am_timing_sel;
	else if (attr == &dev_attr_cm_timing)
		sel = cm_timing_sel;
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
static int address_space_sel = CIMAX_MC_ACS_AM;
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
	address_space_sel = sel;
	
	cimax_module_control[1] = CIMAX_MC_HAD | address_space_sel;
	res = cimax_send_control(cimax_module_control);
	if (res != 0)
		return res;
	
	return size;
}
static ssize_t address_space_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	int i;
	
	buf[0] = 0;
	for (i = 0; i < ARRAY_SIZE(cimax_address_spaces); i++) {
		if (i != 0)
			strcat(buf, " ");
		if (cimax_address_spaces[i].sel == address_space_sel)
			strcat(buf, "[");
		strcat(buf, cimax_address_spaces[i].name);
		if (cimax_address_spaces[i].sel == address_space_sel)
			strcat(buf, "]");
	}
	strcat(buf, "\n");
	
	return strlen(buf);
}
static DEVICE_ATTR(address_space, 0600, address_space_show, address_space_store);

static ssize_t interrupt_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	char status;
	
	status = cimax_irq_status();
	if (status < 0)
		return status;
	
	buf[0] = 0;
	if (status & CIMAX_INT_IRQ_A)
		strcat(buf, "irq ");
	if (status & CIMAX_INT_DET_A)
		strcat(buf, "presence ");
	if (*buf) // strip the extra space
		buf[strlen(buf) - 1] = 0;
	strcat(buf, "\n");
	
	return strlen(buf);
}
static DEVICE_ATTR(interrupt, 0400, interrupt_show, NULL);

static ssize_t presence_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	char status;
	
	status = cimax_presence_status();
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

/* chip initialization commands */
static char cimax_setup_modsel_power[] = {
	CIMAX_DEST, CIMAX_DEST_SEL_MOD_A,
	/* CIMAX_POWER */ CIMAX_POWER_VCCC_PP | CIMAX_POWER_VCCC_AL
};
static char cimax_setup_hostintf[] = {
	CIMAX_INT_CONFIG, CIMAX_INT_AL | CIMAX_INT_PP,
	/* CIMAX_BUS */ CIMAX_BUS_CS_AL | CIMAX_BUS_MODE_DIRSTR
	              | CIMAX_BUS_DIR_WR_LOW | CIMAX_BUS_STR_AL,
	/* CIMAX_WAIT */ CIMAX_WAIT_MODE_ACK | CIMAX_ACK_AL | CIMAX_ACK_PP,
	/* CIMAX_CONTROL */ CIMAX_CONTROL_LOCK
};
static struct i2c_msg cimax_setup_modsel_power_msg = {
	.addr = CIMAX_I2C_ADDR,
	.len = sizeof(cimax_setup_modsel_power),
	.buf = cimax_setup_modsel_power,
};
static struct i2c_msg cimax_setup_hostintf_msg = {
	.addr = CIMAX_I2C_ADDR,
	.len = sizeof(cimax_setup_hostintf),
	.buf = cimax_setup_hostintf,
};
static char cimax_shutdown_reset[] = {
	CIMAX_CONTROL, CIMAX_CONTROL_RESET
};
static struct i2c_msg cimax_shutdown_reset_msg = {
	.addr = CIMAX_I2C_ADDR,
	.len = sizeof(cimax_shutdown_reset),
	.buf = cimax_shutdown_reset,
};

static void pnx8550_cimax_shutdown(struct platform_device *pdev)
{
	struct i2c_adapter *adapter;
	int res;
	
	PNX8550_XIO_SEL1 = 0;

	adapter = i2c_get_adapter(PNX8550_I2C_IP3203_BUS1);
	res = i2c_transfer(adapter, &cimax_shutdown_reset_msg, 1);
	if (res != 1)
		printk(KERN_ERR "%s: i2c write error %d\n", __func__, res);
}

static int __devexit pnx8550_cimax_remove(struct platform_device *pdev);
static int __devinit pnx8550_cimax_probe(struct platform_device *pdev)
{
	struct i2c_adapter *adapter;
	int res;
	
	PNX8550_XIO_SEL1 = PNX8550_XIO_SEL_ENAB | PNX8550_XIO_SEL_SIZE_64MB
			| PNX8550_XIO_SEL_TYPE_68360 | PNX8550_XIO_SEL_USE_ACK
			| CIMAX_BASE_8MB * PNX8550_XIO_SEL_OFFSET;
	
	adapter = i2c_get_adapter(PNX8550_I2C_IP3203_BUS1);
	#define I2CCMD(x) \
		res = i2c_transfer(adapter, &x, 1); \
		if (res != 1) { \
			printk(KERN_ERR "%s: i2c error %d writing %s\n", \
					__func__, res, #x); \
			pnx8550_cimax_remove(pdev); \
			return res; \
		}
	I2CCMD(cimax_shutdown_reset_msg);
	I2CCMD(cimax_setup_modsel_power_msg);
	I2CCMD(cimax_setup_hostintf_msg);
	cimax_send_control(cimax_power_control);
	cimax_send_control(cimax_timing_control);
	cimax_send_control(cimax_module_control);
	
	#define ADD_SYSFS_FILE(name) \
		res = device_create_file(&pdev->dev, &dev_attr_##name); \
		if (res) { \
			printk(KERN_ERR "CIMaX failed to register sysfs file %s\n", \
					#name); \
			pnx8550_cimax_remove(pdev); \
			return res; \
		}
	ADD_SYSFS_FILE(power);
	ADD_SYSFS_FILE(am_timing);
	ADD_SYSFS_FILE(cm_timing);
	ADD_SYSFS_FILE(address_space);
	ADD_SYSFS_FILE(presence);
	ADD_SYSFS_FILE(interrupt);
	
	cimax_base = PNX8550_BASE18_ADDR + 8*1024*1024*CIMAX_BASE_8MB;
	res = cimax_devs_create();
	if (res < 0) {
		printk(KERN_ERR "%s: failed to register devices: %d\n", __func__,
				res);
		return res;
	}

	printk(KERN_INFO "CIMaX at %08x, i2c address %02x\n", cimax_base,
			CIMAX_I2C_ADDR);
	return 0;
}

static int __devexit pnx8550_cimax_remove(struct platform_device *pdev)
{
	cimax_devs_remove();
	pnx8550_cimax_shutdown(pdev);
	
	return 0;
}
static struct platform_driver pnx8550_cimax_driver = {
	.probe		= pnx8550_cimax_probe,
	.remove		= __devexit_p(pnx8550_cimax_remove),
	.shutdown   = pnx8550_cimax_shutdown,
	.driver		= {
		.name	= "cimax",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(pnx8550_cimax_driver);

MODULE_DESCRIPTION("PNX8550 CIMaX driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:cimax");
