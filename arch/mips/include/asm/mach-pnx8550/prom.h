/*
 *  Various system definitions for PNX8550.
 *
 *  Public domain.
 *
 */

#ifndef __PNX8550_PROM_H
#define __PNX8550_PROM_H

extern int prom_argc;
extern char **prom_argv, **prom_envp;
extern char prom_mtdparts[1024];
extern void  __init prom_init_cmdline(void);
extern void  __init prom_init_mtdparts(void);
extern char *prom_getenv(char *envname);
extern char *prom_getcmdline(void);
extern void __init board_setup(void);
extern void pnx8550_machine_restart(char *);
extern void pnx8550_machine_halt(void);
extern void pnx8550_machine_power_off(void);
extern struct resource ioport_resource;
extern struct resource iomem_resource;
extern unsigned long get_system_mem_size(void);

#endif
