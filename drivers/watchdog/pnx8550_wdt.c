/*
 * PNX8550 watchdog using Counter3.
 *
 * Based on softdog.c:
 *     (c) Copyright 1996 Alan Cox <alan@lxorguk.ukuu.org.uk>,
 *                        All Rights Reserved.
 * 
 * It tries to perform a "clean" emergency_restart() on timeout; if the
 * system is sufficiently hung that this doesn't work, then the hardware
 * will reset the CPU after the timeout has expired once more.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>
#include <int.h>
#include <cm.h>

#define MAX_TIMEOUT 0xffffffff

static int timeout = -1;
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout,
	"Watchdog timeout in seconds. (typ. 1 < timeout < 17, default=maximum)");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		"Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static void pnx8550wdt_ping_kernel(unsigned long data);
static struct timer_list pnx8550wdt_timer =
		TIMER_INITIALIZER(pnx8550wdt_ping_kernel, 0, 0);
static unsigned int mips_freq;
static unsigned long kernel_ping_interval;

// interrupt handler to restart the system. if the system is hosed badly
// enough that this IRQ doesn't work, the timer will reset the CPU after
// another timeout has expired.
static irqreturn_t pnx8550wdt_interrupt(int irq, void *dev_id)
{
	pr_crit("Watchdog timeout; initiating system reboot\n");
	emergency_restart();
	panic("emergency reboot failed!?\n");
}

// main watchdog functionality
static int pnx8550wdt_ping_user(struct watchdog_device *w)
{
	write_c0_count3(0);
	return 0;
}

static void pnx8550wdt_ping_kernel(unsigned long data)
{
	pnx8550wdt_ping_user(NULL);
	mod_timer(&pnx8550wdt_timer, jiffies + kernel_ping_interval);
}

static int pnx8550wdt_switch_to_kernel(struct watchdog_device *w)
{
	// set timeout to maximum and (re)start timer
	write_c0_count3(0);
	write_c0_compare3(MAX_TIMEOUT);
	mod_timer(&pnx8550wdt_timer, jiffies + kernel_ping_interval);
	return 0;
}

static int pnx8550wdt_switch_to_user(struct watchdog_device *w)
{
	// set timeout and disable in-kernel timer
	write_c0_count3(0);
	write_c0_compare3(mips_freq * w->timeout);
	del_timer(&pnx8550wdt_timer);
	return 0;
}

static int pnx8550wdt_set_timeout(struct watchdog_device *w, unsigned int t)
{
	w->timeout = t;
	write_c0_compare3(mips_freq * w->timeout);
	return 0;
}

// watchdog interface
static struct watchdog_info pnx8550wdt_info = {
	.identity = "PNX8550 Counter3 Watchdog",
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
};

static struct watchdog_ops pnx8550wdt_ops = {
	.owner = THIS_MODULE,
	.start = pnx8550wdt_switch_to_user,
	.stop = pnx8550wdt_switch_to_kernel,
	.ping = pnx8550wdt_ping_user,
	.set_timeout = pnx8550wdt_set_timeout,
};

static struct watchdog_device pnx8550wdt_dev = {
	.info = &pnx8550wdt_info,
	.ops = &pnx8550wdt_ops,
	.min_timeout = 1,
};

// notifier for system shutdown
static int pnx8550wdt_notify(struct notifier_block *this, unsigned long code,
	void *unused)
{
	// leave WDT active during reboots, in kernel ping. this gives watchdog
	// safetly during reboots (which can hang in some obscure situations),
	// without risking resets because the watchdog daemon didn't cleanly
	// close the device.
	pnx8550wdt_switch_to_kernel(&pnx8550wdt_dev);
	return NOTIFY_DONE;
}

static struct notifier_block pnx8550wdt_notifier = {
	.notifier_call	= pnx8550wdt_notify,
};

// module init / exit
static int __init pnx8550wdt_init(void)
{
	int ret;
	unsigned int n;
	unsigned int m;
	unsigned int p;
	unsigned int configPR;

	// calculate MIPS clock, which is also the Counter3 clock
	n = (PNX8550_CM_PLL0_CTL & PNX8550_CM_PLL_N_MASK) >> 16;
	m = (PNX8550_CM_PLL0_CTL & PNX8550_CM_PLL_M_MASK) >> 8;
	p = (PNX8550_CM_PLL0_CTL & PNX8550_CM_PLL_P_MASK) >> 2;
	mips_freq = 27000000UL * n / (m << p);

	// calculate possible timeouts
	pnx8550wdt_dev.max_timeout = MAX_TIMEOUT / mips_freq;
	if (timeout < 0)
		// if timeout not set, default to maximum.
		timeout = pnx8550wdt_dev.max_timeout;
	// clamp to min/max
	if (timeout < pnx8550wdt_dev.min_timeout) {
		pr_warn("timeout %d must be >= %d, using %d\n",
			timeout, pnx8550wdt_dev.min_timeout, pnx8550wdt_dev.min_timeout);
		timeout = pnx8550wdt_dev.min_timeout;
	}
	if (timeout > pnx8550wdt_dev.max_timeout) {
		timeout = pnx8550wdt_dev.max_timeout;
		pr_warn("timeout %d must be <= %d, using %d\n",
			timeout, pnx8550wdt_dev.max_timeout, pnx8550wdt_dev.max_timeout);
	}
	pnx8550wdt_dev.timeout = timeout;
	// kernel pings 3 times per timeout interval
	kernel_ping_interval = msecs_to_jiffies(
			1000/3 * pnx8550wdt_dev.max_timeout);

	// allocate IRQ
	if (request_irq(PNX8550_INT_TIMER3, pnx8550wdt_interrupt, 0,
			"pnx8550wdt", &pnx8550wdt_dev)) {
		pr_err("cannot allocate irq %d\n", PNX8550_INT_TIMER3);
		ret = -EBUSY;
		goto err0;
	}

	// register watchdog device
	watchdog_set_nowayout(&pnx8550wdt_dev, nowayout);
	ret = register_reboot_notifier(&pnx8550wdt_notifier);
	if (ret)
		goto err1;
	ret = watchdog_register_device(&pnx8550wdt_dev);
	if (ret)
		goto err2;

	// switch to kernel-based ping and start Counter3
	pnx8550wdt_switch_to_kernel(&pnx8550wdt_dev);
	configPR = read_c0_config7();
	configPR &= ~0x00000020;
	write_c0_config7(configPR);

	pr_info("PNX8550 Counter3 Watchdog initialized, timeout=%d\n",
		pnx8550wdt_dev.timeout);
	return 0;

err2:
	unregister_reboot_notifier(&pnx8550wdt_notifier);
err1:
	free_irq(PNX8550_INT_TIMER3, &pnx8550wdt_dev);
err0:
	return ret;
}

static void __exit pnx8550wdt_exit(void)
{
	unsigned int configPR;
	// Counter3 stop
	configPR = read_c0_config7();
	configPR |= 0x00000020;
	write_c0_config7(configPR);
	// release resources
	del_timer(&pnx8550wdt_timer);
	free_irq(PNX8550_INT_TIMER3, &pnx8550wdt_dev);
	// unregister devices
	watchdog_unregister_device(&pnx8550wdt_dev);
	unregister_reboot_notifier(&pnx8550wdt_notifier);
}

module_init(pnx8550wdt_init);
module_exit(pnx8550wdt_exit);

MODULE_AUTHOR("Matthias <tqzamf@gmail.com>");
MODULE_DESCRIPTION("PNX8550 Counter3 Watchdog Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
