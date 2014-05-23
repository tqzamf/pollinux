/* -------------------------------------------------------------------------- */
/* i2c-ip3203.c i2c driver algorithms for PNX8550 High Performance ports */
/* -------------------------------------------------------------------------- */

/* This module is the work of Willem van Beek,
 * evidently inspired by Simon G. Vogl.
 *
 * Restriction :
 * Slave Transmitter functionality not working (yet)
 * Slave Receiver doesn't use DMA functionality
 * No time-out (or watchdog) on Slave functionality
 *
 *
Rev Date        Author        Comments
--------------------------------------------------------------------------------
001             M Neill       Initial, based on ?
....
007 20051122    raiyat        Linux 2.6.14.2 changes
009 20060908    laird         Linux 2.6.17.7 changes
--------------------------------------------------------------------------------
 */



/*
-------------------------------------------------------------------------------
Standard include files:
-------------------------------------------------------------------------------
*/
#define DEBUG
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/mm.h>

/*
-------------------------------------------------------------------------------
Project include files:
-------------------------------------------------------------------------------
*/
#include <linux/i2c.h>
#include "i2c-ip3203.h"
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/mach-pnx8550/int.h>
#include <asm/io.h>

#ifndef FALSE
#define FALSE 0
#define TRUE  1
#endif

#define errids_Ok 0
#define errids_IsI2cHardwareError 0x01
#define errids_IsI2cDeviceError   0x02
#define errids_IsI2cWriteError    0x03

#define evMasterReady               0x0001
#define evSlaveRxIntermediateNtf    0x0002
#define evSlaveReceiverReady        0x0004
#define evSlaveReadRequest          0x0008
#define evSlaveTransmitterReady     0x0010
#define evSlaveStopCondition        0x0020

#ifndef BOOL
#define BOOL       int
#endif

/* --- adapters                                        */
#define NR_I2C_DEVICES 2 /* Two High Performance controllers : Don't change this value */

#define I2C_HIGH_PERFORMANCE_PORT_1 0x00 /* IP3203 Controller on the Viper */
#define I2C_HIGH_PERFORMANCE_PORT_2 0x01 /* IP3203 Controller on the Viper */
#define IP3203_UNIT0                0xBBE45000
#define IP3203_UNIT1                0xBBE46000

#define MODULE_CLOCK                27000 /* I2C module clock speed in KHz */

#define I2CADDRESS( address )       ((address) & 0x00FE) /* only 7 bits I2C address implemented */
#define TIMER_OFF                   0

//#define USE_DMA

typedef enum
{
    Idle = 0x00,
    MasterTransmitter = 0x01,
    MasterReceiver = 0x02,
    SlaveReceiver = 0x04,
    SlaveTransmitter = 0x08,
} I2cMode;

struct I2cBusObject
{
    unsigned char  *  mst_txbuf; /* Master mode variables */
    int               mst_txbuflen;
    int               mst_txbufindex;
    unsigned char  *  mst_rxbuf;
    int               mst_rxbuflen;
    int               mst_rxbufindex;
    int               mst_address;
    wait_queue_head_t iic_wait_master;
    unsigned long     int_pin;
    unsigned int           mst_status;
//    int               mst_timer;
//    int               mst_timeout;
//    BOOL              mst_timeout_valid;

    unsigned char  *  slv_buf; /* Slave mode variables */
    int               slv_bufsize;
    int               slv_buflen;
    int               slv_bufindex;
    BOOL              slv_enabled;
//    int               slv_timer;
//    int               slv_timeout;
//    BOOL              slv_timeout_valid;

    int               offset; /* I2C HW controller address offset, required by HAL */
    I2cMode           mode;
    int               isr_event;
//    BOOL    bus_blocked;
#ifdef USE_DMA
    unsigned char  *  dma_bufptr;
    unsigned char  *  dma_clientbuf;
    int               dma_len;
    I2cMode           dma_transmit;
#endif
    struct  fasync_struct ** slv_usr_notify;
};

/* Local Macros for IP3203 */
static __inline void STAOUT(struct I2cBusObject * a, int p)
{
        int val = READ_IP3203_I2CCON(a);
        if (p) { val |= IP3203_STA; } else { val &= ~IP3203_STA; }
        WRITE_IP3203_I2CCON(a, val);
}
static __inline void STOOUT(struct I2cBusObject * a, int p)
{
        int val = READ_IP3203_I2CCON(a);
        if (p) { val |= IP3203_SETSTO; } else { val &= ~IP3203_SETSTO; }
        WRITE_IP3203_I2CCON(a, val);
}
static __inline void AAOUT(struct I2cBusObject * a, int p)
{
        int val = READ_IP3203_I2CCON(a);
        if (p) { val |= IP3203_AA; } else { val &= ~IP3203_AA; }
        WRITE_IP3203_I2CCON(a, val);
}
static __inline void ENABLE_I2C_CONTROLLER(struct I2cBusObject * a)
{
        int val = READ_IP3203_I2CCON(a);

        val |= IP3203_EN;
        WRITE_IP3203_I2CCON(a, val);
}
static __inline void DISABLE_I2C_CONTROLLER(struct I2cBusObject * a)
{
        int val = READ_IP3203_I2CCON(a);

        val &= ~IP3203_EN;
        WRITE_IP3203_I2CCON(a, val);
}
static __inline void CLEAR_I2C_INTERRUPT(struct I2cBusObject * a)
{
        WRITE_IP3203_INT_CLEAR(a, 1);
}
static __inline void ENABLE_I2C_INTERRUPT(struct I2cBusObject * a)
{
        WRITE_IP3203_INT_ENABLE(a, IP3203_INTBIT);
}
static __inline void DISABLE_I2C_INTERRUPT(struct I2cBusObject * a)
{
        WRITE_IP3203_INT_ENABLE(a, 0);
}

static int i2c_debug = 7;
/* ----- global defines ----------------------------------------------- */
#define DEB(x) if (i2c_debug>=1) x
#define DEB2(x) if (i2c_debug>=2) x
#define DEB3(x) if (i2c_debug>=3) x /* print several statistical values*/
#define ASSERT(x) if (!(x)) dev_dbg(&i2c_adap->dev, "ASSERTION FAILED at line %d in file %s\n", __LINE__, __FILE__);
/* Types and defines: */

#define TIMEOUT     200 /* Should be wait period of >100us i.e 1 byte @100KHz */


/* Local Types */

static unsigned long ip3203_controller[NR_I2C_DEVICES] = {  IP3203_UNIT0, IP3203_UNIT1 };
static unsigned long ip3203_intpin[NR_I2C_DEVICES] = { PNX8550_INT_I2C1, PNX8550_INT_I2C2 };

static struct i2c_adapter * slave_device = NULL;

static void do_slave_tasklet(unsigned long);
DECLARE_TASKLET(ip3203_slave_tasklet, do_slave_tasklet, 0);

#ifdef USE_DMA
static void dma_start( unsigned char *clientbuf, struct i2c_adapter * i2c_adap, int len, I2cMode transmitmode );
static void dma_end( struct i2c_adapter * i2c_adap );
static void __inline local_memcpy( void *dest, void *src, int len );
static void __inline local_memcpy_receive( void *dest, void *src, int len ); /* Temp, find a better solution */
#endif

#define div_DmaBufSize 128

static struct I2cBusObject ip3203_i2cbus[NR_I2C_DEVICES];

//static int i2c_debug = 0;


/* This function is called from Primary ISR */
static void recover_from_hardware_error(struct i2c_adapter * i2c_adap)
{
        struct I2cBusObject * busptr = (struct I2cBusObject *)i2c_adap->algo_data;

        dev_dbg(&i2c_adap->dev, "Recover from HW Error\n");
        DISABLE_I2C_CONTROLLER( busptr );
        STAOUT( busptr, 0 ); /* Don't generate START condition */
        STOOUT( busptr, 1 ); /* Recover from error condition */

        if( ( busptr->mode == MasterReceiver ) || ( busptr->mode == MasterTransmitter ) )
        {
                busptr->mst_status = errids_IsI2cHardwareError;
                busptr->isr_event |= evMasterReady;
                wake_up_interruptible(&(busptr->iic_wait_master));
        }
        if ( busptr->slv_enabled )
        {
                AAOUT( busptr, 1 ); /* ACK bit will be returned after slave address reception */
        }
        else
        {
                AAOUT( busptr, 0 ); /* NOT ACK bit will be returned after slave address reception */
        }
        busptr->mode = Idle;
        CLEAR_I2C_INTERRUPT( busptr ); /* Reset interrupt */
        ENABLE_I2C_CONTROLLER( busptr ); /* Enable I2C controller */
}

