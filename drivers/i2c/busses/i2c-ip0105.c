/* ------------------------------------------------------------------------- */
/* i2c-ip0105.c i2c driver algorithms for PNX8550 Fast I2C ports        */
/* ------------------------------------------------------------------------- */

/* This module is the work of Willem van Beek,
 * evidently inspired by Simon G. Vogl.
 *
 * Restriction :
 * Slave Transmitter functionality not working (yet)
 * No time-out (or watchdog) on Slave functionality
 *
 *
Rev Date        Author        Comments
--------------------------------------------------------------------------------
001             M Neill       Initial
....
006 20051122    raiyat        Linux 2.6.14.2 changes
007 20060908    laird         Linux 2.6.17.7 changes
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
#include <linux/fs.h>

/*
-------------------------------------------------------------------------------
Project include files:
-------------------------------------------------------------------------------
*/
#include <linux/i2c.h>
#include "i2c-ip0105.h"
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/mach-pnx8550/int.h>

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
#define NR_I2C_DEVICES 2 /* Two Fast I2C ports */

#define I2C_HW_ip0105               0x00 /* IP0105 Controller on the Viper */
#define FAST_I2C_PORT_3             0x00 /* IP0105 Controller on the Viper */
#define FAST_I2C_PORT_4             0x01 /* IP0105 Controller on the Viper */
#define IP0105_UNIT0                0xBBE69000
#define IP0105_UNIT1                0xBBE4C000

#define MODULE_CLOCK                27000 /* I2C module clock speed in KHz */

#define I2CADDRESS( address )       ((address) & 0x00FE) /* only 7 bits I2C address implemented */
#define TIMER_OFF                   0


typedef enum
{
    Idle = 0u,
    MasterTransmitter,
    MasterReceiver,
    SlaveTransmitter,
    SlaveReceiver
} I2cMode;

struct I2cBusObject
{
    unsigned char  * mst_txbuf; /* Master mode variables */
    int     mst_txbuflen;
    int     mst_txbufindex;
    unsigned char  * mst_rxbuf;
    int     mst_rxbuflen;
    int     mst_rxbufindex;
    int     mst_address;
    wait_queue_head_t iic_wait_master;
        unsigned long int_pin;
    unsigned int mst_status;
//    int     mst_timer;
//    int     mst_timeout;
//    BOOL    mst_timeout_valid;

    unsigned char    * slv_buf; /* Slave mode variables */
    int     slv_bufsize;
    int     slv_buflen;
    int     slv_bufindex;
    BOOL    slv_enabled;
//    int     slv_timer;
//    int     slv_timeout;
//    BOOL    slv_timeout_valid;

    int     offset; /* I2C HW controller address offset, required by HAL */
    I2cMode mode;
    int     isr_event;
//    BOOL    bus_blocked;
    struct  fasync_struct ** slv_usr_notify;
};

/* declaration of static functions */

/* Local functions for IP0105 */

static __inline void STAOUT(struct I2cBusObject * a, int p)
{
        int val = READ_IP0105_I2C_CONTROL(a);
        if (p) { val |= IP0105_STA; } else { val &= ~IP0105_STA; }
        WRITE_IP0105_I2C_CONTROL(a, val);
}
static __inline void STOOUT(struct I2cBusObject * a, int p)
{
        WRITE_IP0105_I2C_STOP(a, p);
}
static __inline void AAOUT(struct I2cBusObject * a, int p)
{
        int val = READ_IP0105_I2C_CONTROL(a);
        if (p) { val |= IP0105_AA; } else { val &= ~IP0105_AA; }
        WRITE_IP0105_I2C_CONTROL(a, val);
}
static __inline void ENABLE_I2C_CONTROLLER(struct I2cBusObject * a)
{
        int val = READ_IP0105_I2C_CONTROL(a);

        val |= IP0105_EN;
        WRITE_IP0105_I2C_CONTROL(a, val);
}
static __inline void DISABLE_I2C_CONTROLLER(struct I2cBusObject * a)
{
        int val = READ_IP0105_I2C_CONTROL(a);

        val &= ~IP0105_EN;
        WRITE_IP0105_I2C_CONTROL(a, val);
}
static __inline void CLEAR_I2C_INTERRUPT(struct I2cBusObject * a)
{
        WRITE_IP0105_I2C_INT_CLR(a, 1);
}
static __inline void ENABLE_I2C_INTERRUPT(struct I2cBusObject * a)
{
        WRITE_IP0105_I2C_INT_EN(a, IP0105_INTBIT);
}
static __inline void DISABLE_I2C_INTERRUPT(struct I2cBusObject * a)
{
        WRITE_IP0105_I2C_INT_EN(a, 0);
}

