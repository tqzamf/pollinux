// Roku Pandora2 Infra-red driver
//
// This driver deals with Infra-red receiver on the Pandora2 board. It
// is based on the empeg (http://www.empeg.com) input driver.
//
// (C) 2007 Roku LLC
// (C) 1999 empeg ltd
//
// Authors:
//    Mike Crowe <mcrowe@rokulabs.com>
//    Mike Crowe <mac@empeg.com>
//
// This driver is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published
// by the Free Software Foundation; either version 2 of the License,
// or (at your option) any later version.
//
// This driver is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this driver; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
// 02110-1301 USA. Alternatively visit http://www.gnu.org/licenses/.
//

#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/sem.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/vmalloc.h>
#include <linux/timer.h>
#include <linux/poll.h>

#include <asm/irq.h>
#include <asm/io.h>
#include <asm/mach-pnx8550/gpio.h>
#include <asm/mach-pnx8550/int.h>

MODULE_AUTHOR("Mike Crowe <mcrowe@rokulabs.com>");
MODULE_DESCRIPTION("Roku Pandora2 Infra-red driver");
MODULE_LICENSE("GPL");

#define MODULE_NAME "roku-pandora2-ir"

#define IR_DEBUG 0

#define MS_TO_JIFFIES(MS) ((MS)/(1000/HZ))
#define JIFFIES_TO_MS(J) (((J)*1000)/HZ)

#define IR_REPEAT_TIMEOUT MS_TO_JIFFIES(500) /* 0.5 seconds */
#define IR_BUTTON_UP_TIMEOUT MS_TO_JIFFIES(150) /* .15 seconds */
#define IR_BUFFER_SIZE (128)

// The datasheet claims that the timestamping clock runs at 13.5MHz (108/8). My testing implies that it actually runs at
// 3.375MHz (108/32).
#define US_TO_TICKS(x) (3375*(x)/1000)
#define TICKS_TO_US(x) (1000*(x)/3375)

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define IR_TSU_CHANNEL 0
#define IR_INTERRUPT ((IR_TSU_CHANNEL) < 8 ? PNX8550_INT_GPIO_TSU_7_0 : PNX8550_INT_GPIO_TSU_15_8)
#define IR_GPIO 40

typedef u32 input_code;

struct ir_device
{
	dev_t devt;
	struct semaphore sem;
	wait_queue_head_t wq;

	struct timer_list timer;

	input_code current_button_down;
	input_code last_code_received;
	
	input_code *buf_start;
	input_code *buf_end;
	input_code *buf_wp;
	input_code *buf_rp;

	unsigned long last_ir_jiffies;

	u32 count_missed;
	u32 count_repeat;
	u32 count_badrepeat;
	u32 count_spurious;
	u32 count_valid;
	u32 count_malformed;
};

static int ir_open_count = 0;

/* Work out how many jiffies have passed since the parameter. This
 * means that if past_jiffies is actually in the future it will appear
 * to be hugely in the past. */
static inline unsigned long jiffies_since(unsigned long past_jiffies)
{
	/* Since jiffies is volatile we need to make sure we are using
         * a consistent value for it for the whole function. */
	const unsigned long now_jiffies = jiffies;
	if (past_jiffies <= now_jiffies) {
		/* Simple case */
		return now_jiffies - past_jiffies;
	} else {
		/* Wrap around case */
		return ULONG_MAX - past_jiffies + now_jiffies;
	}
}

//
// BUTTON and QUEUE MANAGEMENT
//

static void ir_append(struct ir_device *dev, u32 code)
{
	input_code *new_wp;

	down(&dev->sem);
	new_wp = dev->buf_wp + 1;
	if (new_wp == dev->buf_end)
		new_wp = dev->buf_start;

	// If we're not full
	if (new_wp != dev->buf_rp)
	{
		*dev->buf_wp = code;
		dev->buf_wp = new_wp;
		/* Now we've written, wake up anyone who's reading */
		wake_up_interruptible(&dev->wq);
	}
#if IR_DEBUG
	else
		printk(KERN_WARNING "Infra-red buffer is full.\n");
#endif
	up(&dev->sem);
}

static void ir_button_up(struct ir_device *dev)
{
	if (dev->current_button_down) {
		ir_append(dev, (1 << 31) | dev->current_button_down);
		dev->current_button_down = 0;
	}
}

static void ir_button_down(struct ir_device *dev, input_code code)
{
	dev->current_button_down = code;
	dev->last_code_received = code;
	ir_append(dev, code);
}

