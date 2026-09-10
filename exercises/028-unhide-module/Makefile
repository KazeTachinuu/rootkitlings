obj-m += unhide.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	make -C $(KDIR) M=$(PWD)/src modules

clean:
	make -C $(KDIR) M=$(PWD)/src clean
