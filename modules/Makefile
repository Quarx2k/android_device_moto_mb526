# Makefile for Android Defy kernel-omap tree
# "make jordan_modules" in repo root (not here)


# list of modules to build :
obj-m += symsearch/
obj-m += overclock/
obj-m += backlight/
obj-m += multitouch/
obj-m += mmcfix/
obj-m += gpiofix/
obj-m += clockfix/
obj-m += logcap/
obj-m += usbled/


PWD := $(shell pwd)

.PHONY: clean
clean:
	@find $(PWD) | grep "\\.o$$" | xargs rm -f
	@find $(PWD) | grep "\\.cmd$$" | xargs rm -f
	@find $(PWD) | grep "\\.mod.c$$" | xargs rm -f
	@find $(PWD) | grep "\\.order$$" | xargs rm -f
	@find $(PWD) | grep "\\.tmp_versions$$" | xargs rm -rf

EXTRA_CFLAGS += -I$(KERNEL_DIR)

#all: verifypackages .depend $(TARGET)
#######################################################################################
# TODO: add some checks here...
#
verifypackages:
ifeq ($(strip $(HAVE_GPL)),)
	@echo "*** The GPL package does not seem to be present. You will need both the"
	@echo "*** GPL package and the Non GPL package to execute this makefile."
	exit 1
endif


#######################################################################################
# Builds the modules
#
.PHONY: all
all: .depend modules
modules:
	pwd
	@echo $@ EXTRA_CFLAGS = $(EXTRA_CFLAGS)
	$(MAKE) CROSS_COMPILE=$(CROSS_COMPILE) EXTRA_CFLAGS="$(EXTRA_CFLAGS)" ARCH=$(ARCH) -C $(KERNEL_DIR) M=$(PWD) modules
	@find $(PWD) | grep "\\.o$$" | xargs rm -f
	@find $(PWD) | grep "\\.cmd$$" | xargs rm -f
	@find $(PWD) | grep "\\.mod.c$$" | xargs rm -f
	@find $(PWD) | grep "\\.order$$" | xargs rm -f
	@find $(PWD) | grep "\\.tmp_versions$$" | xargs rm -rf

#######################################################################################
# TODO: Causes the modules to get rebuilt
#
.depend:
	@find $(PWD) | grep "\\.o$$" | xargs echo "To clean: "

