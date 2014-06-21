/*
 * CIMaX driver for PNX8550 STB810 board.
 * 
 * GPLv2.
 */

#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>

#include <pci.h>
#include <xio.h>
#include <cimax.h>
#include <i2c.h>

//static char cimax_irq_status_regsel = {
	//CIMAX_INT_STATUS
//};
//static unsigned char cimax_irq_status_buf;
//static struct i2c_msg cimax_irq_status_msg[] = {
	//{
		//.addr = CIMAX_I2C_ADDR,
		//.len = 1,
		//.buf = &cimax_irq_status_regsel,
	//}, {
		//.addr = CIMAX_I2C_ADDR,
		//.flags = I2C_M_RD,
		//.len = 1,
		//.buf = &cimax_irq_status_buf,
	//}
//};
//static int cimax_irq_status(void) {
	//struct i2c_adapter *adapter;
	//int res;
	
	//adapter = i2c_get_adapter(PNX8550_I2C_IP3203_BUS1);
	//res = i2c_transfer(adapter, cimax_irq_status_msg,
			//ARRAY_SIZE(cimax_irq_status_msg));
	//if (res != ARRAY_SIZE(cimax_irq_status_msg)) {
		//printk(KERN_ERR "%s: i2c write error %d\n", __func__, res);
		//return -1;
	//}
	
	//return cimax_irq_status_buf;
//}

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
	int res, cimax_base;
	
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
	
	cimax_base = PNX8550_BASE18_ADDR + 8*1024*1024*CIMAX_BASE_8MB;
	printk(KERN_INFO "CIMaX at %08x, i2c address %02x\n", cimax_base,
			CIMAX_I2C_ADDR);
	
	// TODO register /dev/cimax
	
	return 0;
}

static int __devexit pnx8550_cimax_remove(struct platform_device *pdev)
{
	// TODO remove device
	
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
