#
# Makefile for ALSA
#

snd-au1x00-objs := au1x00.o
snd-sgi-o2-objs := sgio2audio.o ad1843.o
snd-sgi-hal2-objs := hal2.o
snd-pnx8550-ao1-objs := pnx8550ao1.o
snd-pnx8550-spdo-objs := pnx8550spdo.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_AU1X00) += snd-au1x00.o
obj-$(CONFIG_SND_SGI_O2) += snd-sgi-o2.o
obj-$(CONFIG_SND_SGI_HAL2) += snd-sgi-hal2.o
obj-$(CONFIG_SND_PNX8550AO1) += snd-pnx8550-ao1.o
obj-$(CONFIG_SND_PNX8550SPDO) += snd-pnx8550-spdo.o
