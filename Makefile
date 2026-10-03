KDIR ?= /lib/modules/$(shell uname -r)/build
MODULE_DIR := $(CURDIR)/module

.PHONY: all clean doctor editor test-references validate validate-quick vm tools
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
	rm -f compile_commands.json
	rm -f modules.order Module.symvers .modules.order.cmd .Module.symvers.cmd

doctor:
	@"$(CURDIR)/scripts/doctor"

editor: clean
	bear -- $(MAKE) -C "$(KDIR)" M="$(MODULE_DIR)" W=1 modules
	@python3 scripts/expand-compile-db

vm: all
	"$(CURDIR)/vm/boot" "$(MODULE_DIR)/hello.ko"

validate:
	@"$(CURDIR)/scripts/validate"

validate-quick:
	@"$(CURDIR)/scripts/validate" --quick

test-references:
	@"$(CURDIR)/scripts/test-references"
