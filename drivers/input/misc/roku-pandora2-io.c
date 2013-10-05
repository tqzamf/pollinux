// Roku IO driver
//
// (C) 2007 Roku LLC
//
// Authors:
//   Mike Crowe <mcrowe@rokulabs.com>
//
// This driver deals with inputs and outputs on the Pandora2
// board. Both come in two varieties. The first variety are simply
// connected to single GPIOs and can be read/written at will. The
// second variety are connected to one of eight GPIOs that are shared
// via various latches between different uses. These are called
// "banked".
//
// The banked inputs are connected to the shared GPIOs when the
// required latch is driven low. This allows the value to be read. So
// the sequence is: set latch control low, read GPIOs, set latch
// control high.
//
// The banked outputs are latched from the shared GPIOs when the
// required latch goes low. So the sequence is: write GPIOs as
// required, set latch control low, set latch control high. It is the
// setting of the latch control high that causes the data to be
// latched.
//
// Because there is no way to retrieve the current state of the
// outputs it is necessary for the driver to store a mirror of the
// current state of each bank so that future changes to a single
// output can preserve the state of the other outputs in the same
// bank.
//
// The driver leaves the shared GPIO range tristated most of the time
// because many times a second the timer interrupt causes the inputs
// to be read so this is more efficient. When an output operation is
// required they are reconfigured temporarily.

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

#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/timer.h>

#include <asm/irq.h>
#include <asm/io.h>
#include <asm/mach-pnx8550/gpio.h>

MODULE_AUTHOR("Mike Crowe <mcrowe@rokulabs.com>");
MODULE_DESCRIPTION("Roku Pandora2 IO driver");
MODULE_LICENSE("GPL");

static uint initial_outputs;
module_param(initial_outputs, uint, 0);
MODULE_PARM_DESC(initial_outputs, "Initial value of pandora2 GPIO outputs.")

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define POLLS_PER_SECOND 40
#define JIFFY_INCREMENT (HZ/POLLS_PER_SECOND)

// Twelve on the IO connector, eight DIP switches and one front panel switch
#define INPUT_COUNT (12 + 8 + 1)

// Six on the IO connector, six in the left array, ten in the right array, one for HD600 compatibility, one Ethernet LED enable
#define OUTPUT_COUNT (6 + 6 + 10 + 1 + 1)

// The number of latches used to connect inputs to the shared GPIO range
#define INPUT_LATCHES 2

// The number of latches used to connect outputs to the shared GPIO range
#define OUTPUT_LATCHES 3

#define FIRST_BUTTON BTN_MOUSE

struct io_input_dev
{
	struct input_dev *input_dev;
	struct timer_list timer;

	u32 last_input_state;
};

struct io_output_dev
{
	struct input_dev *input_dev;

	u8 last_output_state[OUTPUT_LATCHES];
};

static spinlock_t shared_gpio_lock;
static struct io_input_dev io_input_dev;
static struct io_output_dev io_output_dev;

static int io_open_count = 0;