static int i2c_debug = 7;
/* ----- global defines ----------------------------------------------- */
#define DEB(x) if (i2c_debug>=1) x
#define DEB2(x) if (i2c_debug>=2) x
#define DEB3(x) if (i2c_debug>=3) x /* print several statistical values*/
#define ASSERT(x) if (!(x)) dev_dbg(&i2c_adap->dev, "ASSERTION FAILED at line %d in file %s\n", __LINE__, __FILE__);
/* Types and defines: */

#define TIMEOUT     200 /* Should be wait period of >100us i.e 1 byte @100KHz */


static __inline void SETSPEED100(struct I2cBusObject * a)
{
        int val = READ_IP0105_I2C_CONTROL(a);

        val &= 0x00F0;
        val |= 0x00F4;
        WRITE_IP0105_I2C_CONTROL(a, val);
}


static __inline void SETSPEED25(struct I2cBusObject * a)
{
        int val = READ_IP0105_I2C_CONTROL(a);

        val |= 0x00F7;
        WRITE_IP0105_I2C_CONTROL(a, val);
}

static __inline void SETSPEED400(struct I2cBusObject * a)
{
        int val = READ_IP0105_I2C_CONTROL(a);

        val |= 0x00F0;
        WRITE_IP0105_I2C_CONTROL(a, val);
}


/* Local Types */

static unsigned long IP0105_Controller[2] = {  IP0105_UNIT0, IP0105_UNIT1 };
static unsigned long IP0105_INTPIN[2] = { PNX8550_INT_I2C3, PNX8550_INT_I2C4 };

static struct i2c_adapter * slave_device = NULL;

static void do_slave_tasklet(unsigned long);
DECLARE_TASKLET(ip0105_slave_tasklet, do_slave_tasklet, 0);

static struct I2cBusObject ip0105_i2cbus[NR_I2C_DEVICES];

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
                disable_irq(busptr->int_pin);
                busptr->mst_status = errids_IsI2cHardwareError;
                busptr->isr_event |= evMasterReady;
                enable_irq(busptr->int_pin);  //isv_SwExtSet( INTERRUPT_ID( device ) );
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
                        WRITE_IP0105_I2C_DAT(busptr, myaddr); /* Read bit cleared */
                        busptr->mode = MasterTransmitter;
                }
                else
                {
                        int myaddr = ((busptr->mst_address & 0xFE) | 0x01);
                        dev_dbg(&i2c_adap->dev, "START TRANSMITTED, Read bit set(%x)\n", myaddr);
                        WRITE_IP0105_I2C_DAT(busptr, myaddr); /* Read bit set */
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
//                tasklet_schedule(&ip0105_slave_tasklet);
                CLEAR_I2C_INTERRUPT(busptr); /* Don't reset i2c interrupt */
                break;
        case 0xA8: /* Own SLA+R has been received; ACK has been returned */
                busptr->mode = SlaveTransmitter;
                busptr->isr_event |= evSlaveReadRequest;
                slave_device = i2c_adap;
                tasklet_schedule(&ip0105_slave_tasklet);
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
                        WRITE_IP0105_I2C_DAT(busptr, myaddr); /* Read bit cleared */
                        busptr->mode = MasterTransmitter;
                }
                else
                {
                        int myaddr = ((busptr->mst_address & 0xFE) | 0x01);
                        dev_dbg(&i2c_adap->dev, "START TRANSMITTED, Read bit set(%x)\n", myaddr);
                        WRITE_IP0105_I2C_DAT(busptr, myaddr); /* Read bit set */
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
                        WRITE_IP0105_I2C_DAT(busptr, *( busptr->mst_txbuf + busptr->mst_txbufindex ));
                        busptr->mst_txbufindex++;
                        STAOUT( busptr, 0 ); /* Don't generate (repeated) START condition */
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
//                tasklet_schedule(&ip0105_slave_tasklet);
                CLEAR_I2C_INTERRUPT(busptr); /* Don't reset i2c interrupt */
                break;

        case 0xB0: /* Arbitration lost in SLA+R/~W as master; Own SLA+R has been received, ACK has been returned */
                STAOUT( busptr, 1 ); /* Retry the master operation */
                busptr->mst_txbufindex = 0;
                busptr->mst_rxbufindex = 0;
                busptr->mode = SlaveTransmitter;

                busptr->isr_event |= evSlaveReadRequest;
                slave_device = i2c_adap;
                tasklet_schedule(&ip0105_slave_tasklet);
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
                        *( busptr->mst_rxbuf + busptr->mst_rxbufindex++ ) = READ_IP0105_I2C_DAT(busptr);
                }
                else
                {
                        (void)READ_IP0105_I2C_DAT(busptr); /* Ignore received byte, no storage space available */
                }
                /* fall through; */
        case 0x40: /* SLA+R has been transmitted; ACK has been received */
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
                        *( busptr->mst_rxbuf + busptr->mst_rxbufindex++ ) = READ_IP0105_I2C_DAT(busptr);
