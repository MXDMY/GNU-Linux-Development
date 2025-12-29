# export ARCH = arm64
# export CROSS_COMPILE = aarch64-none-linux-gnu
obj-m += chrdev_template.o miscdev_template.o platdev_template.o platdrv_template.o
KDIR := /usr/src/linux-headers-5.10.0-30-amd64
PWD ?= $(shell pwd)
all:
	make -C $(KDIR) M=$(PWD) modules
clean:
	make -C $(KDIR) M=$(PWD) clean