static void io_configure_gpio(void)
{
	// The latch control pins are all driven high. We'll drive
	// them low individually when we want to read/write the
	// latched pins via the shared GPIOs.
	
	const u32 mc0 = 0
		// Output latch control
		| PIN_MODE_WRITE(0, PIN_MODE_GPIO)
		// Simple outputs
		| PIN_MODE_WRITE(1, PIN_MODE_GPIO)
		| PIN_MODE_WRITE(2, PIN_MODE_GPIO)
		;
	
	const u32 mc16 = 0
		// Shared GPIOs under latch control
		| PIN_MODE_WRITE(22, PIN_MODE_GPIO)
		| PIN_MODE_WRITE(23, PIN_MODE_GPIO)
		| PIN_MODE_WRITE(24, PIN_MODE_GPIO)
		| PIN_MODE_WRITE(25, PIN_MODE_GPIO)
		| PIN_MODE_WRITE(26, PIN_MODE_GPIO)
		| PIN_MODE_WRITE(27, PIN_MODE_GPIO)
		| PIN_MODE_WRITE(28, PIN_MODE_GPIO)
		| PIN_MODE_WRITE(29, PIN_MODE_GPIO)
		// Output latch control
		| PIN_MODE_WRITE(20, PIN_MODE_GPIO)
		| PIN_MODE_WRITE(21, PIN_MODE_GPIO)
		;

	const u32 mc32 = 0
		// Simple inputs
		| PIN_MODE_WRITE(32, PIN_MODE_GPIO)
		// Simple outputs
		| PIN_MODE_WRITE(47, PIN_MODE_GPIO)
		;

	const u32 mc48 = 0
		// Simple inputs
		| PIN_MODE_WRITE(48, PIN_MODE_GPIO)
		| PIN_MODE_WRITE(49, PIN_MODE_GPIO)
		| PIN_MODE_WRITE(52, PIN_MODE_GPIO)
		| PIN_MODE_WRITE(53, PIN_MODE_GPIO)
		// Simple outputs
		| PIN_MODE_WRITE(54, PIN_MODE_GPIO)
		| PIN_MODE_WRITE(55, PIN_MODE_GPIO)
		// Input latch control
		| PIN_MODE_WRITE(56, PIN_MODE_GPIO)
		| PIN_MODE_WRITE(57, PIN_MODE_GPIO)
		;
	
	const u32 iod0 = 0
		// Output latch control
		| PIN_DATA_WRITE(0, 1)
		// Simple outputs
		| PIN_DATA_WRITE(1, 0)
		| PIN_DATA_WRITE(2, 0)
		;
	
	const u32 iod16 = 0
		// Shared GPIOs under latch control
		| PIN_MULTI_TRISTATE(22, 8)

		// Output latch control
		| PIN_DATA_WRITE(20, 1)
		| PIN_DATA_WRITE(21, 1)
		;

	const u32 iod32 = 0
		// Simple inputs
		| PIN_TRISTATE(32)
		// Simple outputs
		| PIN_DATA_WRITE(47, 0)
		;

	const u32 iod48 = 0
		// Simple inputs
		| PIN_TRISTATE(48)
		| PIN_TRISTATE(49)
		| PIN_TRISTATE(52)
		| PIN_TRISTATE(53)
		// Simple outputs
		| PIN_DATA_WRITE(54, 0)
		| PIN_DATA_WRITE(55, 0)
		// Input latch control
		| PIN_DATA_WRITE(56, 1)
		| PIN_DATA_WRITE(57, 1)
		;

	BUILD_BUG_ON(MMIO_IOD(22) != MMIO_IOD(29));
	BUILD_BUG_ON(MMIO_MC(22) != MMIO_MC(29));

	// Configure all the GPIOs
	MMIO(MMIO_MC(0)) = mc0;
	MMIO(MMIO_MC(16)) = mc16;
	MMIO(MMIO_MC(32)) = mc32;
	MMIO(MMIO_MC(48)) = mc48;

	// Now use the shared GPIOs to turn all the LEDs off. Drive
	// low.
	MMIO(MMIO_IOD(22)) = PIN_MULTI_WRITE(22, 8, 0);

	// Set all the latches so that the contents of the shared GPIO
	// range goes to all the LEDs..
	MMIO(MMIO_IOD(20)) = PIN_DATA_WRITE(20, 0) | PIN_DATA_WRITE(21, 0);
	MMIO(MMIO_IOD(0)) = PIN_DATA_WRITE(0, 0);
	
	// Now we can set all the GPIOs to the states we want them to
	// be which will also tristate the shared GPIOs and reset the
	// latches to high. Note that because the Ethernet enable is
	// inverted this will actually enable the Ethernet LED.
	MMIO(MMIO_IOD(0)) = iod0;
	MMIO(MMIO_IOD(16)) = iod16;
	MMIO(MMIO_IOD(32)) = iod32;
	MMIO(MMIO_IOD(48)) = iod48;
};