static __inline void iic_interrupt_idle(struct i2c_adapter * i2c_adap, struct I2cBusObject * busptr, int i2c_state)
{
        switch (i2c_state)
        {
        case 0x08: /* Master Transmitter or Master Receiver: A START condition has been transmitted */
                if( ( busptr->mst_txbufindex < busptr->mst_txbuflen ) || ( busptr->mst_rxbufindex == busptr->mst_rxbuflen ) ) /* if bytes to transmit or no bytes to receive  ? */
                {
                        int myaddr = (busptr->mst_address & 0xFE);
                        dev_dbg(&i2c_adap->dev, "START TRANSMITTED, Read bit cleared addr = %x\n", myaddr);
                        WRITE_IP3203_I2CDAT(busptr, myaddr); /* Read bit cleared */
                        busptr->mode = MasterTransmitter;
#ifdef USE_DMA
                        {
                                int dmalen;
                                if( busptr->mst_txbuflen <= div_DmaBufSize )
                                {
                                        dmalen = busptr->mst_txbuflen;
                                }
                                else
                                {
                                        dmalen = div_DmaBufSize;
                                }
                                dma_start( busptr->mst_txbuf, i2c_adap, dmalen,  MasterTransmitter);
                                busptr->mst_txbufindex = dmalen;
                        }
#endif
                }
                else
                {
                        int myaddr = ((busptr->mst_address & 0xFE) | 0x01);
                        dev_dbg(&i2c_adap->dev, "START TRANSMITTED, Read bit set(%x)\n", myaddr);
                        WRITE_IP3203_I2CDAT(busptr, myaddr); /* Read bit set */
                        busptr->mode = MasterReceiver;
                }
                STAOUT( busptr, 0 );/* Clear STA flag */
                CLEAR_I2C_INTERRUPT( busptr ); /* Reset interrupt */
	        break;
        case 0x60: /* Own SLA+W has been received; ACK has been returned */
                busptr->slv_bufindex = 1; /* reset buffer pointer !!! */
                AAOUT( busptr, busptr->slv_enabled ); /* ACK bit will be returned after next byte reception */
                busptr->mode = SlaveReceiver;

//                Todo : Start timeout timer in tasklet
//                tasklet_schedule(&ip3203_slave_tasklet);
                CLEAR_I2C_INTERRUPT(busptr); /* Don't reset i2c interrupt */
                break;
        case 0xA8: /* Own SLA+R has been received; ACK has been returned */
                busptr->mode = SlaveTransmitter;
                busptr->isr_event |= evSlaveReadRequest;
                slave_device = i2c_adap;
                tasklet_schedule(&ip3203_slave_tasklet);
                /* Don't reset i2c interrupt */
                break;
        case 0x70: /* General call address (0x00) has been received; ACK has been returned */
                /* fall through; */
        case 0x78: /* Arbitration lost in SLA+R/~W as master; General call address (0x00) has been received; ACK has been returned */
                /* fall through; */
		default:
                recover_from_hardware_error( i2c_adap );
		    	break;
        }
}

static __inline void iic_interrupt_master_transmitter(struct i2c_adapter * i2c_adap, struct I2cBusObject * busptr, int i2c_state)
{
        switch (i2c_state)
		{
        case 0x10: /* Master Transmitter or Master Receiver: A repeated START condition has been transmitted */
               /* Note : A repeated START should not occur when in MasterReceiver mode in this implementation */
                if( ( busptr->mst_txbufindex < busptr->mst_txbuflen ) || ( busptr->mst_rxbufindex == busptr->mst_rxbuflen ) ) /* if bytes to transmit or no bytes to receive  ? */
                {
                        int myaddr = (busptr->mst_address & 0xFE);
dev_dbg(&i2c_adap->dev, "START TRANSMITTED, Read bit cleared addr = %x\n", myaddr);
                        WRITE_IP3203_I2CDAT(busptr, myaddr); /* Read bit cleared */
                        busptr->mode = MasterTransmitter;
#ifdef USE_DMA
                        {
                                int dmalen;
                                if( busptr->mst_txbuflen <= div_DmaBufSize )
                                {
                                        dmalen = busptr->mst_txbuflen;
                                }
                                else
                                {
                                        dmalen = div_DmaBufSize;
                                }
                                dma_start( busptr->mst_txbuf, i2c_adap, dmalen,  MasterTransmitter);
                                busptr->mst_txbufindex = dmalen;
                        }
#endif
                }
                else
                {
                        int myaddr = ((busptr->mst_address & 0xFE) | 0x01);
dev_dbg(&i2c_adap->dev, "START TRANSMITTED, Read bit set(%x)\n", myaddr);
                        WRITE_IP3203_I2CDAT(busptr, myaddr); /* Read bit set */
                        busptr->mode = MasterReceiver;
                }
                STAOUT( busptr, 0 );/* Clear STA flag */
                CLEAR_I2C_INTERRUPT( busptr ); /* Reset interrupt */
				break;

        case 0x18: /* Master Transmitter: SLA+W has been transmitted; ACK has been received */
            /* fall through; */
        case 0x28: /* Master Transmitter: Data byte in I2DAT has been transmitted; ACK has been received */
                if( busptr->mst_txbufindex < busptr->mst_txbuflen )  /* transmit buffer not empty ? */
                {
                        WRITE_IP3203_I2CDAT(busptr, *( busptr->mst_txbuf + busptr->mst_txbufindex ));
                        busptr->mst_txbufindex++;
                        STAOUT( busptr, 0 ); /* Don't generate (repeated) START condition */
#ifdef USE_DMA
                        {
                                int dmalen;
                                if( ( busptr->mst_txbuflen - busptr->mst_txbufindex ) <= div_DmaBufSize )
                                {
                                        dmalen = busptr->mst_txbuflen - busptr->mst_txbufindex;
                                }
                                else
                                {
                                        dmalen = div_DmaBufSize;
                                }
                                dma_start( busptr->mst_txbuf + busptr->mst_txbufindex, i2c_adap, dmalen, MasterTransmitter);
                                busptr->mst_txbufindex += dmalen;
                        }
#endif
                }
                else
                {
                        if( busptr->mst_rxbufindex < busptr->mst_rxbuflen ) /* if bytes to receive  ? */
                        {
                                STAOUT( busptr, 1 ); /* Generate repeated START condition */
                        }
                        else
                        {
                                STAOUT( busptr, 0 ); /* Don't generate (repeated) START condition */
                                STOOUT( busptr, 1 ); /* Generate STOP condition */
                                busptr->mode = Idle;
                                busptr->isr_event |= evMasterReady;
                                wake_up_interruptible(&(busptr->iic_wait_master));
                        }
                }
                CLEAR_I2C_INTERRUPT( busptr ); /* reset interrupt */
                break;

        case 0x20: /* Master Transmitter: SLA+W has been transmitted; NOT ACK has been received */
                dev_dbg(&i2c_adap->dev, "Master Transmitter : SLA+W, NOT ACK has been received\n");
                /* fall through; */
        case 0x30: /* Master Transmitter: Data byte in I2DAT has been transmitted; NOT ACK has been received */
                STAOUT( busptr, 0 ); /* Don't generate (repeated) START condition */
                STOOUT( busptr, 1 ); /* Generate STOP condition */
                busptr->mode = Idle;
                busptr->mst_status = ( i2c_state == 0x20 ) ? errids_IsI2cDeviceError : errids_IsI2cWriteError;
                busptr->isr_event |= evMasterReady;
                CLEAR_I2C_INTERRUPT( busptr ); /* Reset interrupt */
                wake_up_interruptible(&(busptr->iic_wait_master));
                break;

        case 0x38: /* Arbitration lost in SLA+R/~W or Data bytes */
                busptr->mode = Idle;
                STAOUT( busptr, 1 ); /* Retry the master operation */
                busptr->mst_txbufindex = 0;
                busptr->mst_rxbufindex = 0;
                AAOUT( busptr, busptr->slv_enabled ); /* ACK bit will be returned, or not, after slave address reception */
                CLEAR_I2C_INTERRUPT( busptr ); /* Reset interrupt */
                break;

        case 0x68: /* Arbitration lost in SLA+R/~W as master; Own SLA+W has been received, ACK has been returned */
                STAOUT( busptr, 1 ); /* Retry the master operation */
                busptr->mst_txbufindex = 0;
                busptr->mst_rxbufindex = 0;

                busptr->slv_bufindex = 1; /* reset buffer pointer !!! */
                AAOUT( busptr, busptr->slv_enabled ); /* ACK bit will be returned, or not, after next byte reception */
                busptr->mode = SlaveReceiver;

//                Todo : Start timeout timer in tasklet
//                slave_device = i2c_adap;
//                tasklet_schedule(&ip3203_slave_tasklet);
                CLEAR_I2C_INTERRUPT(busptr); /* Don't reset i2c interrupt */
                break;

        case 0xB0: /* Arbitration lost in SLA+R/~W as master; Own SLA+R has been received, ACK has been returned */
                STAOUT( busptr, 1 ); /* Retry the master operation */
                busptr->mst_txbufindex = 0;
                busptr->mst_rxbufindex = 0;
                busptr->mode = SlaveTransmitter;

                busptr->isr_event |= evSlaveReadRequest;
                slave_device = i2c_adap;
                tasklet_schedule(&ip3203_slave_tasklet);
                /* Don't reset i2c interrupt */
                break;

        case 0x70: /* General call address (0x00) has been received; ACK has been returned */
                /* fall through; */
        case 0x78: /* Arbitration lost in SLA+R/~W as master; General call address (0x00) has been received; ACK has been returned */
                /* fall through; */
        default:
                recover_from_hardware_error( i2c_adap );
                break;
		}

}

