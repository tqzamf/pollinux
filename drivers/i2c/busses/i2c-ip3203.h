/* ------------------------------------------------------------------------- */
/* i2c-algo-IP3203.h i2c driver algorithms for VIPER2           		     */
/* ------------------------------------------------------------------------- */

/* $Id: $ */

#ifndef I2C_ALGO_IP3203_H
#define I2C_ALGO_IP3203_H 1

#include <linux/i2c.h>
#include <asm/io.h>

#ifndef SA_INTERRUPT
#define SA_INTERRUPT		IRQF_DISABLED
#endif

/* Local Macros for IP3203 */
#define READ_IP3203_I2CCON(a)                readl((u32*)((a->offset)+0x000000000))
#define READ_IP3203_I2CSTAT(a)               readl((u32*)((a->offset)+0x000000004))
#define READ_IP3203_I2CDAT(a)                readl((u32*)((a->offset)+0x000000008))
#define READ_IP3203_I2CSLA(a)                readl((u32*)((a->offset)+0x00000000C))
#define READ_IP3203_HSBIR(a)                 readl((u32*)((a->offset)+0x000000010))
#define READ_IP3203_FSBIR(a)                 readl((u32*)((a->offset)+0x000000014))
#define READ_IP3203_INTROG(a)                readl((u32*)((a->offset)+0x000000018))
#define READ_IP3203_DMA_ADDR(a)              readl((u32*)((a->offset)+0x000000020))
#define READ_IP3203_DMA_LENGTH(a)            readl((u32*)((a->offset)+0x000000024))
#define READ_IP3203_DMA_COUNTER(a)           readl((u32*)((a->offset)+0x000000028))
#define READ_IP3203_DMA_CONTROL(a)           readl((u32*)((a->offset)+0x00000002C))
#define READ_IP3203_DMA_STATUS(a)            readl((u32*)((a->offset)+0x000000030))
#define READ_IP3203_INT_STATUS(a)            readl((u32*)((a->offset)+0x000000FE0))
#define READ_IP3203_INT_ENABLE(a)            readl((u32*)((a->offset)+0x000000FE4))
#define READ_IP3203_INT_CLEAR(a)             readl((u32*)((a->offset)+0x000000FE8))
#define READ_IP3203_INT_SET(a)               readl((u32*)((a->offset)+0x000000FEC))
#define READ_IP3203_POWER_DOWN(a)            readl((u32*)((a->offset)+0x000000FF4))
#define READ_IP3203_MODULE_ID(a)             readl((u32*)((a->offset)+0x000000FFC))

#define WRITE_IP3203_I2CCON(a, l)            writel(l, (u32*)((a->offset)+0x000000000))
#define WRITE_IP3203_I2CSTAT(a, l)           writel(l, (u32*)((a->offset)+0x000000004))
#define WRITE_IP3203_I2CDAT(a, l)            writel(l, (u32*)((a->offset)+0x000000008))
#define WRITE_IP3203_I2CSLA(a, l)            writel(l, (u32*)((a->offset)+0x00000000C))
#define WRITE_IP3203_HSBIR(a, l)             writel(l, (u32*)((a->offset)+0x000000010))
#define WRITE_IP3203_FSBIR(a, l)             writel(l, (u32*)((a->offset)+0x000000014))
//#define WRITE_IP3203_INTROG(a, l)            writel(l, (u32*)((a->offset)+0x000000018))
#define WRITE_IP3203_DMA_ADDR(a, l)          writel(l, (u32*)((a->offset)+0x000000020))
#define WRITE_IP3203_DMA_LENGTH(a, l)        writel(l, (u32*)((a->offset)+0x000000024))
#define WRITE_IP3203_DMA_COUNTER(a, l)       writel(l, (u32*)((a->offset)+0x000000028))
#define WRITE_IP3203_DMA_CONTROL(a, l)       writel(l, (u32*)((a->offset)+0x00000002C))
#define WRITE_IP3203_DMA_STATUS(a, l)        writel(l, (u32*)((a->offset)+0x000000030))
#define WRITE_IP3203_INT_STATUS(a, l)        writel(l, (u32*)((a->offset)+0x000000FE0))
#define WRITE_IP3203_INT_ENABLE(a, l)        writel(l, (u32*)((a->offset)+0x000000FE4))
#define WRITE_IP3203_INT_CLEAR(a, l)         writel(l, (u32*)((a->offset)+0x000000FE8))
#define WRITE_IP3203_INT_SET(a, l)           writel(l, (u32*)((a->offset)+0x000000FEC))
#define WRITE_IP3203_POWER_DOWN(a, l)        writel(l, (u32*)((a->offset)+0x000000FF4))
#define WRITE_IP3203_MODULE_ID(a, l)         writel(l, (u32*)((a->offset)+0x000000FFC))

#define IP3203_EN                       (0x40) /* Bit in the register I2CCON     */
#define IP3203_STA                      (0x20) /* Bit in the register I2CCON     */
#define IP3203_SETSTO                   (0x10) /* Bit in the register I2CCON     */
#define IP3203_AA                       (0x04) /* Bit in the register I2CCON     */
#define IP3203_INTBIT                   (0x01) /* Bit in the register INT_STATUS */
#define IP3203_INTRO_MST                (0x80) /* Bit in the register INTROG     */ 
#define IP3203_INTRO_TRX                (0x40) /* Bit in the register INTROG     */ 
#define IP3203_INTRO_HS                 (0x20) /* Bit in the register INTROG     */ 
#define IP3203_INTRO_STO                (0x10) /* Bit in the register INTROG     */ 
#define IP3203_INTRO_SI                 (0x08) /* Bit in the register INTROG     */ 
#define IP3203_INTRO_BB                 (0x04) /* Bit in the register INTROG     */ 
#define IP3203_INTRO_SEL                (0x02) /* Bit in the register INTROG     */ 
#define IP3203_INTRO_ACK                (0x01) /* Bit in the register INTROG     */ 


struct i2c_algo_IP3203_data {
    void * baseAddress;
};

#define TMHW_I2C_MAX_SS_SPEED  100  // kHz
#define TMHW_I2C_MAX_FS_SPEED  400  // kHz
#define TMHW_I2C_MAX_HS_SPEED 3400  // kHz

union _i2cHSBIR
{
    struct
    {
        unsigned int hsBitRate : 5;
        unsigned int : 27;
    } bits;
    unsigned int u32;
} IP3203_i2cHSBIR;

union _i2cFSBIR
{
    struct
    {
        unsigned int fsBitRate : 7;
        unsigned int SFMode : 1;
#define I2C_SS_MODE                               0
#define I2C_FS_MODE                               1
        unsigned int : 24;
    } bits;
    unsigned int u32;
} IP3203_i2cFSBIR;

#endif /* I2C_ALGO_IP3203_H */
