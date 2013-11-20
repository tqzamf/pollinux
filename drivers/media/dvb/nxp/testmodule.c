#include <linux/module.h>	/* Needed by all modules */
#include <linux/kernel.h>	/* Needed for KERN_INFO */
#include <linux/init.h>		/* Needed for the macros */
#include "phStbDemux_common.h"


static void start(void *device, int nfeed) {
	printk("DVB TEST: start called!\n");
	return;
}
static void stop(void *device, int nfeed) {
	printk("DVB TEST: stop called!\n");
	return;
}
static int set_ts_filter(void *device, struct phStb_ts_filter *filter, int pdma) {
	printk("DVB TEST: set_ts_filter called!\n");
	return 0;
}
static int del_ts_filter(void *device, struct phStb_ts_filter *filter, int pdma) {
	printk("DVB TEST: del_ts_filter called!\n");
	return 0;
}
static int set_section_filter(void *device, struct phStb_section_filter *filter, int pdma) {
	printk("DVB TEST: set_section_filter called!\n");
	return 0;
}
static int del_section_filter(void *device, struct phStb_section_filter *filter, int pdma) {
	printk("DVB TEST: del_section_filter called!\n");
	return 0;
}
static void dvr_start(void *device, int nfeed) {
	printk("DVB TEST: dvr_start called!\n");
	return;
}
static void dvr_stop(void *device, int nfeed) {
	printk("DVB TEST: dvr_stop called!\n");
	return;
}
static int dvr_load(void *device, const u8 *buf, size_t len) {
	printk("DVB TEST: dvr_load called!\n");
	return 0;
}
static void set_desc_word(void *device, int index, int parity, unsigned char *cw) {
	printk("DVB TEST: set_desc_word called!\n");
	return;
}
static int set_desc_pid(void *device, int index, unsigned int pid) {
	printk("DVB TEST: set_desc_pid called!\n");
	return 0;
}

static struct phStb_demux_device command = {
        .start = start,
        .stop = stop,
        .set_ts_filter = set_ts_filter,
        .del_ts_filter = del_ts_filter,
        .set_section_filter = set_section_filter,
        .del_section_filter = del_section_filter,
        .dvr_start = dvr_start,
        .dvr_stop = dvr_stop,
        .dvr_load = dvr_load,
        .set_desc_word = set_desc_word,
        .set_desc_pid = set_desc_pid,
};

static struct phStb_capabilities capabilities;

void register_card(void)
{
		
   printk("register_card\n");
   capabilities.numPesFilters = 256;
   capabilities.numSectionFilters = 256;
   capabilities.numDescramblers = 256;

   dvb_phStb_register(1, NULL, &command, &capabilities);

   return;
}

static int __init testdvb_init(void)
{
	printk(KERN_INFO "testdvb_init called\n");
	register_card();
	return 0;
}

static void __exit testdvb_exit(void)
{
	dvb_phStb_remove(1);
	printk(KERN_INFO "testdvb_exit called\n");
}

module_init(testdvb_init);
module_exit(testdvb_exit);