static void ir_on_code(struct ir_device *dev, u32 code)
{
	dev->last_ir_jiffies = jiffies;
	if (dev->current_button_down != code)
		ir_button_up(dev);

	ir_button_down(dev, code);
	
	// Set a timer to wake up in case nothing else happens now.
	mod_timer(&dev->timer, dev->last_ir_jiffies + IR_BUTTON_UP_TIMEOUT);	
}

static void ir_on_repeat(struct ir_device *dev)
{
	dev->last_ir_jiffies = jiffies;
	
	if (dev->current_button_down == 0) {
		/* We got a repeat code but nothing was held, if it
	           wasn't too long ago assume that we accidentally
	           sent a button up and send another button down to
	           compensate. */
		if (jiffies_since(dev->last_ir_jiffies) < IR_REPEAT_TIMEOUT) {
			ir_button_down(dev, dev->last_code_received);
		}
	}

	// We know that things are happening so defer the timer.
	mod_timer(&dev->timer, dev->last_ir_jiffies + IR_BUTTON_UP_TIMEOUT);
}

//
// IR ALGORITHM
//

static void ir_transition(struct ir_device *dev, u32 span, u8 level)
{
	enum {
		IR_STATE_IDLE = 0x00,
		IR_STATE_START1 = 0x02,
		IR_STATE_START2 = 0x04,
		IR_STATE_START3 = 0x06,
		IR_STATE_DATA1  = 0x08,
		IR_STATE_DATA2 = 0x0a,
	};

	static u32 state = IR_STATE_IDLE;
	static u32 unit_time = 1; // For calibration.
	static u8 bit_position = 0;
	static u32 data = 0;
	static int repeat_valid = FALSE;
	
	if (span >= US_TO_TICKS(40000)) {
		bit_position = 0;
		state = IR_STATE_IDLE;
	}

	/* Now we can actually do something */

 retry:
	switch(state | (level ? 1 : 0))
	{
	case IR_STATE_IDLE | 1:
		/* Going high in idle doesn't mean anything */
		break;
		
	case IR_STATE_IDLE | 0:
		/* Going low in idle is the start of a start sequence */
		state = IR_STATE_START1;
		break;

	case IR_STATE_START1 | 1:
		/* Going high, that should be after 8T */
		/* But some bounds on it so we don't start receiving magic
		   codes from the sky. This should be about 9ms */
		if (span > US_TO_TICKS(8000) && span < US_TO_TICKS(10000)) {
			unit_time = span / 8;
			state = IR_STATE_START2;
		} else
			state = IR_STATE_IDLE;
		break;

	case IR_STATE_START1 | 0:
		/* Shouldn't ever go low in START1, recover */
		state = IR_STATE_IDLE;
		++dev->count_missed;
		break;

	case IR_STATE_START2 | 1:
		/* Shouldn't ever go low in START2, recover */
		state = IR_STATE_IDLE;
		++dev->count_missed;
		break;

	case IR_STATE_START2 | 0:
		/* If this forms the end of the start sequence then we
		 * should have been high for around 4T time.
		 */
		if ((span >= 3 * unit_time) &&
			(span < 5 * unit_time)) {
			state = IR_STATE_START3;
			/*repeat_valid = FALSE;*/
		} else if (span > unit_time && span < 3 * unit_time) {
			/* This means that the last code is repeated - just
			   send out the last code again with the top bit set to indicate
			   a repeat. */
			if (repeat_valid)
			{
				ir_on_repeat(dev);
				++dev->count_repeat;
			}
			else
			{
				++dev->count_badrepeat;
			}
			state = IR_STATE_IDLE;
		} else {
			/* We're out of bounds. Recover */
			state = IR_STATE_IDLE;
			/* But it could be the start of a new sequence
                           so try again */
			++dev->count_spurious;
			goto retry;
		}
		break;

	case IR_STATE_START3 | 0:
		/* Shouldn't happen */
		state = IR_STATE_IDLE;
		++dev->count_missed;
		break;

	case IR_STATE_START3 | 1:
		/* Data will follow this */
		if (span < unit_time) {
			bit_position = 0;
			data = 0;
			state = IR_STATE_DATA1;
		} else {
			/* We're out of bounds. It might be the start
			   of a new sequence so try it again. */
			state = IR_STATE_IDLE;
			++dev->count_spurious;
			goto retry;
		}
		break;
		
	case IR_STATE_DATA1 | 1:
		/* Shouldn't get this. Recover */
		state = IR_STATE_IDLE;
		++dev->count_missed;
		break;

	case IR_STATE_DATA1 | 0:
		/* The actual data bit is encoded in the length of this.
		 */
		if (span < unit_time) {
			/* It's a zero */
			bit_position++;
			state = IR_STATE_DATA2;
		} else if (span < 2 * unit_time) {
			/* It's a one */
			data |= (1<<bit_position);
			bit_position++;
			state = IR_STATE_DATA2;
		} else {
			/* Not valid. It might be the start of a new
                           sequence though. */
			state = IR_STATE_IDLE;
			++dev->count_spurious;
			goto retry;
		}
		break;

	case IR_STATE_DATA2 | 1:
		/* This marks the end of the post-data space
		 * It is a consistent length
		 */
		if (span < unit_time) {
			/* It's a valid space */
			if (bit_position >= 32) {
				u16 mfr;
				u8 data1, data2;
				u32 cpu_data = be32_to_cpu(data);

				// On modern remotes the manufacturer code is not protected.
				mfr = cpu_data >> 16;
				data1 = (cpu_data >> 8) & 0xff;
				data2 = cpu_data & 0xff;

				/* We've finished getting data, confirm
				   it passes the validity check */
				if (data1 == ((u8)(~data2))) {
					u32 decoded_data;
					decoded_data = ((u32)(mfr) << 8) | data1;
					ir_on_code(dev, decoded_data);
					repeat_valid = TRUE;
					++dev->count_valid;
				} else {
#if IR_DEBUG
					printk("Got an invalid sequence %08lx (%04lx, %04lx)\n",
					       (unsigned long)data, (unsigned long)data1,
					       (unsigned long)data2);
					printk("(%04lx %04lx, %04lx %04lx)\n",
					       (unsigned long)data1, (unsigned long)~data1,
					       (unsigned long)data2, (unsigned long)~data2);
#endif
					++dev->count_malformed;
				}
				state = IR_STATE_IDLE;
			}
			else
				state = IR_STATE_DATA1;
		} else {
			/* It's too long to be valid. Give up and try again. */
			state = IR_STATE_IDLE;
			++dev->count_spurious;
			goto retry;
		}
		break;

	case IR_STATE_DATA2 | 0:
		/* Shouldn't get this. Recover */
		state = IR_STATE_IDLE;
		++dev->count_missed;
		break;

	default:
		state = IR_STATE_IDLE;
		break;
	}
}
	

