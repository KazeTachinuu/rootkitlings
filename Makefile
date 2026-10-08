KDIR ?= /lib/modules/$(shell uname -r)/build
MODULE_DIR := $(CURDIR)/module

.PHONY: all clean compile-db test-solutions doctor validate validate-quick vm tools
all: tools
	$(MAKE) -C "$(KDIR)" M="$(MODULE_DIR)" W=1 modules

tools: tools/cred_trigger_test tools/icmp_send tools/ioctl_test

tools/cred_trigger_test: tools/cred_trigger_test.c
	$(CC) -static -o $@ $<

tools/icmp_send: tools/icmp_send.c
	$(CC) -static -o $@ $<

tools/ioctl_test: tools/ioctl_test.c
	$(CC) -static -o $@ $<

clean:
	@for d in module lings/*/* capstones/*; do \
		[ -f "$$d/Kbuild" ] && $(MAKE) -C "$(KDIR)" M="$(CURDIR)/$$d" clean 2>/dev/null; \
	done; true
	rm -f tools/cred_trigger_test tools/icmp_send tools/ioctl_test
	rm -f modules.order Module.symvers .modules.order.cmd .Module.symvers.cmd

doctor:
	@"$(CURDIR)/scripts/doctor"

compile-db: clean
	bear -- $(MAKE) -C "$(KDIR)" M="$(MODULE_DIR)" W=1 modules
	@python3 scripts/expand-compile-db

vm: all
	"$(CURDIR)/vm/boot" "$(MODULE_DIR)/hello.ko"

validate:
	@"$(CURDIR)/scripts/validate"

validate-quick:
	@"$(CURDIR)/scripts/validate" --quick

test-solutions:
	@"$(CURDIR)/scripts/test-solutions"