//
// OUTPUT
//

static void io_set_latched_output(struct io_output_dev *dev, u8 latch_index, u8 index, u8 value)
{
	u8 bank = dev->last_output_state[latch_index];

	// Set the bit we want to set correctly
	if (value)
		bank |= (1<<index);
	else
		bank &= ~(1<<index);

	// Stop a button scanning interrupt coming in.
	spin_lock(&shared_gpio_lock);
	
	// Drive the bank to the correct state
	MMIO(MMIO_IOD(22)) = PIN_MULTI_WRITE(22, 8, bank);

	// Pulse the correct GPIO.
	switch (latch_index)
	{
	case 0:
		MMIO(MMIO_IOD(20)) = PIN_DATA_WRITE(20, 0);
		MMIO(MMIO_IOD(20)) = PIN_DATA_WRITE(20, 1); // Setting high causes the data to be latched
		break;
	case 1:
		MMIO(MMIO_IOD(21)) = PIN_DATA_WRITE(21, 0);
		MMIO(MMIO_IOD(21)) = PIN_DATA_WRITE(21, 1); // Setting high causes the data to be latched
		break;
	case 2:
		MMIO(MMIO_IOD(0)) = PIN_DATA_WRITE(0, 0);
		MMIO(MMIO_IOD(0)) = PIN_DATA_WRITE(0, 1); // Setting high causes the data to be latched
		break;
	default:
		BUG();
		break;
	}

	// Tristate the bank again
	MMIO(MMIO_IOD(22)) = PIN_MULTI_TRISTATE(22, 8);
	
	spin_unlock(&shared_gpio_lock);
	
	dev->last_output_state[latch_index] = bank;
}

// In order to maintain compatibility with the HD600 the LEDs are
// numbered a bit oddly.

// First set - connectivity LEDS.
// Second set - mode LEDs.
// Third set - off board GPIO.
// Special - Ethernet LED enable.

static int io_set_output(struct io_output_dev *dev, u8 index, int value)
{
	switch (index)
	{
	case 0:
		// The HD2000 doesn't have a BUSY LED. The CF/SD
		// activity LED is dealt with in the hardware.
		break;
	case 1: // D3 (first in right bank)
		io_set_latched_output(dev, 0, (index - 1), value);
		break;
	case 2: // D11
		io_set_latched_output(dev, 1, (index - 2) + 6, value);
		break;
	case 3: // D13
	case 4: // D14
	case 5: // D15
	case 6: // D16
	case 7: // D17
	case 8: // D18
	case 9: // D19
	case 10: // D20 (last in right bank)
		io_set_latched_output(dev, 2, (index - 3), value);
		break;

	case 11: // D4 (first in left bank)
	case 12: // D5
	case 13: // D6
	case 14: // D7
	case 15: // D8
	case 16: // D9 (last in left bank)
		io_set_latched_output(dev, 0, (index - 11) + 1, value);
		break;

	case 17: // LED0 - GPIO1
	case 18: // LED1 - GPIO2
		MMIO(MMIO_IOD(0)) = PIN_DATA_WRITE(index - 17 + 1, !!value);
		break;
	case 19: // LED2 - GPIO54
	case 20: // LED3 - GPIO55
		MMIO(MMIO_IOD(54)) = PIN_DATA_WRITE(54 + (index - 19), !!value);
		break;
	case 21: // LED4
	case 22: // LED5
		io_set_latched_output(dev, 1, index - 21 + 4, value);
		break;

	case 31: // ETH_LED_EN (it's active low so we invert it here) - GPIO47
		MMIO(MMIO_IOD(47)) = PIN_DATA_WRITE(47, !value);
		break;
		
	default:
		// Invalid LED
		return -1;
	}
	return 0;
}

static void io_set_outputs(struct io_output_dev *dev, u32 value)
{
	u8 index;
	for(index = 0; index < 32; ++index, value >>= 1) {
		io_set_output(dev, index, value & 1);
		input_event(dev->input_dev, EV_LED, index, value & 1);
	}
}