static irqreturn_t ir_interrupt(int cpl, void *private)
{
	// This interrupt is raised for data valid (low byte) or overflow (high byte)
	const unsigned long interrupt_mask = (1<<(IR_TSU_CHANNEL & 7)) | ((1<<(IR_TSU_CHANNEL & 7)) << 8);
	
	if ((MMIO(TSU_INT_STATUS(IR_TSU_CHANNEL)) & interrupt_mask) == 0) {
		return IRQ_NONE;
	} else {
		struct ir_device *dev = private;
		
		u32 data = MMIO(TSU_DATA(IR_TSU_CHANNEL));
		
		static u32 last_event = 0;
		u8 edge = (data & (1<<31)) ? 1 : 0;
		u32 absolute = data & ~(1<<31);
		u32 interval = absolute - last_event;
		last_event = absolute;
		
		ir_transition(dev, interval, edge);
		
		// Acknowledge the interrupt
		MMIO(TSU_INT_CLEAR(IR_TSU_CHANNEL)) = interrupt_mask;
		return IRQ_HANDLED;
	}
}

static void ir_timer(unsigned long data)
{
	struct ir_device *dev = (struct ir_device *)data;

	// If there's no button down then we have nothing to do.
	if (dev->current_button_down == 0)
		return;
	
	/* If we haven't had a repeat code for a while send a button up. */
	if (jiffies_since(dev->last_ir_jiffies) >= IR_BUTTON_UP_TIMEOUT)
		ir_button_up(dev);
	else
	{
		// We were woken up unnecessarily: reschedule
		if (likely(ir_open_count)) {
			mod_timer(&dev->timer, dev->last_ir_jiffies + IR_BUTTON_UP_TIMEOUT);
		}
	}
}

static struct ir_device ir_dev;

static int ir_open(struct inode *inode, struct file *file)
{
	struct ir_device *dev = &ir_dev;

	file->private_data = dev;	
        if (ir_open_count++)
                return 0;

	dev->buf_start = vmalloc(IR_BUFFER_SIZE * sizeof(input_code));
	if (!dev->buf_start) {
		printk(KERN_WARNING "Could not allocate memory buffer for IR.\n");
		return -ENOMEM;
	}
	
	if (request_irq(IR_INTERRUPT, ir_interrupt, IRQF_SAMPLE_RANDOM | IRQF_SHARED,
			MODULE_NAME, dev)) {
		printk(KERN_ERR "Failed to get IR IRQ\n");
		vfree(dev->buf_start);
		return -EIO;
	}

	dev->buf_end = dev->buf_start + IR_BUFFER_SIZE;
	dev->buf_rp = dev->buf_wp = dev->buf_start;

	// Now let's start generating some interrupts
	MMIO(TSU_CTL(IR_TSU_CHANNEL)) = (IR_GPIO << 2) | TSU_MODE_BOTH;

	// We should do locking on this really
	MMIO(TSU_INT_ENABLE(IR_TSU_CHANNEL)) |= (1<<(IR_TSU_CHANNEL&7));
	return 0;	
}

