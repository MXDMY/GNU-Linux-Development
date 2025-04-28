# export ARCH = arm64
# export CROSS_COMPILE = aarch64-none-linux-gnu
obj-m += chrdev_template.o miscdev_template.o
KDIR := /home/a/Downloads/linux-5.10
PWD ?= $(shell pwd)
all:
	make -C $(KDIR) M=$(PWD) modules
clean:
	make -C $(KDIR) M=$(PWD) clean