static __inline void iic_interrupt_master_receiver(struct i2c_adapter * i2c_adap, struct I2cBusObject * busptr, int i2c_state)
{
        switch(i2c_state)
		{
		/* 0x10 : A repeated START should not occur when in MasterReceiver mode in this implementation */
        case 0x38: /* Arbitration lost in NOT ACK bit */
                busptr->mode = Idle;
                STAOUT( busptr, 1 ); /* Retry the master operation */
                busptr->mst_txbufindex = 0;
                busptr->mst_rxbufindex = 0;
                AAOUT( busptr, busptr->slv_enabled); /* ACK bit will be returned, or not, after slave address reception */
                CLEAR_I2C_INTERRUPT( busptr ); /* Reset interrupt */
                break;

        case 0x50: /* Data byte has been received; ACK has been returned */
                if ( busptr->mst_rxbufindex < busptr->mst_rxbuflen ) /* bytes to receive ? */
                {
                        *( busptr->mst_rxbuf + busptr->mst_rxbufindex++ ) = READ_IP3203_I2CDAT(busptr);
                }
                else
                {
                        (void)READ_IP3203_I2CDAT(busptr); /* Ignore received byte, no storage space available */
                }
                /* fall through; */
        case 0x40: /* SLA+R has been transmitted; ACK has been received */
#ifdef USE_DMA
                {
                    int dmalen;
                    if( ( busptr->mst_rxbuflen - 1 - busptr->mst_rxbufindex ) <= div_DmaBufSize )
                    {
                            dmalen = busptr->mst_rxbuflen -1 - busptr->mst_rxbufindex;
                    }
                    else
                    {
                            dmalen = div_DmaBufSize;
                    }
                    if( ( dmalen > 1 ) && /* Bug in N1 can not ask DMA to receive 1 byte */
                        ( busptr->mst_rxbufindex + dmalen <= div_DmaBufSize ) ) /* Bug in N1, second DMA transfer response is different */
                    {
                            dma_start( busptr->mst_rxbuf + busptr->mst_rxbufindex, i2c_adap, dmalen, MasterReceiver );
                            busptr->mst_rxbufindex += (dmalen - 1); /* Last byte should be read by the ISR */
                    }
                }
#endif

                if ( busptr->mst_rxbufindex < ( busptr->mst_rxbuflen - 1 ) )
                {
                        AAOUT( busptr, 1 ); /* ACK bit will be returned after byte reception */
                }
                else
                {
                        AAOUT( busptr, 0 ); /* NOT ACK bit will be returned after (last) byte reception */
                }
                STAOUT( busptr, 0 ); /* Don't generate (repeated) START condition */
                CLEAR_I2C_INTERRUPT( busptr ); /* reset interrupt */
                break;

        case 0x48: /* SLA+R has been transmitted; NOT ACK has been received */
                STAOUT( busptr, 0 ); /* Don't generate (repeated) START condition */
                STOOUT( busptr, 1 ); /* Generate STOP condition */
                busptr->mode = Idle;
                busptr->mst_status = errids_IsI2cDeviceError;
                busptr->isr_event |= evMasterReady;
                wake_up_interruptible(&(busptr->iic_wait_master));
                CLEAR_I2C_INTERRUPT( busptr ); /* reset interrupt */
                break;

            case 0x58: /* Data byte has been received; NOT ACK has been returned */
                if ( busptr->mst_rxbufindex < busptr->mst_rxbuflen ) /* bytes to receive ? */
                {
               	    *( busptr->mst_rxbuf + busptr->mst_rxbufindex++ ) = READ_IP3203_I2CDAT(busptr);
                     dev_dbg(&i2c_adap->dev, "received byte %x\n", *(busptr->mst_rxbuf + busptr->mst_rxbufindex -1));
                }
                else
                {
                    (void)READ_IP3203_I2CDAT(busptr);  /* Ignore received byte, no storage space available */
                    dev_dbg(&i2c_adap->dev, "ignore received byte\n");
                }
                AAOUT( busptr, busptr->slv_enabled ); /* ACK bit will be returned, or not, after slave address reception */
                STAOUT( busptr, 0 ); /* Don't generate (repeated) START condition */
                STOOUT( busptr, 1 ); /* Generate STOP condition */
                busptr->mode = Idle;
                busptr->isr_event |= evMasterReady;
                CLEAR_I2C_INTERRUPT( busptr ); /* reset interrupt */
                wake_up_interruptible(&(busptr->iic_wait_master));
                break;

        case 0x68: /* Arbitration lost in SLA+R/~W as master; Own SLA+W has been received, ACK has been returned */
                STAOUT( busptr, 1 ); /* Retry the master operation */
                busptr->mst_txbufindex = 0;
                busptr->mst_rxbufindex = 0;

                busptr->slv_bufindex = 1; /* reset buffer pointer !!! */
                AAOUT( busptr, busptr->slv_enabled ); /* ACK bit will be returned, or not, after next byte reception */
                busptr->mode = SlaveReceiver;

//                Todo : Start timeout timer in tasklet
//                tasklet_schedule(&ip3203_slave_tasklet);
                CLEAR_I2C_INTERRUPT(busptr); /* Don't reset i2c interrupt */
                break;

        case 0xB0: /* Arbitration lost in SLA+R/~W as master; Own SLA+R has been received, ACK has been returned */
                STAOUT( busptr, 1 ); /* Retry the master operation */
                busptr->mst_txbufindex = 0;
                busptr->mst_rxbufindex = 0;

                busptr->mode = SlaveReceiver;
                busptr->isr_event |= evSlaveReadRequest;
                slave_device = i2c_adap;
                tasklet_schedule(&ip3203_slave_tasklet);
                /* Don't reset i2c interrupt */
                break;

        case 0x70: /* General call address (0x00) has been received; ACK has been returned */
                /* fall through; */
        case 0x78: /* Arbitration lost in SLA+R/~W as master; General call address (0x00) has been received; ACK has been returned */
                /* fall through; */
        default:
				recover_from_hardware_error( i2c_adap );
				break;
		}
}

