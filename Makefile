KDIR ?= /lib/modules/$(shell uname -r)/build
MODULE_DIR := $(CURDIR)/module

.PHONY: all clean editor vm tools
all: tools
	$(MAKE) -C "$(KDIR)" M="$(MODULE_DIR)" W=1 modules

tools: tools/icmp_send

tools/icmp_send: tools/icmp_send.c
	$(CC) -static -o $@ $<

clean:
	$(MAKE) -C "$(KDIR)" M="$(MODULE_DIR)" clean
	@for d in capstones/*/; do \
		[ -f "$$d/Kbuild" ] && $(MAKE) -C "$(KDIR)" M="$(CURDIR)/$$d" clean 2>/dev/null; \
	done; true
	rm -f tools/icmp_send
	rm -f modules.order Module.symvers .modules.order.cmd .Module.symvers.cmd

editor: clean
	bear -- $(MAKE) -C "$(KDIR)" M="$(MODULE_DIR)" W=1 modules
	@python3 scripts/expand-compile-db

vm: all
	"$(CURDIR)/vm/boot" "$(MODULE_DIR)/hello.ko"
