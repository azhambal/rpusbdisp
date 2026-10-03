######################################
#
#    RoboPeak USB LCD Display Linux Driver
#
#    Copyright (C) 2009 - 2013 RoboPeak Team
#    This file is licensed under the GPL. See LICENSE in the package.
#
#    http://www.robopeak.net
#
#    Author Shikai Chen
#
#    Builds standalone (make) and under kbuild/DKMS
#    (make -C <kernel> M=<this dir>).
#
######################################

DRIVER_NAME := rp_usbdisplay
KERNEL_SOURCE_DIR ?= /lib/modules/$(shell uname -r)/build

obj-m := $(DRIVER_NAME).o

$(DRIVER_NAME)-objs := src/main.o \
                       src/usbhandlers.o \
                       src/fbhandlers.o \
                       src/touchhandlers.o

# src/inc/common.h includes "inc/protocol.h", which sits at ../common in the git
# tree and at ./common inside the staged DKMS package. Pick whichever is there
# so one Makefile covers both. Must use $(src), not $(PWD): under kbuild $(PWD)
# points at the kernel tree, not at these sources.
COMMON_DIR := $(if $(wildcard $(src)/../common/inc/protocol.h),$(src)/../common,$(src)/common)
ccflags-y := -I$(src)/src -I$(COMMON_DIR)

modules:
	$(MAKE) -C $(KERNEL_SOURCE_DIR) M=$(CURDIR) modules

modules_install:
	$(MAKE) -C $(KERNEL_SOURCE_DIR) M=$(CURDIR) modules_install

install: modules_install

clean:
	$(MAKE) -C $(KERNEL_SOURCE_DIR) M=$(CURDIR) clean

.PHONY: modules modules_install install clean