dev_dbg(&i2c_adap->dev, "received byte %x\n", *(busptr->mst_rxbuf + busptr->mst_rxbufindex -1));
                }
                else
                {
                        (void)READ_IP0105_I2C_DAT(busptr);
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
//                tasklet_schedule(&ip0105_slave_tasklet);
                CLEAR_I2C_INTERRUPT(busptr); /* Don't reset i2c interrupt */
                break;

        case 0xB0: /* Arbitration lost in SLA+R/~W as master; Own SLA+R has been received, ACK has been returned */
                STAOUT( busptr, 1 ); /* Retry the master operation */
                busptr->mst_txbufindex = 0;
                busptr->mst_rxbufindex = 0;

                busptr->mode = SlaveReceiver;
                busptr->isr_event |= evSlaveReadRequest;
                slave_device = i2c_adap;
                tasklet_schedule(&ip0105_slave_tasklet);
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
                        *( busptr->slv_buf + busptr->slv_bufindex++ ) = READ_IP0105_I2C_DAT(busptr);
                }
                else
                {
                        (void)READ_IP0105_I2C_DAT(busptr);
                }
                AAOUT( busptr, busptr->slv_enabled ); /* ACK bit will be returned, or not, after next byte reception */

                CLEAR_I2C_INTERRUPT( busptr );  /* Reset i2c interrupt */
				break;

        case 0x88: /* Previously addressed with own SLA; DATA byte has been received; NOT ACK has been returned */
                if ( busptr->slv_bufindex < busptr->slv_bufsize )
                {
                        *( busptr->slv_buf + busptr->slv_bufindex++ ) = READ_IP0105_I2C_DAT(busptr);
                }
                else
                {
                        (void)READ_IP0105_I2C_DAT(busptr);
                }

                AAOUT( busptr, busptr->slv_enabled ); /* ACK bit will be returned, or not, after slave address reception */
                CLEAR_I2C_INTERRUPT( busptr );    /* reset i2c interrupt */
				break;

        case 0xA0: /* A STOP condition or repeated START has been received while still address as SLV/REC or SLV/TRX */
				// Slave will be enabled again when data is out of the buffer
                AAOUT( busptr, 0 ); /* NOT ACK bit will be returned after slave address reception */

                busptr->isr_event |= evSlaveStopCondition;
                slave_device = i2c_adap;
                tasklet_schedule(&ip0105_slave_tasklet);
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
                tasklet_schedule(&ip0105_slave_tasklet);
                CLEAR_I2C_INTERRUPT( busptr );   /* Reset i2c interrupt */
                break;

        case 0xB8: /* Data byte in I2DAT has been transmitted; ACK has been received */
                if ( busptr->slv_bufindex < busptr->slv_bufsize )
                {
                        WRITE_IP0105_I2C_DAT(busptr, *( busptr->slv_buf + busptr->slv_bufindex ));
                        busptr->slv_bufindex++;

                        if ( busptr->slv_bufindex < busptr->slv_buflen )
                        {
                                AAOUT( busptr, 1 ); /* Data byte will be transmitted */
                        }
                        else
                        {
                                AAOUT( busptr, 0 ); /* Last data byte will be transmitted */
                        }
                        CLEAR_I2C_INTERRUPT( busptr ); /* Reset interrupt */
                }
                else
                {
                        busptr->isr_event |= evSlaveReadRequest;
                        slave_device = i2c_adap;
                        tasklet_schedule(&ip0105_slave_tasklet);
                        /* Don't reset i2c interrupt */
                }
                break;

        case 0xC0: /* Data byte in I2DAT has been transmitted; NOT ACK has been received */
				/* fall through; */
        case 0xC8: /* Last data byte in I2DAT has been transmitted (AA=0); ACK has been received */
                AAOUT( busptr, busptr->slv_enabled ); /* ACK bit will be returned, or not, after slave address reception */

                busptr->isr_event |= evSlaveTransmitterReady;
                slave_device = i2c_adap;
                tasklet_schedule(&ip0105_slave_tasklet);
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

