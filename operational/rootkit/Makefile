KDIR ?= $(shell find $(CURDIR)/../../.rklings/kernel/kits -maxdepth 1 -mindepth 1 -type d | head -1)/build

.PHONY: all clean

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