static int io_output_event(struct input_dev *input_dev, unsigned int type, unsigned int code, int value)
{
	struct io_output_dev *dev = input_dev->private;

	if (type == EV_LED)
		return io_set_output(dev, code, value);
	else
		return -1;
}

//
// INPUT
//

/// We read the inputs so that the buttons on the IO connector
/// numbered 0-11 appear in bits 0-11, the front panel switch is in
/// bit 12 and the DIP switches are in bits 13-20.
inline u32 io_read_inputs(struct io_input_dev *dev)
{
	u32 result = 0;
	
	// First read the direct inputs
	u32 iod32 = MMIO(MMIO_IOD(32));
	u32 iod48 = MMIO(MMIO_IOD(48));

	// Stop anyone else using the shared GPIOs.
	spin_lock(&shared_gpio_lock);
	
	// Read the DIP switches
	MMIO(MMIO_IOD(57)) = PIN_DATA_WRITE(57, 0);
	result <<= 8;
	result |= PIN_MULTI_READ(MMIO(MMIO_IOD(22)), 22, 8);
	//result |= io_read_bank();
	MMIO(MMIO_IOD(57)) = PIN_DATA_WRITE(57, 1);

	// Now read the directly wired inputs including the front
	// panel switch
	result <<= 5;
	result |= 0
		| (PIN_MULTI_READ(iod48, 48, 2) << 0)
		| (PIN_MULTI_READ(iod48, 52, 2) << 2)
		| (PIN_DATA_READ(iod32, 32) << 4);

	// Then read the second batch of latched inputs
	MMIO(MMIO_IOD(56)) = PIN_DATA_WRITE(56, 0);
	result <<= 8;
	result |= PIN_MULTI_READ(MMIO(MMIO_IOD(22)), 22, 8);
	MMIO(MMIO_IOD(56)) = PIN_DATA_WRITE(56, 1);

	spin_unlock(&shared_gpio_lock);
	return result;
}

static void io_report_inputs(struct io_input_dev *dev, u32 state, int report_down)
{
	u32 mask;
	u32 button;
	u32 changes;

	if (report_down)
		changes = state ^ ((1<<INPUT_COUNT)-1);
	else
		changes = dev->last_input_state ^ state;
	
	// The buttons are active low so we invert the state when we
	// pass it to report_key.
	for(mask = 1, button = FIRST_BUTTON; mask <= changes; mask<<=1, ++button)
		if (changes & mask)
			input_report_key(dev->input_dev, button, !(state & mask));
	input_sync(dev->input_dev);

	// Make sure we don't report these changes again.
	dev->last_input_state = state;
}

static void io_input_timer(unsigned long data)
{
	u32 new_inputs;
	struct io_input_dev *dev = (struct io_input_dev *)data;

	new_inputs = io_read_inputs(dev);

	if (unlikely(new_inputs != dev->last_input_state))
		io_report_inputs(dev, new_inputs, FALSE);

	if (likely(io_open_count)) {
		dev->timer.expires = jiffies + JIFFY_INCREMENT;
		add_timer(&dev->timer);
	}
}

static int io_input_open(struct input_dev *input_dev)
{
	struct io_input_dev *dev = input_dev->private;

        if (io_open_count++)
                return 0;

	// We're the first open so kick off the timer
	dev->timer.expires = jiffies;
	add_timer(&dev->timer);
        return 0;
}

static void io_input_close(struct input_dev *input_dev)
{
	struct io_input_dev *dev = input_dev->private;

        if (!--io_open_count) {
		// We're the last close so kill the timer
		del_timer_sync(&dev->timer);
	}
}

//
// DRIVER
//