static __inline void iic_interrupt_slave_receiver(struct i2c_adapter * i2c_adap, struct I2cBusObject * busptr, int i2c_state)
{
        switch (i2c_state)
		{
        case 0x80: /* Previously addressed with own SLA; DATA byte has been received; ACK has been returned */
                if( busptr->slv_bufindex < busptr->slv_bufsize )
                {
                        *( busptr->slv_buf + busptr->slv_bufindex++ ) = READ_IP3203_I2CDAT(busptr);
                }
                else
                {
                        (void)READ_IP3203_I2CDAT(busptr); /* Ignore received byte, no storage space available */
                }
                AAOUT( busptr, busptr->slv_enabled ); /* ACK bit will be returned, or not, after next byte reception */

                if( busptr->slv_bufindex < busptr->slv_bufsize )
                {
#if 0 // USE_DMA
                        int dmalen;
                        {
                                if( ( busptr->slv_bufsize - busptr->slv_bufindex ) >  div_DmaBufSize )
                                {
                                        dmalen = div_DmaBufSize;
                                }
                                else
                                {
                                        dmalen = busptr->slv_bufsize - busptr->slv_bufindex;
                                }
                                dma_start( busptr->slv_buf + busptr->slv_bufindex, i2c_adap, dmalen, SlaveReceiver );
                                busptr->slv_bufindex += dmalen;/* Any number of bytes might be received */
                        }
#endif
                }
                CLEAR_I2C_INTERRUPT( busptr );  /* Reset i2c interrupt */
				break;

        case 0x88: /* Previously addressed with own SLA; DATA byte has been received; NOT ACK has been returned */
                if ( busptr->slv_bufindex < busptr->slv_bufsize )
                {
                        *( busptr->slv_buf + busptr->slv_bufindex++ ) = READ_IP3203_I2CDAT(busptr);// I2CDATIN( busptr );
                }
                else
                {
                        (void)READ_IP3203_I2CDAT(busptr); // I2CDATIN( busptr ); /* Ignore received byte, no storage space available */
                }

                AAOUT( busptr, busptr->slv_enabled ); /* ACK bit will be returned, or not, after slave address reception */
                CLEAR_I2C_INTERRUPT( busptr );    /* reset i2c interrupt */
				break;

        case 0xA0: /* A STOP condition or repeated START has been received while still address as SLV/REC or SLV/TRX */
				// Slave will be enabled again when data is out of the buffer
                AAOUT( busptr, 0 ); /* NOT ACK bit will be returned after slave address reception */

                busptr->isr_event |= evSlaveStopCondition;
                slave_device = i2c_adap;
                tasklet_schedule(&ip3203_slave_tasklet);
                CLEAR_I2C_INTERRUPT( busptr );   /* Reset i2c interrupt */
				break;

        case 0x70: /* General call address (0x00) has been received; ACK has been returned */
                /* fall through; */
        case 0x78: /* Arbitration lost in SLA+R/~W as master; General call address (0x00) has been received; ACK has been returned */
                /* fall through; */
        case 0x90: /* Previously addressed with General call; DATA byte has been received; ACK has been returned */
                /* fall through; */
        case 0x98: /* Previously addressed with General call; DATA byte has been received; NOT ACK has been returned */
                /* fall through; */
		default:
				recover_from_hardware_error( i2c_adap );
				break;
        }
}

static __inline void iic_interrupt_slave_transmitter(struct i2c_adapter * i2c_adap, struct I2cBusObject * busptr, int i2c_state)
{
        switch (i2c_state)
		{
		case 0xA0: /* A STOP condition or repeated START has been received while still address as SLV/TRX */
				// Slave will be enabled again when data is out of the buffer
                AAOUT( busptr, 0 ); /* NOT ACK bit will be returned after slave address reception */

                busptr->isr_event |= evSlaveStopCondition;
                slave_device = i2c_adap;
                tasklet_schedule(&ip3203_slave_tasklet);
                CLEAR_I2C_INTERRUPT( busptr );   /* Reset i2c interrupt */
                break;

        case 0xB8: /* Data byte in I2DAT has been transmitted; ACK has been received */
                if ( busptr->slv_bufindex < busptr->slv_bufsize )
                {
                        WRITE_IP3203_I2CDAT(busptr, *( busptr->slv_buf + busptr->slv_bufindex ));
                        busptr->slv_bufindex++;

                        if ( busptr->slv_bufindex < busptr->slv_buflen )
                        {
                                AAOUT( busptr, 1 ); /* Data byte will be transmitted */
                        }
                        else
                        {
                                AAOUT( busptr, 0 ); /* Last data byte will be transmitted */
                        }
#ifdef USE_DMA
                        {
                            int dmalen;
                            if( ( busptr->slv_buflen - 1 - busptr->slv_bufindex ) >  div_DmaBufSize )
                            {
                                    dmalen = div_DmaBufSize;
                            }
                            else
                            {
                                    dmalen = busptr->slv_buflen - 1 - busptr->slv_bufindex;
                            }
                            if( dmalen > 0 )
                            {
                                    dma_start( busptr->slv_buf + busptr->slv_bufindex, i2c_adap, dmalen, SlaveTransmitter );
                                    busptr->slv_bufindex += dmalen;
                            }
                        }
#endif
                        CLEAR_I2C_INTERRUPT( busptr ); /* Reset interrupt */
                }
                else
                {
                        busptr->isr_event |= evSlaveReadRequest;
                        slave_device = i2c_adap;
                        tasklet_schedule(&ip3203_slave_tasklet);
                        /* Don't reset i2c interrupt */
                }
                break;

        case 0xC0: /* Data byte in I2DAT has been transmitted; NOT ACK has been received */
				/* fall through; */
        case 0xC8: /* Last data byte in I2DAT has been transmitted (AA=0); ACK has been received */
                AAOUT( busptr, busptr->slv_enabled ); /* ACK bit will be returned, or not, after slave address reception */

                busptr->isr_event |= evSlaveTransmitterReady;
                slave_device = i2c_adap;
                tasklet_schedule(&ip3203_slave_tasklet);
                /* Don't reset i2c interrupt */
                break;

        case 0x70: /* General call address (0x00) has been received; ACK has been returned */
                /* fall through; */
        case 0x78: /* Arbitration lost in SLA+R/~W as master; General call address (0x00) has been received; ACK has been returned */
                /* fall through; */
        default: /* Miscellaneous: No relevant state information available; SI=0 */
                recover_from_hardware_error( i2c_adap );
                break;
		}

}

static irqreturn_t ip3203_iic_interrupt(int irq, void *dev_id)
{
		// get the BusObject
		struct i2c_adapter * i2c_adap = (struct i2c_adapter *)dev_id;
        struct I2cBusObject * busptr = (struct I2cBusObject *)i2c_adap->algo_data;
        int i2c_state;

#ifdef USE_DMA
        if( Idle != READ_IP3203_DMA_CONTROL( busptr ) ) /* DMA was active */
        {
            dma_end( i2c_adap );
        }
#endif

        // Get State of Iic Bus
        i2c_state = READ_IP3203_I2CSTAT(busptr);
        switch (busptr->mode)
		{
            case Idle:
                    iic_interrupt_idle(i2c_adap, busptr, i2c_state);
                    break;
            case MasterTransmitter:
                    iic_interrupt_master_transmitter(i2c_adap, busptr, i2c_state);
                    break;
            case MasterReceiver:
                    iic_interrupt_master_receiver(i2c_adap, busptr, i2c_state);
                    break;
            case SlaveReceiver:
                    iic_interrupt_slave_receiver(i2c_adap, busptr, i2c_state);
                    break;
            case SlaveTransmitter:
                    iic_interrupt_slave_transmitter(i2c_adap, busptr, i2c_state);
                    break;
		}
        return IRQ_HANDLED;
}

