#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>

static struct i2c_board_info pcf8563_i2c_device[] __initconst = {
	{
		I2C_BOARD_INFO("pcf8563", 0x51) /* RTC */
	},
};

static int __init pnx8550_i2c_init(void)
{
        int err;
        printk("pnx8550: Registering I2C Devices\n");
        err = i2c_register_board_info(3, pcf8563_i2c_device, ARRAY_SIZE(pcf8563_i2c_device));
        if (err < 0)
                printk(KERN_ERR "pnx8550: cannot register board I2C devices\n");
        return err;
}

arch_initcall(pnx8550_i2c_init);
