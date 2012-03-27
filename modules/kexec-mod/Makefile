EXTRA_CFLAGS += -D__PLAT_TI_OMAP3430__ -Wall -march=armv7-a
EXTRA_CFLAGS += -DCONFIG_KEXEC

obj-m += kexec_load.o
kexec_load-objs := kexec.o machine_kexec.o mmu.o sys.o core.o relocate_kernel.o proc-v7.o tlb-v7.o cache-v7.o abort-ev7.o copypage-v6.o entry-common.o driver_sys.o init-mm.o
#head.o

all:
	make -C kernel/ M=$(PWD) modules

clean:
	make -C kernel/ M=$(PWD) clean
	rm -f *.order