static void do_slave_tasklet(unsigned long unused)
{
        struct I2cBusObject * busptr = (struct I2cBusObject *)slave_device->algo_data;

        if (busptr->mode == SlaveReceiver)
        {
				if (busptr->isr_event & evSlaveRxIntermediateNtf)
				{
                        dev_dbg(&slave_device->dev, "Slave Receiver : Intermediate Ntf\n");
                        busptr->isr_event &= ~evSlaveRxIntermediateNtf;
                }
                if (busptr->isr_event & evSlaveReceiverReady)
                {
                        busptr->slv_bufindex = 1;
                        busptr->isr_event &= ~evSlaveReceiverReady;
                }
                if (busptr->isr_event & evSlaveReadRequest)
                {
                        dev_dbg(&slave_device->dev, "Slave Receiver : ReadRequest\n");
                        busptr->isr_event &= ~evSlaveReadRequest;
                }
                if (busptr->isr_event & evSlaveTransmitterReady)
                {
                        dev_dbg(&slave_device->dev, "Slave Receiver : TransmitterReady\n");
                        busptr->isr_event &= ~evSlaveTransmitterReady;
                }
                if (busptr->isr_event & evSlaveStopCondition)
                {
                        dev_dbg(&slave_device->dev, "Slave Receiver : SlaveStopCondition\n");
                        if (busptr->slv_usr_notify)
                        {
dev_dbg(&slave_device->dev, "Slave Receiver : Sending kill\n");
                                kill_fasync(busptr->slv_usr_notify, SIGIO, POLL_IN);
                        }
                        else  if (busptr->slv_enabled)
                        {
dev_dbg(&slave_device->dev, "Slave Receiver : AAOUT\n");
                                AAOUT(busptr, 1);
                        }

dev_dbg(&slave_device->dev, "Slave Receiver : Idle\n");
                        busptr->mode = Idle;
                        busptr->isr_event &= ~evSlaveStopCondition;
                }

        }
        else if (busptr->mode == SlaveTransmitter)
        {
                if (busptr->isr_event & evSlaveRxIntermediateNtf)
                        dev_dbg(&slave_device->dev, "Slave Transmitter : Intermediate Ntf\n");
                if (busptr->isr_event & evSlaveReceiverReady)
                        dev_dbg(&slave_device->dev, "Slave Transmitter : Ready\n");
                if (busptr->isr_event & evSlaveReadRequest)
                        dev_dbg(&slave_device->dev, "Slave Transmitter : ReadRequest\n");
                if (busptr->isr_event & evSlaveTransmitterReady)
                        dev_dbg(&slave_device->dev, "Slave Transmitter : TransmitterReady\n");
                if (busptr->isr_event & evSlaveStopCondition)
                        dev_dbg(&slave_device->dev, "Slave Transmitter : SlaveStopCondition\n");
        }

}

#define WAIT_ip3203INT()                do{ int t = TIMEOUT;                                       \
                                            while(!(READ_IP3203_INT_STATUS(busptr) & IP3203_INTBIT)     \
                                                   && (t-->0)){}                               \
                                          }while(0)
#define WAIT_IP3203_STO_OR_INT()        do{ int t = TIMEOUT;                                       \
                                            while(!(READ_IP3203_INT_STATUS(busptr) & IP3203_INTBIT)     \
                                                 &&(READ_IP3203_INTROG(busptr) & IP3203_INTRO_STO)      \
                                                 &&(t-->0)){}                                  \
                                          }while(0)

/******************************************************************************
*   This function resets IP3203. The parameter "a" indicates base address
*   of the IP3203 Block.
*******************************************************************************/
static void ip3203_reset(struct i2c_adapter *i2c_adap)
{
    struct I2cBusObject * busptr = (struct I2cBusObject *)i2c_adap->algo_data;

    dev_dbg(&i2c_adap->dev, "Reset the IP3203\n");
#ifdef USE_DMA
    dev_dbg(&i2c_adap->dev, "Stop DMA\n");
    WRITE_IP3203_DMA_CONTROL(busptr, 0); /*  *IP3203_DMA_CONTROL(a) = 0;  Disable DMA */
    WRITE_IP3203_DMA_LENGTH(busptr, 0);
#endif
    DISABLE_I2C_INTERRUPT(busptr);       /*  *IP3203_INT_ENABLE(a) = 0;  Disable I2C interrupts */
    AAOUT(busptr, 0);                    /*  *IP3203_I2CCON(a) &= ~IP3203_AA;  Disable slave mode     */
    STAOUT(busptr, 0);                   /* *IP3203_I2CCON(a)     &= ~IP3203_STA;  Remove start request   */

    if(READ_IP3203_INTROG(busptr) & IP3203_INTRO_BB)
    {   /* Bus is busy */
        dev_dbg(&i2c_adap->dev, "I2C bus is busy\n");
        WAIT_ip3203INT();
dev_dbg(&i2c_adap->dev, "int_status = 0x%x\n", READ_IP3203_INT_STATUS(busptr));
        if(READ_IP3203_INT_STATUS(busptr) & IP3203_INTBIT)
        {/* Interrupt flag is set */
            unsigned int i2c_state = READ_IP3203_I2CSTAT(busptr);
            if((i2c_state == 0x08) || (i2c_state == 0x10))
            {
                WRITE_IP3203_I2CDAT(busptr, 0xEE); /*  *IP3203_I2CDAT(a) = 0xEE; Transmit dummy address */
                CLEAR_I2C_INTERRUPT(busptr); /* *IP3203_INT_CLEAR(a) = IP3203_INTBIT; Clear I2C interrupt    */
            }
            else if((i2c_state == 0x40) || (i2c_state == 0x50))
            {   /* One byte must be read which should be NACKed */
                AAOUT(busptr, 0);  /* *IP3203_I2CCON(a)   &= ~IP3203_AA;  NACK next byte      */
                CLEAR_I2C_INTERRUPT(busptr); /* *IP3203_INT_CLEAR(a) = IP3203_INTBIT; Clear I2C interrupt    */
            }
            else
            {
                /* For rest of the states just generating stop condition is enough */
            }
            WAIT_ip3203INT();
            STOOUT(busptr, 1);  /*  *IP3203_I2CCON(a)   |= IP3203_SETSTO;  Generate stop condition */
            CLEAR_I2C_INTERRUPT(busptr); /* *IP3203_INT_CLEAR(a) = IP3203_INTBIT; Clear I2C interrupt: Not necessary but no harm */
            WAIT_IP3203_STO_OR_INT();
        }
        else
        {/* Interrupt flag did not set, May be due to clock stretching */
            STOOUT(busptr, 1);  /*  *IP3203_I2CCON(a)   |= IP3203_SETSTO;  Generate stop condition */
            CLEAR_I2C_INTERRUPT(busptr); /* *IP3203_INT_CLEAR(a) = IP3203_INTBIT; Clear I2C interrupt: Not necessary but no harm */
            WAIT_IP3203_STO_OR_INT();
        }
    }
    else
    { /* Bus is free, do nothing */
        dev_dbg(&i2c_adap->dev, "I2C bus is free\n");
//        STOOUT(busptr, 1);  /*  *IP3203_I2CCON(a)   |= IP3203_SETSTO;  Generate stop condition */
//        CLEAR_I2C_INTERRUPT(busptr); /* *IP3203_INT_CLEAR(a) = IP3203_INTBIT; Clear I2C interrupt: Not necessary but no harm */
//        WAIT_IP3203_STO_OR_INT();
    }

    if(READ_IP3203_INTROG(busptr) & IP3203_INTRO_STO)
    {
        ASSERT(FALSE); /* Could not free I2C bus */
    }

    /* Set default values */
    DISABLE_I2C_CONTROLLER(busptr);      /*  *IP3203_I2CCON(a) &= ~IP3203_EN; Disable I2C controller */
#ifdef USE_DMA
    WRITE_IP3203_DMA_CONTROL(busptr, 0); /*  *IP3203_DMA_CONTROL(a) = 0;  Disable DMA */
#endif
    DISABLE_I2C_INTERRUPT(busptr);       /*  *IP3203_INT_ENABLE(a) = 0;  Disable I2C interrupts */
    AAOUT(busptr, 0);                    /*  *IP3203_I2CCON(a) &= ~IP3203_AA;  Disable slave mode     */
    STAOUT(busptr, 0);                   /* *IP3203_I2CCON(a) &= ~IP3203_STA;  Remove start request   */
    CLEAR_I2C_INTERRUPT(busptr);         /* *IP3203_INT_CLEAR(a) = IP3203_INTBIT; Clear I2C interrupt    */

    dev_dbg(&i2c_adap->dev, "Reset done, re-init\n");
//    enable_irq(busptr->int_pin);
    /*  re-init again */
    ENABLE_I2C_CONTROLLER(busptr);
    ENABLE_I2C_INTERRUPT(busptr);
    if (busptr->slv_enabled == TRUE)
    {
        AAOUT(busptr, 1);
    }
    dev_dbg(&i2c_adap->dev, "re-init done\n");
}

