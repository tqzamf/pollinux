/* ------------------------------------------------------------------------- */
/* i2c-algo-IP3203.h i2c driver algorithms for VIPER2           		     */
/* ------------------------------------------------------------------------- */

/* $Id: $ */

#ifndef I2C_ALGO_IP3203_H
#define I2C_ALGO_IP3203_H 1

#include <linux/i2c.h>
#include <asm/io.h>

#ifndef SA_INTERRUPT
#define SA_INTERRUPT            IRQF_DISABLED
#endif


/* Local Macros for IP0105 */
#define READ_IP0105_I2C_CONTROL(a)           readl((u32*)((a->offset)+0x000000000))
#define READ_IP0105_I2C_DAT(a)               readl((u32*)((a->offset)+0x000000004))
#define READ_IP0105_I2C_STATUS(a)            readl((u32*)((a->offset)+0x000000008))
#define READ_IP0105_I2C_ADDRESS(a)           readl((u32*)((a->offset)+0x00000000C))
#define READ_IP0105_I2C_STOP(a)              readl((u32*)((a->offset)+0x000000010))
#define READ_IP0105_I2C_PD(a)                readl((u32*)((a->offset)+0x000000014))
#define READ_IP0105_I2C_SET_PINS(a)          readl((u32*)((a->offset)+0x000000018))
#define READ_IP0105_I2C_OBS_PINS(a)          readl((u32*)((a->offset)+0x00000001C))
#define READ_IP0105_I2C_INT_STATUS(a)        readl((u32*)((a->offset)+0x000000FE0))
#define READ_IP0105_I2C_INT_EN(a)            readl((u32*)((a->offset)+0x000000FE4))
#define READ_IP0105_I2C_INT_CLR(a)           readl((u32*)((a->offset)+0x000000FE8))
#define READ_IP0105_I2C_INT_SET(a)           readl((u32*)((a->offset)+0x000000FEC))
#define READ_IP0105_I2C_POWERDOWN(a)         readl((u32*)((a->offset)+0x000000FF4))
#define READ_IP0105_MODULE_ID(a)             readl((u32*)((a->offset)+0x000000FFC))

#define WRITE_IP0105_I2C_CONTROL(a, l)            writel(l,(u32*)((a->offset)+0x000000000))
#define WRITE_IP0105_I2C_DAT(a, l)                writel(l,(u32*)((a->offset)+0x000000004))
#define WRITE_IP0105_I2C_STATUS(a, l)             writel(l,(u32*)((a->offset)+0x000000008))
#define WRITE_IP0105_I2C_ADDRESS(a, l)            writel(l,(u32*)((a->offset)+0x00000000C))
#define WRITE_IP0105_I2C_STOP(a, l)               writel(l,(u32*)((a->offset)+0x000000010))
#define WRITE_IP0105_I2C_PD(a, l)                 writel(l,(u32*)((a->offset)+0x000000014))
#define WRITE_IP0105_I2C_SET_PINS(a, l)           writel(l,(u32*)((a->offset)+0x000000018))
#define WRITE_IP0105_I2C_OBS_PINS(a, l)           writel(l,(u32*)((a->offset)+0x00000001C))
#define WRITE_IP0105_I2C_INT_STATUS(a, l)         writel(l,(u32*)((a->offset)+0x000000FE0))
#define WRITE_IP0105_I2C_INT_EN(a, l)             writel(l,(u32*)((a->offset)+0x000000FE4))
#define WRITE_IP0105_I2C_INT_CLR(a, l)            writel(l,(u32*)((a->offset)+0x000000FE8))
#define WRITE_IP0105_I2C_INT_SET(a, l)            writel(l,(u32*)((a->offset)+0x000000FEC))
#define WRITE_IP0105_I2C_POWERDOWN(a, l)          writel(l,(u32*)((a->offset)+0x000000FF4))
#define WRITE_IP0105_MODULE_ID(a, l)              writel(l,(u32*)((a->offset)+0x000000FFC))


#define IP0105_AA                       (0x80) /* Bit in the register I2CCON     */
#define IP0105_EN                       (0x40) /* Bit in the register I2CCON     */
#define IP0105_STA                      (0x20) /* Bit in the register I2CCON     */
#define IP0105_STO                      (0x10) /* Bit in the register I2CCON     */
#define IP0105_CR2                      (0x03) /* Bits in the register I2CCON     */
#define IP0105_INTBIT                   (0x01) /* Bit in the register INT_STATUS */


struct i2c_algo_IP0105_data {
    void * baseAddress;
};

#define TMHW_I2C_MAX_SS_SPEED  100  // kHz
#define TMHW_I2C_MAX_FS_SPEED  400  // kHz
#define TMHW_I2C_MAX_HS_SPEED 3400  // kHz



#endif /* I2C_ALGO_IP3203_H */