static int __devinit io_probe(struct platform_device *dev)
{
        struct input_dev *input_dev = NULL;
	struct input_dev *output_dev = NULL;
        int err;
	unsigned long i;

	spin_lock_init(&shared_gpio_lock);
	
        input_dev = input_allocate_device();
        if (!input_dev) {
                err = -ENOMEM;
		goto err_free_devices;
	}

	output_dev = input_allocate_device();
	if (!output_dev) {
		err = -ENOMEM;
		goto err_free_devices;
	}

        input_dev->private = &io_input_dev;
        input_dev->name = "Roku Pandora2 input device",
        input_dev->phys = "hd2000/gpio";
	input_dev->open = io_input_open;
	input_dev->close = io_input_close;
        input_dev->id.bustype = BUS_HOST;
        input_dev->id.vendor  = 0x001f;
        input_dev->id.product = 0x0001;
        input_dev->id.version = 0x0100;	
        input_dev->cdev.dev = &dev->dev;
	input_dev->evbit[0] = BIT(EV_KEY);

	// The buttons we support
	for(i = FIRST_BUTTON; i < FIRST_BUTTON + INPUT_COUNT; ++i)
		set_bit(i, input_dev->keybit);

        output_dev->private = &io_output_dev;
        output_dev->name = "Roku Pandora2 output device",
        output_dev->phys = "hd2000/gpio";
	output_dev->event = io_output_event;
        output_dev->id.bustype = BUS_HOST;
        output_dev->id.vendor  = 0x001f;
        output_dev->id.product = 0x0001;
        output_dev->id.version = 0x0100;	
        output_dev->cdev.dev = &dev->dev;
	output_dev->evbit[0] = BIT(EV_LED);

	// The LEDs we support
	for(i = 0; i < OUTPUT_COUNT - 1; ++i)
		set_bit(i, output_dev->ledbit);
	// The Ethernet LED enable bit is 31.
	set_bit(31, output_dev->ledbit);
	
        err = input_register_device(input_dev);
	if (err)
		goto err_free_devices;

        err = input_register_device(output_dev);
	if (err)
		goto err_free_devices;

	init_timer(&io_input_dev.timer);
	io_input_dev.timer.data = (unsigned long)&io_input_dev;
	io_input_dev.timer.function = io_input_timer;
	io_input_dev.input_dev = input_dev;
	io_output_dev.input_dev = output_dev;

	// Set all the GPIOs up how we want them.
	io_configure_gpio();
	io_set_outputs(&io_output_dev, initial_outputs);
	
	// Report all the buttons that are currently down.
	io_report_inputs(&io_input_dev, io_read_inputs(&io_input_dev), TRUE);

	return 0;

err_free_devices:
	if (output_dev)
		input_free_device(output_dev);
	if (input_dev)
		input_free_device(input_dev);
        return err;
}

static int __devexit io_remove(struct platform_device *dev)
{
        input_unregister_device(io_input_dev.input_dev);
        input_unregister_device(io_output_dev.input_dev);

	BUG_ON(io_open_count != 0);
        return 0;
}

static void io_shutdown(struct platform_device *dev)
{
	input_unregister_device(io_input_dev.input_dev);
	input_unregister_device(io_output_dev.input_dev);
}


static struct platform_driver io_platform_driver = {
        .driver         = {
                .name   = "roku-pandora2-io",
                .owner  = THIS_MODULE,
        },
        .probe          = io_probe,
        .remove         = io_remove,
        .shutdown       = io_shutdown,
};

static struct platform_device *io_platform_device;

static int __init io_init(void)
{
	int err;
	err = platform_driver_register(&io_platform_driver);
	if (err == 0) {
		io_platform_device = platform_device_alloc("roku-pandora2-io", -1);
		if (io_platform_device) {
			err = platform_device_add(io_platform_device);
			if (err == 0) {
				return 0;
			} else {
				platform_device_put(io_platform_device);
			}
		} else {
			err = -ENOMEM;
		}
		
		platform_driver_unregister(&io_platform_driver);
	}
	return err;
}

static void __exit io_exit(void)
{
	platform_device_del(io_platform_device);
	platform_driver_unregister(&io_platform_driver);
}

module_init(io_init);
module_exit(io_exit);