/*
 *
 */
static void calc_speed_values (unsigned int fsSpeed, unsigned int hsSpeed, unsigned int * pfsbir, unsigned int * phsbir)
{
        unsigned int fsBitRate, hsBitRate;
        unsigned int clockFreqKHz = MODULE_CLOCK;

        union _i2cFSBIR ip3203_i2cFSBIR;
        union _i2cHSBIR ip3203_i2cHSBIR;

        /* calculate register values */
        fsBitRate = clockFreqKHz/(8*fsSpeed) - 1;
        if (clockFreqKHz%(8*fsSpeed) != 0)
        {
                fsBitRate++;
        }

        hsBitRate = clockFreqKHz/(3*hsSpeed) - 1;
        if (clockFreqKHz%(3*hsSpeed) != 0)
        {
                hsBitRate++;
        }

        /* limit upper boundary of register value = lower limit of speed */
        if (fsBitRate > 127)
        {
                fsBitRate = 127;
        }

        if (hsBitRate > 31)
        {
                hsBitRate = 31;
        }

        ip3203_i2cFSBIR.u32 = 0;
        ip3203_i2cHSBIR.u32 = 0;
        ip3203_i2cFSBIR.bits.fsBitRate = fsBitRate;
        ip3203_i2cHSBIR.bits.hsBitRate = hsBitRate;
        if (fsSpeed > TMHW_I2C_MAX_SS_SPEED)
        {/* SFMode = FS mode */
                ip3203_i2cFSBIR.bits.SFMode = I2C_FS_MODE;
        }
        else
        {/* SFMode = SS mode */
                ip3203_i2cFSBIR.bits.SFMode = I2C_SS_MODE;
        }
        *pfsbir = ip3203_i2cFSBIR.u32;
        *phsbir = ip3203_i2cHSBIR.u32;

        /* IP3203 version 2.c and 2.d patch */
        switch (*pfsbir)
        {
        case (0x00):
                *pfsbir++;
                break;
        case (0x02):
                *pfsbir = 0x04;
                break;
        case (0x03):
                *pfsbir++;
                break;
        case (0x08):
                *pfsbir =0xa;
                break;
        case (0x09):
                *pfsbir++;
                break;
        case (0x0b):
                *pfsbir++;
                break;
        case (0x0e):
                *pfsbir++;
                break;
        case (0x28):
                *pfsbir++;
                break;
        case (0x4f):
                *pfsbir++;
                break;
        case (0x80):
                *pfsbir++;
                break;
        case (0x83):
                *pfsbir = 0x85;
                break;
        case (0x84):
                *pfsbir++;
                break;
        case (0x87):
                *pfsbir = 0x8a;
                break;
        case (0x88):
                *pfsbir = 0x8a;
                break;
        case (0x89):
                *pfsbir++;
                break;
        default:
                if (*pfsbir >= 0xaa)
                {
                        *pfsbir = 0xa9;
                }
        break;
        }

        switch (*phsbir)
        {
        case (0x00):
                *phsbir++;
                break;
        case (0x0c):
                *phsbir++;
                break;
        default:
                break;
        }
        /* end IP3203 version 2.c and 2.d patch */
}/* i2cConvertSpeedToReg */


static void ip3203_init(struct i2c_adapter * i2c_adap, int device)
{
        struct I2cBusObject *busptr;
        unsigned int fsbir=0, hsbir=0; /* Bit rate */

        dev_dbg(&i2c_adap->dev, "init I2C bus %x\n", i2c_adap->nr);
        busptr                    = &ip3203_i2cbus[device];
        busptr->offset            = ip3203_controller[device];
        busptr->int_pin           = ip3203_intpin[device];
        busptr->mode              = Idle;
        busptr->isr_event         = 0;    /* Clear all event flags(bits) */
//        busptr->bus_blocked       = FALSE;

        busptr->slv_enabled       = FALSE;
        busptr->slv_buf           = NULL; //SlaveBuffer[device];
        busptr->slv_bufindex      = 0;
        busptr->slv_buflen        = 0;
        busptr->slv_bufsize       = 0;    //SLAVE_BUF_SIZE;
        busptr->slv_usr_notify    = NULL;

//        busptr->mst_timer         = TIMER_OFF;
//        busptr->mst_timeout       = TIMER_OFF;
//        busptr->mst_timeout_valid = FALSE;
//        busptr->slv_timer         = TIMER_OFF;
//        busptr->slv_timeout       = TIMER_OFF;
//        busptr->slv_timeout_valid = FALSE;

        /* The remaining elements of the struct are initialized when i2c write,read functions are called */
#ifdef USE_DMA
        busptr->dma_bufptr        = kmalloc(div_DmaBufSize, GFP_KERNEL | GFP_DMA /* GFP_ATOMIC */ );
		ASSERT( NULL != busptr->dma_bufptr  );
        busptr->dma_transmit      = Idle;
#endif
        init_waitqueue_head( &(busptr->iic_wait_master) );

        DISABLE_I2C_CONTROLLER( busptr ); /* Disable I2C controller */

        calc_speed_values ( TMHW_I2C_MAX_SS_SPEED, TMHW_I2C_MAX_SS_SPEED, &fsbir, &hsbir );
        WRITE_IP3203_FSBIR( busptr, fsbir);
        WRITE_IP3203_HSBIR( busptr, hsbir);

        AAOUT( busptr, 0 ); /* Slave mode disabled */
        STOOUT( busptr, 0 ); /* Do not generate stop condition */
        STAOUT( busptr, 0 ); /* Do not generate Start condition */

        /* Install interrupt handler. */
        if (0 != request_irq( busptr->int_pin, ip3203_iic_interrupt, SA_INTERRUPT, "i2c", (void *)i2c_adap))
        {
                ASSERT( 0 );
        }
        CLEAR_I2C_INTERRUPT( busptr ); /* Clear the interrupt flags if by-chance(if not power up ) they are set */
        disable_irq( busptr->int_pin );   /* disable i2c interrupt in Interrupt controller */
        enable_irq( busptr->int_pin );   /* Enable i2c interrupt in Interrupt controller */

        ENABLE_I2C_CONTROLLER( busptr ); /* Enable I2C Controller */
        ENABLE_I2C_INTERRUPT( busptr ); /* Enable  both the SI and DMA interrupts */

        return;
}

/* Master ReadWrite interface function */
static unsigned int i2c_write_read(struct i2c_adapter *i2c_adap, int address, void *msgwram, int lenw, void *msgr, int lenr )
{
        struct I2cBusObject * busptr = (struct I2cBusObject *)i2c_adap->algo_data;

        int retval = 0;

#ifdef USE_DMA
        if (busptr->dma_len)
        {
            /* Check to see if the previous DMA transfer has completed */
            while(READ_IP3203_DMA_COUNTER(busptr)!=0)
            {
                msleep(1);
            }

            dma_end(i2c_adap);
        }
#endif
        busptr->mst_status     = errids_Ok;
        busptr->mst_address    = address;
        busptr->mst_txbuflen   = lenw;
        busptr->mst_txbufindex = 0;
        busptr->mst_rxbuf      = msgr;
        busptr->mst_rxbufindex = 0;

        if( msgwram != NULL )
        {
                busptr->mst_txbuf = msgwram;
        }
        else
        {
                busptr->mst_txbuflen = 0; /* If both ptrs are NULL, do not write data */
        }

        if( msgr != NULL )
        {
                busptr->mst_rxbuflen = lenr;
        }
        else
        {
                busptr->mst_rxbuflen = 0;
        }

		if(busptr->mst_address == 0xa0)
		{
/*
			printk("mst_address: %08x\n", busptr->mst_address);
			printk("mst_txbuflen: %d\n", busptr->mst_txbuflen);
			printk("mst_rxbuflen: %d\n", busptr->mst_rxbuflen);
			if(busptr->mst_rxbuf != 0)
				printk("mst_rxbuf: %02x\n", busptr->mst_rxbuf[0]);
			if(busptr->mst_txbuf != 0)
				printk("mst_txbuf: %02x\n", busptr->mst_txbuf[0]);
*/
		}

        STAOUT( busptr, 1 ); /* Generate START condition when selected bus is free and start interrupt driven machine */
        /* Suspend current task till the event flag is set */
        /* Wait for IIC transfer, result of action is written in I2cBusObject */

        retval = interruptible_sleep_on_timeout(&(busptr->iic_wait_master), (5*HZ));
        if (retval == 0)
        {
                int i2c_state = READ_IP3203_I2CSTAT(busptr);  // I2CSTATIN( device );
                int i2c_intstate = READ_IP3203_INT_STATUS(busptr);

				pr_info("I2C3203 timeout\n");
                dev_dbg(&i2c_adap->dev, "I2C Status 0%x, int stat %x\n", i2c_state, i2c_intstate);
                return errids_IsI2cHardwareError;
        }

        return busptr->mst_status;

}

