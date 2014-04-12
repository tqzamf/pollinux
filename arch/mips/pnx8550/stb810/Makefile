
# Makefile for the NXP STB810 Board.

obj-y := init.o board_setup.o irqmap.o gif.o splash.o gpiostubs.o

#If we have defined anv ENV variable called STB810_MIPS_MEM_SIZE then pass it on to the
#C code
ifneq "$(STB810_MIPS_MEM_SIZE)" ""
	CFLAGS += -DSTB810_MIPS_MEM_SIZE=$(STB810_MIPS_MEM_SIZE)
endif
