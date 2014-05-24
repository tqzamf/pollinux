#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/mach-pnx8550/i2c.h>

static struct i2c_board_info ip0105_bus1_devices[] __initconst = {
	{
		I2C_BOARD_INFO("pcf8563", 0x51) /* RTC */
	},
};

static int __init pnx8550_i2c_init(void)
{
        int err;
        err = i2c_register_board_info(PNX8550_I2C_IP0105_BUS1,
				ip0105_bus1_devices, ARRAY_SIZE(ip0105_bus1_devices));
        if (err < 0)
                printk(KERN_ERR "pnx8550: cannot register IP0105 bus 1 I2C devices\n");
        return err;
}

arch_initcall(pnx8550_i2c_init);