static irqreturn_t ip0105_iic_interrupt(int irq, void *dev_id)
{
		// get the BusObject
		struct i2c_adapter * i2c_adap = (struct i2c_adapter *)dev_id;
        struct I2cBusObject * busptr = (struct I2cBusObject *)i2c_adap->algo_data;
        int i2c_state;

        // Get State of Iic Bus
        i2c_state = READ_IP0105_I2C_STATUS(busptr);
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

#define WAIT_ip0105INT()                do{ int t = TIMEOUT;                                       \
                                            while(!(READ_IP0105_I2C_INT_STATUS(busptr) & IP0105_INTBIT)     \
                                                   && (t-->0)){}                               \
                                          }while(0)

#define WAIT_IP0105_STO_OR_INT()        do{ int t = TIMEOUT;                                       \
                                            while(!(READ_IP0105_I2C_INT_STATUS(busptr) & IP0105_INTBIT)     \
                                                 &&(READ_IP0105_I2C_CONTROL(busptr) & IP0105_STO)      \
                                                 &&(t-->0)){}                                  \
                                          }while(0)

/******************************************************************************
*   This function resets IP0105. The parameter "i2c_adap" indicates base address
*   of the IP0105 Block.
*******************************************************************************/
static void ip0105_reset(struct i2c_adapter *i2c_adap)
{
    struct I2cBusObject * busptr = (struct I2cBusObject *)i2c_adap->algo_data;

    dev_dbg(&i2c_adap->dev, "Reset the IP0105\n");

    DISABLE_I2C_INTERRUPT(busptr);
    AAOUT(busptr, 0);
    STAOUT(busptr, 0);

    if(1 /*READ_IP0105_INTROG(busptr) & IP0105_INTRO_BB*/)
    {   /* Bus is busy */
        dev_dbg(&i2c_adap->dev, "I2C bus is busy\n");
        WAIT_ip0105INT();
dev_dbg(&i2c_adap->dev, "int_status = 0x%x\n", READ_IP0105_I2C_INT_STATUS(busptr));
        if(READ_IP0105_I2C_INT_STATUS(busptr) & IP0105_INTBIT)
        {/* Interrupt flag is set */
            unsigned int i2c_state = READ_IP0105_I2C_STATUS(busptr);
            if((i2c_state == 0x08) || (i2c_state == 0x10))
            {
                WRITE_IP0105_I2C_DAT(busptr, 0xEE);    /*  Transmit dummy address */
                CLEAR_I2C_INTERRUPT(busptr);           /* Clear I2C interrupt    */
            }
            else if((i2c_state == 0x40) || (i2c_state == 0x50))
            {   /* One byte must be read which should be NACKed */
                AAOUT(busptr, 0);                      /*  NACK next byte      */
                CLEAR_I2C_INTERRUPT(busptr);           /*  Clear I2C interrupt    */
            }
            else
            {
                /* For rest of the states just generating stop condition is enough */
            }
            WAIT_ip0105INT();
            STOOUT(busptr, 1);                         /*  Generate stop condition */
            CLEAR_I2C_INTERRUPT(busptr);               /*  Clear I2C interrupt: Not necessary but no harm */
            WAIT_IP0105_STO_OR_INT();
        }
        else
        {/* Interrupt flag did not set, May be due to clock stretching */
            STOOUT(busptr, 1);                         /*  Generate stop condition */
            CLEAR_I2C_INTERRUPT(busptr);               /*  Clear I2C interrupt: Not necessary but no harm */
            WAIT_IP0105_STO_OR_INT();
        }
    }
    else
    { /* Bus is free, do nothing */
        dev_dbg(&i2c_adap->dev, "I2C bus is free\n");
//        STOOUT(busptr, 1);                           /*  Generate stop condition */
//        CLEAR_I2C_INTERRUPT(busptr);                 /* Clear I2C interrupt: Not necessary but no harm */
//        WAIT_IP0105_STO_OR_INT();
    }

    if(READ_IP0105_I2C_CONTROL(busptr) & IP0105_STO)
    {
        ASSERT(FALSE); /* Could not free I2C bus */
    }

    /* Set default values */
    DISABLE_I2C_CONTROLLER(busptr);      /*  Disable I2C controller */
    DISABLE_I2C_INTERRUPT(busptr);       /*  Disable I2C interrupts */
    AAOUT(busptr, 0);                    /*  Disable slave mode     */
    STAOUT(busptr, 0);                   /*  Remove start request   */
    CLEAR_I2C_INTERRUPT(busptr);         /*  Clear I2C interrupt    */

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

static void ip0105_init(struct i2c_adapter * i2c_adap, int device)
{
        struct I2cBusObject *busptr;

        dev_dbg(&i2c_adap->dev, "init I2C bus %x\n", i2c_adap->nr);
        busptr                    = &ip0105_i2cbus[device];
        busptr->offset            = IP0105_Controller[device];
        busptr->int_pin           = IP0105_INTPIN[device];
        busptr->mode              = Idle;
        busptr->isr_event         = 0; /* Clear all event flags(bits) */
//        busptr->bus_blocked       = FALSE;

        busptr->slv_enabled       = FALSE;
        busptr->slv_buf           = NULL; // SlaveBuffer[device];
        busptr->slv_bufindex      = 0;
        busptr->slv_buflen        = 0;
        busptr->slv_bufsize       = 0; // SLAVE_BUF_SIZE;
        busptr->slv_usr_notify    = NULL;

//        busptr->mst_timer         = TIMER_OFF;
//        busptr->mst_timeout       = TIMER_OFF;
//        busptr->mst_timeout_valid = FALSE;
//        busptr->slv_timer         = TIMER_OFF;
//        busptr->slv_timeout       = TIMER_OFF;
//        busptr->slv_timeout_valid = FALSE;

        /* The remaining elements of the struct are initialized when i2c write,read functions are called */
        init_waitqueue_head(&(busptr->iic_wait_master));

        DISABLE_I2C_CONTROLLER( busptr ); /* Disable I2C controller */

        SETSPEED100(busptr);

        AAOUT( busptr, 0 ); /* Slave mode disabled */
        STOOUT( busptr, 0 ); /* Do not generate stop condition */
        STAOUT( busptr, 0 ); /* Do not generate Start condition */

        /* Install interrupt handler. */
        // Todo : find out which pin I2c interrupt line
        if (0 != request_irq(busptr->int_pin, ip0105_iic_interrupt, SA_INTERRUPT, "i2c", (void *)i2c_adap))
        {
            ASSERT( 0 );
        }
        CLEAR_I2C_INTERRUPT( busptr ); /* Clear the interrupt flags if by-chance(if not power up ) they are set */
        disable_irq(busptr->int_pin);   /* disable i2c interrupt in Interrupt controller */
        enable_irq(busptr->int_pin);   /* Enable i2c interrupt in Interrupt controller */

        ENABLE_I2C_CONTROLLER( busptr ); /* Enable I2C Controller */
        ENABLE_I2C_INTERRUPT( busptr); /* Enable  both the SI and DMA interrupts */

        return;
}

/* Master ReadWrite interface function */
static unsigned int i2c_write_read(struct i2c_adapter * i2c_adap, int address, void *msgwram, int lenw, void *msgr, int lenr )
{
        struct I2cBusObject * busptr = (struct I2cBusObject *)i2c_adap->algo_data;

        int retval = 0;
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

        STAOUT( busptr, 1 ); /* Generate START condition when selected bus is free and start interrupt driven machine */
        /* Suspend current task till the event flag is set */
        /* Wait for IIC transfer, result of action is written in I2cBusObject */
        retval = interruptible_sleep_on_timeout(&(busptr->iic_wait_master), (5*HZ));
        if (retval == 0)
        {
                dev_dbg(&i2c_adap->dev, "I2C Status 0%x, int stat %x\n",
                          READ_IP0105_I2C_STATUS(busptr),
                          READ_IP0105_I2C_INT_STATUS(busptr));
				pr_info("I2C0105 timeout\n");
                return errids_IsI2cHardwareError;
        }

        return busptr->mst_status;

}

static int ip0105_xfer(struct i2c_adapter *i2c_adap, struct i2c_msg msgs[], int num)
{
        struct i2c_msg *pmsg;
        void * wr_buf = NULL;
        void * rd_buf = NULL;
        int i, wr_len = 0, rd_len = 0;
        unsigned char addr = msgs[0].addr;
        int ret=0;

        dev_dbg(&i2c_adap->dev, "IP0105 xfer nr %d\n", num);
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
                        ip0105_reset(i2c_adap);
                        break;
                case errids_IsI2cDeviceError:
                        num = -1;
                        dev_dbg(&i2c_adap->dev, "Device error\n");
                        ip0105_reset(i2c_adap);
                        break;
                case errids_IsI2cWriteError:
                        num = -1;
                        dev_dbg(&i2c_adap->dev, "Write error\n");
                        ip0105_reset(i2c_adap);
                        break;
                default:
                        num = -1;
                        dev_dbg(&i2c_adap->dev, "Error Unkonwn\n");
                        ip0105_reset(i2c_adap);
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
                        WRITE_IP0105_I2C_ADDRESS( busptr, I2CADDRESS( arg )); /* >> 1 */; /* 7 bits address, No General call support */
dev_dbg(&i2c_adap->dev, "Set Own adress to %x\n", READ_IP0105_I2C_ADDRESS(busptr));
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

static u32 ip0105_func(struct i2c_adapter *adap)
{
        return I2C_FUNC_SMBUS_EMUL | I2C_FUNC_PROTOCOL_MANGLING | I2C_FUNC_I2C;
}

/* -----exported algorithm data: -------------------------------------  */

static struct i2c_algorithm ip0105_algo_0 = {
        .master_xfer = ip0105_xfer,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
        .algo_control = algo_control,                   /* ioctl                */
#endif
        .functionality = ip0105_func,                     /* functionality        */
};
static struct i2c_algorithm ip0105_algo_1 = {
        .master_xfer = ip0105_xfer,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
        .algo_control = algo_control,
#endif
        .functionality = ip0105_func,
};
static struct i2c_algorithm * ip0105_algo[NR_I2C_DEVICES] = { &ip0105_algo_0, &ip0105_algo_1 };
/*
 * registering functions to load algorithms at runtime
 */
int i2c_ip0105_add_bus(int device, struct i2c_adapter * i2c_adap)
{
        int res;
        dev_dbg(&i2c_adap->dev, "i2c-ip0105: I2C IP0105 algorithm module\n");

        DEB2(printk("i2c-ip0105: HW routines for %s registered.\n", i2c_adap->name));

        /* register new adapter to i2c module... */
        i2c_adap->algo = ip0105_algo[device];
        i2c_adap->timeout = 100;            /* default values, should       */
        i2c_adap->retries = 3;              /* be replaced by defines       */
        dev_dbg(&i2c_adap->dev, "initialise IP0105 device %d\n", device);
        ip0105_init(i2c_adap, device);

        if ((res = i2c_add_numbered_adapter(i2c_adap)) < 0)
        {
                dev_dbg(&i2c_adap->dev, "i2c-ip0105 %d: Unable to register with I2C\n", device);
                return -ENODEV;
        }
        dev_dbg(&i2c_adap->dev, "add adapter %d returned %x\n", i2c_adap->nr, res);
        /* Todo : scan bus */

        return 0;
}

int i2c_ip0105_del_bus(struct i2c_adapter *i2c_adap)
{
        struct I2cBusObject *busptr;
        int res;

        dev_dbg(&i2c_adap->dev, "exit bus\n");
        busptr = (struct I2cBusObject *)i2c_adap->algo_data;

        if ((res = i2c_del_adapter(i2c_adap)) < 0)
                return res;
        DEB2(dev_dbg(&i2c_adap->dev, "i2c-ip0105: adapter unregistered: %s\n",i2c_adap->name));

        return 0;
}

static struct i2c_adapter ip0105_ops_0 = {
       .name = "IP0105 0",                        // name
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31)
       .id = FAST_I2C_PORT_3,                // id
#endif
       .algo_data = &ip0105_i2cbus[0], // algo_data
       .nr = 2,
};
static struct i2c_adapter ip0105_ops_1 = {
       .name = "IP0105 1",
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31)
       .id = FAST_I2C_PORT_4,
#endif
       .algo_data = &ip0105_i2cbus[1], // algo_data
       .nr = 3,
};
static struct i2c_adapter * ip0105_ops[NR_I2C_DEVICES] = { &ip0105_ops_0, &ip0105_ops_1 };


