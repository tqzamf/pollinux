/*
 * Copyright 2001, 2002, 2003 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 * Copyright (C) 2007 Ralf Baechle (ralf@linux-mips.org)
 *
 * Common time service routines for MIPS machines. See
 * Documents/MIPS/README.txt.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/param.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/smp.h>
#include <linux/kernel_stat.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/export.h>

#include <asm/bootinfo.h>
#include <asm/cpu.h>
#include <asm/time.h>
#include <asm/hardirq.h>
#include <asm/div64.h>
#include <asm/debug.h>

#include <int.h>
#include <cm.h>

static unsigned long cpj;

static cycle_t hpt_read(struct clocksource *cs)
{
	return read_c0_count2();
}

static struct clocksource pnx_clocksource = {
	.name		= "pnx8xxx",
	.rating		= 200,
	.read		= hpt_read,
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static irqreturn_t pnx8xxx_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *c = dev_id;

	/* clear MATCH, signal the event */
	c->event_handler(c);

	return IRQ_HANDLED;
}

static struct clock_event_device pnx8xxx_clockevent;

static struct irqaction pnx8xxx_timer_irq = {
	.handler	= pnx8xxx_timer_interrupt,
	.flags		= IRQF_DISABLED | IRQF_PERCPU | IRQF_TIMER,
	.name		= "pnx8xxx_timer",
	.dev_id		= &pnx8xxx_clockevent,
};

static irqreturn_t monotonic_interrupt(int irq, void *dev_id)
{
	/* Timer 2 clear interrupt */
	write_c0_compare2(-1);
	return IRQ_HANDLED;
}

static struct irqaction monotonic_irqaction = {
	.handler = monotonic_interrupt,
	.flags = IRQF_DISABLED | IRQF_TIMER,
	.name = "Monotonic timer",
};

static int pnx8xxx_set_next_event(unsigned long delta,
				struct clock_event_device *evt)
{
	write_c0_compare(delta);
	/* count might be past compare already, especially if delta is short.
	 * we could try to detect that, but then we can just reset the timer
	 * instead. all that does is lengthen the delay a bit, which shouldn't
	 * harm anything. the clock source, timer2, is independent, so this
	 * shouldn't screw up timing. */
	write_c0_count(0);

	return 0;
}

static void pnx8xxx_set_mode(enum clock_event_mode mode,
				struct clock_event_device *cd)
{
	printk(KERN_INFO "pnx8xxx_clockevent set mode=%i\n", mode);
}

static struct clock_event_device pnx8xxx_clockevent = {
	.name		= "pnx8xxx_clockevent",
	.features	= CLOCK_EVT_FEAT_ONESHOT,
	.rating		= 300,
	.set_next_event = pnx8xxx_set_next_event,
	.set_mode	= pnx8xxx_set_mode,
	.cpumask	= cpu_all_mask,
};

__init void plat_time_init(void)
{
	unsigned int configPR;
	unsigned int n;
	unsigned int m;
	unsigned int p;
	unsigned int pow2p;

	/* PLL0 sets MIPS clock (PLL1 <=> TM1, PLL6 <=> TM2, PLL5 <=> mem) */
	/* (but only if CLK_MIPS_CTL select value [bits 3:1] is 1:  FIXME) */
	n = (PNX8550_CM_PLL0_CTL & PNX8550_CM_PLL_N_MASK) >> 16;
	m = (PNX8550_CM_PLL0_CTL & PNX8550_CM_PLL_M_MASK) >> 8;
	p = (PNX8550_CM_PLL0_CTL & PNX8550_CM_PLL_P_MASK) >> 2;
	pow2p = (1 << p);
	db_assert(m != 0 && pow2p != 0);

	/*
	 * Compute the frequency as in the PNX8550 User Manual 1.0, p.186
	 * (a.k.a. 8-10).  Divide by HZ for a timer offset that results in
	 * HZ timer interrupts per second.
	 */
	mips_hpt_frequency = 27UL * ((1000000UL * n)/(m * pow2p));
	cpj = DIV_ROUND_CLOSEST(mips_hpt_frequency, HZ);

	/* need to compute those values manually for clocksource to work */
	clockevents_calc_mult_shift(&pnx8xxx_clockevent, mips_hpt_frequency, 16);
	pnx8xxx_clockevent.max_delta_ns = clockevent_delta2ns(0x7fffffff, &pnx8xxx_clockevent);
	pnx8xxx_clockevent.min_delta_ns = clockevent_delta2ns(0x300, &pnx8xxx_clockevent);

	/* Timer 1 start */
	configPR = read_c0_config7();
	configPR &= ~0x00000008;
	write_c0_config7(configPR);

	/* Timer 2 start */
	configPR = read_c0_config7();
	configPR &= ~0x00000010;
	write_c0_config7(configPR);

	/* Timer 3 stop */
	configPR = read_c0_config7();
	configPR |= 0x00000020;
	write_c0_config7(configPR);

	/* Setup Timer 1. We clear the timer after setting compare because if
	 * count > compare, the next jiffy will delayed by ~16 seconds. The
	 * downside is that the first jiffy might happen immediatelly -- which
	 * shouldn't hurt anything. */
	write_c0_compare(cpj);
	write_c0_count(0);

	/* Setup Timer 2. Set count first so it doesn't trigger right away. */
	write_c0_count2(0);
	write_c0_compare2(0xffffffff);

	/* now that everything is set up, enable IRQs */
	setup_irq(PNX8550_INT_TIMER1, &pnx8xxx_timer_irq);
	setup_irq(PNX8550_INT_TIMER2, &monotonic_irqaction);

	/* register devices */
	clocksource_register_hz(&pnx_clocksource, mips_hpt_frequency);
	clockevents_register_device(&pnx8xxx_clockevent);
	printk(KERN_INFO "pnx8xxx_clockevent mips_hpt_frequency=%u mult=%u shift=%u\n",
			mips_hpt_frequency, pnx8xxx_clockevent.mult, pnx8xxx_clockevent.shift);
	printk(KERN_INFO "pnx8xxx_clockevent min_delta=%llu max_delta=%llu cpj=%lu\n",
			pnx8xxx_clockevent.min_delta_ns, pnx8xxx_clockevent.max_delta_ns, cpj);
}
