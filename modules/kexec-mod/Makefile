# In this string we need to use -D__SOME__ command for define which platform we use
# And C preprocessor use only code for platform, we defined
# For example if we use Texas Instruments OMAP 3430 - we should use
# EXTRA_CFLAGS += -D__PLAT_TI_OMAP3430__
# Else if we need for Freescale i.MX31 - we should use
# EXTRA_CFLAGS += -D__PLAT_FREESCALE_IMX31__

# I don't yet have this fully set up for the droid x yet
# This line seems to work on my X, otherwise use the line
# That's commented out.
EXTRA_CFLAGS += -D__PLAT_TI_OMAP3430__ -Wall -march=arm
# EXTRA_CFLAGS += -mfpu=neon

# Make this match the optimisation values of the kernel you're
# loading this into. Should work without changes, but it seems to
# crash on the Nexus One and Droid Pro. If you're compiling on an 
# Evo 4g, set this value to 1 and change the EXTRA_CFLAGS value to
# something appropriate to your phone
# EXTRA_CFLAGS += -O0

obj-m += kexec_load.o
kexec_load-objs := kexec.o machine_kexec.o mmu.o sys.o core.o relocate_kernel.o \
	proc-v7.o tlb-v7.o cache-v7.o abort-ev7.o copypage-v6.o entry-common.o driver_sys.o

all:
	make -C kernel/ M=$(PWD) modules

clean:
	make -C kernel/ M=$(PWD) clean
	rm -f *.order
