KDIR ?= $(shell find $(CURDIR)/../../.rklings/kernel/kits -maxdepth 1 -mindepth 1 -type d | head -1)/build
SRC  := $(CURDIR)/../../curriculum/P00-complete-rootkit/solution
BUILD := $(CURDIR)/.build

.PHONY: all clean

all:
	@rm -rf $(BUILD)
	@mkdir -p $(BUILD)
	@cp -r $(SRC)/Kbuild $(SRC)/include $(SRC)/core $(SRC)/crypto $(SRC)/network $(SRC)/execution $(BUILD)/
	$(MAKE) -C $(KDIR) M=$(BUILD) modules
	@cp $(BUILD)/rklings.ko $(CURDIR)/

clean:
	@rm -rf $(BUILD) $(CURDIR)/rklings.ko