int __init i2c_algo_ip0105_init (void)
{
    int device;
    printk("i2c-ip0105: I2C IP0105 algorithm module\n");
    for (device = 0; device < NR_I2C_DEVICES; device++)
    {
        if (i2c_ip0105_add_bus(device, ip0105_ops[device]) < 0)
        {
            printk("i2c-ip0105 %d: Unable to register with I2C\n", device);
            return -ENODEV;
        }
    }
    return 0;
}

void __exit i2c_algo_ip0105_exit(void)
{
    struct I2cBusObject *busptr;
    int device;

    for( device = 0; device < NR_I2C_DEVICES; device++ )
    {
        printk("exit bus %x\n", device);
        busptr = &ip0105_i2cbus[device];

        DISABLE_I2C_INTERRUPT( busptr );
        DISABLE_I2C_CONTROLLER( busptr ); /* Disable I2C controller */

        disable_irq(busptr->int_pin);   /* Enable i2c interrupt in Interrupt controller */
        free_irq(busptr->int_pin, (void *)&ip0105_i2cbus[device]);
        enable_irq(busptr->int_pin);   /* Enable i2c interrupt in Interrupt controller */

        i2c_ip0105_del_bus(ip0105_ops[device]);
    }
}

MODULE_AUTHOR("Willem van Beek <willem.van.beek@philips.com>");
MODULE_DESCRIPTION("I2C-Bus adapter routines for IP0105");
MODULE_LICENSE("GPL");

/* Called when module is loaded or when kernel is initialized.
 * If MODULES is defined when this file is compiled, then this function will
 * resolve to init_module (the function called when insmod is invoked for a
 * module).  Otherwise, this function is called early in the boot, when the
 * kernel is intialized.  Check out /include/init.h to see how this works.
 */
subsys_initcall(i2c_algo_ip0105_init);

/* Resolves to module_cleanup when MODULES is defined. */
module_exit(i2c_algo_ip0105_exit);