static int ir_release(struct inode *inode, struct file *file)
{
	struct ir_device *dev = file->private_data;

        if (!--ir_open_count) {
		// We're the last close so kill the timer
		del_timer_sync(&dev->timer);

		// Stop generating interrupts (should do locking really)
		MMIO(TSU_INT_ENABLE(IR_TSU_CHANNEL)) &= ~(1<<(IR_TSU_CHANNEL&7));
		MMIO(TSU_CTL(IR_TSU_CHANNEL)) = 0;

		free_irq(IR_INTERRUPT, dev);
		
		vfree(dev->buf_start);
		dev->buf_start = NULL;
	}
	
	return 0;
}

static ssize_t ir_read(struct file *filp, char __user *dest, size_t count, loff_t *offset)
{
	struct ir_device *dev = filp->private_data;
	int n;
	
	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;

	while (dev->buf_rp == dev->buf_wp) {
		up(&dev->sem);
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(dev->wq, (dev->buf_rp != dev->buf_wp)))
			return -ERESTARTSYS;
		if (down_interruptible(&dev->sem))
			return -ERESTARTSYS;
	}

	// So we must have some data then

	// We only accept multiples of the size of the input_code.
	count >>= 2;
	n = 0;

	while(count--) {
		input_code data;
		// If we've run out of data then return
		if (dev->buf_rp == dev->buf_wp) {
			up(&dev->sem);
			return n;
		}

		data = *dev->buf_rp;

		copy_to_user(dest + n, dev->buf_rp, sizeof(input_code));

		n += sizeof(input_code);

		if (++dev->buf_rp == dev->buf_end)
			dev->buf_rp = dev->buf_start;
	}
	
	up(&dev->sem);
	return n;
}

unsigned int ir_poll(struct file *filp, poll_table *wait)
{
	struct ir_device *dev = filp->private_data;
	unsigned int mask = 0;

	down(&dev->sem);
	poll_wait(filp, &dev->wq, wait);
	if (dev->buf_rp != dev->buf_wp)
		mask |= POLLIN | POLLRDNORM;
	up(&dev->sem);
	return mask;
}

struct file_operations ir_ops = {
	.owner = THIS_MODULE,
	.open = ir_open,
	.release = ir_release,
	.read = ir_read,
	.poll = ir_poll,
};

struct miscdevice misc_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "pandora2-ir",
	.fops = &ir_ops,
};

//
// DRIVER
//

static int __devinit ir_probe(struct platform_device *platform_dev)
{
	struct ir_device *dev = &ir_dev;

	if (alloc_chrdev_region(&dev->devt, 0, 1, MODULE_NAME)) {
		printk(KERN_ERR "Failed to allocate character device.\n");
		return -1;
	}

	sema_init(&dev->sem, 1);
	init_waitqueue_head(&dev->wq);

	init_timer(&dev->timer);
	dev->timer.data = (unsigned long)dev;
	dev->timer.function = ir_timer;

	misc_register(&misc_dev);

	return 0;
}
	
static int __devexit ir_remove(struct platform_device *dev)
{
	BUG_ON(ir_open_count != 0);

	misc_deregister(&misc_dev);
        return 0;
}

static void ir_shutdown(struct platform_device *dev)
{
	misc_deregister(&misc_dev);
}


static struct platform_driver ir_platform_driver = {
        .driver         = {
                .name   = MODULE_NAME,
                .owner  = THIS_MODULE,
        },
        .probe          = ir_probe,
        .remove         = ir_remove,
        .shutdown       = ir_shutdown,
};

static struct platform_device *ir_platform_device;

static int __init ir_init(void)
{
	int err;
	err = platform_driver_register(&ir_platform_driver);
	if (err == 0) {
		ir_platform_device = platform_device_alloc(MODULE_NAME, -1);
		if (ir_platform_device) {
			err = platform_device_add(ir_platform_device);
			if (err == 0) {
				return 0;
			} else {
				platform_device_put(ir_platform_device);
			}
		} else {
			err = -ENOMEM;
		}
		
		platform_driver_unregister(&ir_platform_driver);
	}
	return err;
}

static void __exit ir_exit(void)
{
	platform_device_del(ir_platform_device);
	platform_driver_unregister(&ir_platform_driver);
}

module_init(ir_init);
module_exit(ir_exit);


