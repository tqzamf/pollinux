/*
 * Non-driver to power down all unused peripherals.
 * 
 * Public domain.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <framebuffer.h>
#include <dcsn.h>
#include <i2c.h>
#include <cm.h>

#define I2C_ANABEL_BUS                 PNX8550_I2C_IP3203_BUS1
#define I2C_ANABEL_VIDEO2_ADDR         0x6e
#define I2C_ANABEL_AUDIO1_ADDR         0x76
#define I2C_ANABEL_AUDIO2_ADDR         0x7e

// sets a single register over I²C
static void pnx8550_anabel_set_reg(unsigned char addr, unsigned char reg,
		unsigned char value)
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

static int __init pnx8550_powerdown_unused(void)
{
#define POWERDOWN(modname) \
	PNX8550_DCSN_POWERDOWN_CTL(PNX8550_ ## modname ## _BASE) \
			= PNX8550_DCSN_POWERDOWN_CMD; \
	PNX8550_CM_ ## modname ## _CTL = 0;
	
	// smart card 2 and internal OHCI. both are unused because they aren't
	// connected anywhere, but probably don't use much power anyway.
	POWERDOWN(SC2);
	POWERDOWN(OHCI);
	
	// MPEG routing and decoding hardware. unused without tuners.
	POWERDOWN(VMPG);
	POWERDOWN(VLD);
	POWERDOWN(MSP1);
	POWERDOWN(MSP2);
	POWERDOWN(TSDMA);
	// (analog) video input hardware. the board never had analog video in
	// the first place, so these have always been unused.
	POWERDOWN(VIP1);
	POWERDOWN(VIP2);
	// memory-based scalers. not needed when there is no video decoding.
	POWERDOWN(MBS1);
	POWERDOWN(MBS2);
	POWERDOWN(MBS3);

	// the DVD CSS module. undocumented to prevent unauthorized use, and thus
	// useless.
	// the CSS cryptographic break is probably documented significantly better
	// than this module...
	POWERDOWN(DVDCSS);

    // shutdown PNX8510 second video channel, including clocks
    pnx8550_anabel_set_reg(I2C_ANABEL_VIDEO2_ADDR, 0xa5, 0x01);
    pnx8550_anabel_set_reg(I2C_ANABEL_AUDIO2_ADDR, 0x01, 0x01);
    pnx8550_anabel_set_reg(I2C_ANABEL_AUDIO2_ADDR, 0x02, 0x01);
    // shutdown both PNX8510 audio channels
    pnx8550_anabel_set_reg(I2C_ANABEL_AUDIO1_ADDR, 0xfe, 0x00);
    pnx8550_anabel_set_reg(I2C_ANABEL_AUDIO2_ADDR, 0xfe, 0x00);

    // Make sure the QVCP's output clock is enabled and available at the
    // DACs. If it isn't, any access to the DACs will hang the system,
    // regardless of any configured bus timeouts!
    PNX8550_CM_QVCP2_OUT_CTL = PNX8550_CM_CLK_ENABLE | PNX8550_CM_TM_CLK_PLL;
    udelay(300);

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

    return 0;
}
module_init(pnx8550_powerdown_unused);