#ifdef USE_DMA
static void dma_start( unsigned char * clientbuf, struct i2c_adapter * i2c_adap, int len, I2cMode transmitmode )
{
        struct I2cBusObject * busptr = (struct I2cBusObject *)i2c_adap->algo_data;

        ASSERT( len <= div_DmaBufSize ); /* DMA buffer is not that big, internal error */

        if( len != 0 )
        {
                busptr->dma_transmit  = transmitmode;
                busptr->dma_clientbuf = clientbuf;
                busptr->dma_len       = len;

                if( (MasterTransmitter == transmitmode) || (SlaveTransmitter == transmitmode))
                {

                        local_memcpy( busptr->dma_bufptr, clientbuf, len ); /* copy to DMA buffer */
                }
                WRITE_IP3203_DMA_ADDR( busptr, virt_to_phys(busptr->dma_bufptr));
                WRITE_IP3203_DMA_LENGTH( busptr, len);
                WRITE_IP3203_DMA_CONTROL( busptr, transmitmode); /* Allow DMA to handle the interrupt */
        }
}

static void dma_end( struct i2c_adapter * i2c_adap )
{
        struct I2cBusObject * busptr = (struct I2cBusObject *)i2c_adap->algo_data;

        if( (busptr->dma_transmit == MasterReceiver) || (busptr->dma_transmit == SlaveReceiver) )
        {
                local_memcpy_receive( busptr->dma_clientbuf, busptr->dma_bufptr, busptr->dma_len ); /* May be less bytes should be copied */
        }

        if(!READ_IP3203_DMA_STATUS( busptr ) )
        {
                ASSERT( 0 ); /* I2C DMA error occured */
                /* DMA_ERROR cleared automatically when the interrupt is cleared */
        }
        else
        {
                int correction;
                correction = READ_IP3203_DMA_COUNTER( busptr ) ;

                switch( busptr->mode )
                {
                case MasterTransmitter:
                        busptr->mst_txbufindex -= correction; /* correction == 0 under normal condition */
                        break;
                case MasterReceiver:
                        if( correction != 0)
                        { /* DMA unsuccessful, because of the logic implemented in ADOC DMA -- the correction */
                                correction--;
                        }
                        busptr->mst_rxbufindex -= correction; /* Correct the pointer */
                        ASSERT( busptr->mst_rxbufindex >= 0 ); /* How this index could become -ve must be ADOC I2C HW error */
                        break;
                case SlaveReceiver:
                        busptr->slv_bufindex = busptr->slv_bufindex - correction; /* correct the pointer */
                        break;
                case SlaveTransmitter:
                        busptr->slv_bufindex = busptr->slv_bufindex - correction; /* correct the pointer */
                        break;
                default:
                        ASSERT( 0 ); /* I2C is idle, who started the DMA? */
                        break;
                }
        }
        WRITE_IP3203_DMA_CONTROL( busptr, Idle); /* Allow CPU to handle the interrupts */
}


static void __inline local_memcpy( void *dest, void *src, int len )
{
        void * re_dest = (void *)ioremap_nocache(virt_to_phys(dest), len);

	    memcpy(re_dest, src, len);
}


static void __inline local_memcpy_receive( void *dest, void *src, int len )
{
        memcpy(dest, src, len);
}
#endif

