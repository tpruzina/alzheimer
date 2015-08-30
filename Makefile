#KERNEL_SRC_DIR = /usr/src/linux
KERNEL_SRC_DIR = /lib/modules/`uname -r`/build
#EXTRA_CFLAGS = --verbose
SUBARCH := $(shell uname -m | sed -e s/i.86/i386/ | sed -e s/armv../arm/)
ARCH := $(SUBARCH)
VERSION=
DET_KERN_VERSION := $(shell uname -r  |cut -f 1 -d -| cut -d \. -f 3)
EXTRA_CFLAGS= -DDET_KERN_VERSION=$(DET_KERN_VERSION) -DARCH=$(ARCH) -DARCH_$(ARCH)

obj-m += alzheimer$(VERSION).o
alzheimer$(VERSION)-objs := arch/$(ARCH)/eraser.o lkm.o

all:	clean alzheimer$(VERSION)

alzheimer$(VERSION) : clean
	make -C $(KERNEL_SRC_DIR) SUBDIRS=`pwd` modules

clean : 
	rm -f *.o *.ko *.mod.c \.*.o.cmd  \.*.ko.cmd Module.symvers Module.markers modules.order
	rm -f arch/*/*.o arch/*/*.ko arch/*/\.*.o.cmd 
	rm -rf \.tmp_versions