static int ip3203_xfer(struct i2c_adapter *i2c_adap, struct i2c_msg msgs[], int num)
{
        struct i2c_msg *pmsg;
        void * wr_buf = NULL;
        void * rd_buf = NULL;
        int i, wr_len = 0, rd_len = 0;
        unsigned char addr = msgs[0].addr;
        int ret=0;

        dev_dbg(&i2c_adap->dev, "IP3203 xfer nr %d\n", num);
        // when num = 2, assume that first some data must be written to the device
		// before the actual read can be done. Since we are now working in a multi-
		// master environment, a repeated start must be used iso stop/start!
        ASSERT(num > 0 && num <= 2);
        for (i = 0; i < num; i++)
        {
                pmsg = &msgs[i];

                if (i == 0)
                {
                        if (pmsg->flags & I2C_M_TEN)
                        {
                		    // addr = 10 bit addr, not supported yet
                        }
                        else
                        {
                               // addr = 7 bit addr
                               addr &= 0x7f;
                               addr <<= 1;
                        }
                }
                /*  wr_write handles all master read/write commands, including repeat start (I2C_M_NOSTART)
                 */
                if (pmsg->flags & I2C_M_RD )
                {
                        /* read bytes into buffer*/
                        rd_buf = pmsg->buf;
                        rd_len = pmsg->len;
                }
                else
                {
                        /* write bytes from buffer */
                        wr_buf = pmsg->buf;
                        wr_len = pmsg->len;
                }
        }
        if (num != 0)
        {
                switch(ret = i2c_write_read(i2c_adap, addr, wr_buf, wr_len, rd_buf, rd_len))
                {
                case errids_Ok:
                        break;
                case errids_IsI2cHardwareError:
                        num = -1;
                        dev_dbg(&i2c_adap->dev, "Hardware error\n");
                        ip3203_reset(i2c_adap);
                        break;
                case errids_IsI2cDeviceError:
                        num = -1;
                        dev_dbg(&i2c_adap->dev, "Device error\n");
                        ip3203_reset(i2c_adap);
                        break;
                case errids_IsI2cWriteError:
                        num = -1;
                        dev_dbg(&i2c_adap->dev, "Write error\n");
                        ip3203_reset(i2c_adap);
                        break;
                default:
                        num = -1;
                        dev_dbg(&i2c_adap->dev, "Error Unkonwn\n");
                        ip3203_reset(i2c_adap);
                        break;
                }
        }

        return num;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
static int algo_control(struct i2c_adapter *i2c_adap, unsigned int cmd, unsigned long arg)
{
        struct I2cBusObject * busptr =  (struct I2cBusObject *)i2c_adap->algo_data;
        switch (cmd)
        {
        case I2C_SET_SLAVE_ADDRESS:
                if (busptr->slv_enabled)
                        return -EBUSY;
                else
                {
                        WRITE_IP3203_I2CSLA( busptr, I2CADDRESS( arg ));/* >> 1 */; /* 7 bits address, No General call support */
dev_dbg(&i2c_adap->dev, "Set Own adress to %x\n", READ_IP3203_I2CSLA(busptr));
                }
                break;
        case I2C_SET_SLAVE_ENABLE:
                if (arg)
                {
                        if( !busptr->slv_enabled )
                        {
                               unsigned int len = (unsigned int)arg;
                               len++;  // Need an extra byte for the length
                               busptr->slv_buf = kmalloc(len, GFP_KERNEL);
                               if (NULL != busptr->slv_buf)
                               {
                                       busptr->slv_usr_notify = &i2c_adap->fasync;
                                       busptr->slv_bufindex = 1;
                                       busptr->slv_buflen = 0; // todo : slave transmitter, but we can only reveive
                                       busptr->slv_bufsize = len;

                                       busptr->slv_enabled = TRUE;
                                       AAOUT( busptr, 1 ); /* ACK bit will be returned after slave address reception */
                                       dev_dbg(&i2c_adap->dev, "Enable Slave\n");
                               }
                               else return -ENOMEM;
                        }
                        else return -EBUSY;
                }
                else
                {
                        busptr->slv_usr_notify = NULL;
                        busptr->slv_bufsize    = 0;
                        AAOUT( busptr, 0 ); /* ACK will not be sent after slave address reception */
                        busptr->slv_enabled = FALSE;

                        if (NULL != busptr->slv_buf)
                        {
                            kfree(busptr->slv_buf);  // kfree returns void, no check needed
                        }
                        busptr->slv_bufindex  = 0;
                        busptr->slv_buflen    = 0;

dev_dbg(&i2c_adap->dev, "Disable Slave\n");
                }
                break;
      case I2C_GET_SLAVE_DATA:
                {
                        unsigned long ret;
                        unsigned char * user_data = (unsigned char *)arg;
                        unsigned char nr_of_bytes = busptr->slv_bufindex -1;

                        busptr->slv_buf[0] = nr_of_bytes;
                        ret = copy_to_user(user_data, busptr->slv_buf, busptr->slv_bufindex);
                        busptr->slv_bufindex = 1;
                        if (busptr->slv_enabled == TRUE)
                        {
                                AAOUT( busptr, 1);  /* ACK bit will be returned after slave address reception */
                        }
                        if (0 == busptr->slv_buf[0])
                        {
                                return -ENODATA;
                        }
                }
                break;
        }
        return 0;
}
#endif

static u32 ip3203_func(struct i2c_adapter *adap)
{
        return I2C_FUNC_SMBUS_EMUL | I2C_FUNC_PROTOCOL_MANGLING | I2C_FUNC_I2C;
}

/* -----exported algorithm data: -------------------------------------  */

static struct i2c_algorithm ip3203_algo_0 = {
	    .master_xfer = ip3203_xfer,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
        .algo_control = algo_control,                   /* ioctl                */
#endif
        .functionality = ip3203_func,                     /* functionality        */
};
static struct i2c_algorithm ip3203_algo_1 = {
        .master_xfer = ip3203_xfer,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
        .algo_control = algo_control,
#endif
        .functionality = ip3203_func,
};
static struct i2c_algorithm * ip3203_algo[NR_I2C_DEVICES] = { &ip3203_algo_0, &ip3203_algo_1 };
/*
 * registering functions to load algorithms at runtime
 */
int i2c_ip3203_add_bus(int device, struct i2c_adapter * i2c_adap)
{
        int res;
        dev_dbg(&i2c_adap->dev, "i2c-ip3203: I2C IP3203 algorithm module\n");
        dev_dbg(&i2c_adap->dev, "i2c_ip3203_add_bus called\n");
        printk("i2c_ip3203_add_bus called (no dev_dbg)\n");
        DEB2(dev_dbg(&i2c_adap->dev, "i2c-ip0105: HW routines for %s registered.\n", i2c_adap->name));

        /* register new adapter to i2c module... */
        i2c_adap->algo = ip3203_algo[device];
        i2c_adap->timeout = 100;            /* default values, should       */
        i2c_adap->retries = 3;              /* be replaced by defines       */
        dev_dbg(&i2c_adap->dev, "initialise IP3203 device %d\n", device);
        ip3203_init(i2c_adap, device);

        if ((res = i2c_add_numbered_adapter(i2c_adap)) < 0)
        {
                dev_dbg(&i2c_adap->dev, "i2c-ip3203 %d: Unable to register with I2C\n", device);
                return -ENODEV;
        }
        dev_dbg(&i2c_adap->dev, "add adapter %d returned %x\n", i2c_adap->nr, res);
        /* Todo : scan bus */

        return 0;
}

int i2c_ip3203_del_bus(struct i2c_adapter *i2c_adap)
{
        struct I2cBusObject *busptr;
        int device;
        int res;

        for( device = 0; device < NR_I2C_DEVICES; device++ )
        {
                dev_dbg(&i2c_adap->dev, "exit bus %x\n", device);
                busptr = &ip3203_i2cbus[device];

                DISABLE_I2C_INTERRUPT( busptr );
                DISABLE_I2C_CONTROLLER( busptr ); /* Disable I2C controller */
                if (busptr->slv_enabled)
                {
                        busptr->slv_usr_notify = NULL;
                        busptr->slv_bufsize    = 0;
                        AAOUT( busptr, 0 ); /* ACK will not be sent after slave address reception */
                        busptr->slv_enabled = FALSE;

                        if (NULL != busptr->slv_buf)
                        {
                                kfree(busptr->slv_buf); // kfree returns void, no check needed
                        }
                        busptr->slv_bufindex  = 0;
                        busptr->slv_buflen    = 0;
                }

                disable_irq(busptr->int_pin);   /* Enable i2c interrupt in Interrupt controller */
                free_irq(busptr->int_pin, (void *)&ip3203_i2cbus[device]); // free_irq returns void, no check needed
                enable_irq(busptr->int_pin);   /* Enable i2c interrupt in Interrupt controller */

#ifdef USE_DMA
                kfree(busptr->dma_bufptr); // kfree returns void, no check needed
#endif
                if ((res = i2c_del_adapter(i2c_adap)) < 0)
                        return res;
                DEB2(dev_dbg(&i2c_adap->dev, "i2c-ip3203: adapter unregistered: %s\n",i2c_adap->name));
        }

        return 0;
}


static struct i2c_adapter ip3203_ops_0 = {
       .name = "IP3203 0",                        // name
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31)
       .id = I2C_HIGH_PERFORMANCE_PORT_1,                // id
#endif
       .algo_data = &ip3203_i2cbus[0], // algo_data
       .nr = 0,
};

static struct i2c_adapter ip3203_ops_1 = {
       .name = "IP3203 1",
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31)
       .id = I2C_HIGH_PERFORMANCE_PORT_2,
#endif
       .algo_data = &ip3203_i2cbus[1], // algo_data
       .nr = 1,
};
static struct i2c_adapter * ip3203_ops[NR_I2C_DEVICES] = { &ip3203_ops_0, &ip3203_ops_1 };


int __init i2c_algo_ip3203_init (void)
{
    int device;
    printk(/* KERN_INFO,*/ "i2c-ip3203: I2C IP3203 algorithm module\n");
    for (device = 0; device < NR_I2C_DEVICES; device++)
    {
        if (i2c_ip3203_add_bus(device, ip3203_ops[device]) < 0)
        {
            printk(/* KERN_INFO,*/"i2c-ip3203 %d: Unable to register with I2C\n", device);
            return -ENODEV;
        }
    }
    return 0;
}

void __exit i2c_algo_ip3203_exit(void)
{
        struct I2cBusObject *busptr;
        int device;

        for( device = 0; device < NR_I2C_DEVICES; device++ )
        {
                printk(/* KERN_INFO, */"exit bus %x\n", device);
                busptr = &ip3203_i2cbus[device];

                DISABLE_I2C_INTERRUPT( busptr );
                DISABLE_I2C_CONTROLLER( busptr ); /* Disable I2C controller */
                if (busptr->slv_enabled)
                {
                        busptr->slv_usr_notify = NULL;
                        busptr->slv_bufsize    = 0;
                        AAOUT( busptr, 0 ); /* ACK will not be sent after slave address reception */
                        busptr->slv_enabled = FALSE;

                        if (NULL != busptr->slv_buf)
                        {
                                kfree(busptr->slv_buf); // kfree returns void, no check needed
                        }
                        busptr->slv_bufindex  = 0;
                        busptr->slv_buflen    = 0;
                }

                disable_irq(busptr->int_pin);   /* Enable i2c interrupt in Interrupt controller */
                free_irq(busptr->int_pin, (void *)&ip3203_i2cbus[device]); // free_irq returns void, no check needed
                enable_irq(busptr->int_pin);   /* Enable i2c interrupt in Interrupt controller */

#ifdef USE_DMA
                kfree(busptr->dma_bufptr); // kfree returns void, no check needed
#endif
                i2c_ip3203_del_bus(ip3203_ops[device]);
        }
}

MODULE_AUTHOR("Willem van Beek <willem.van.beek@philips.com>");
MODULE_DESCRIPTION("I2C-Bus adapter routines for IP3203");
MODULE_LICENSE("GPL");

/* Called when module is loaded or when kernel is initialized.
 * If MODULES is defined when this file is compiled, then this function will
 * resolve to init_module (the function called when insmod is invoked for a
 * module).  Otherwise, this function is called early in the boot, when the
 * kernel is intialized.  Check out /include/init.h to see how this works.
 */
subsys_initcall(i2c_algo_ip3203_init);

/* Resolves to module_cleanup when MODULES is defined. */
module_exit(i2c_algo_ip3203_exit);
