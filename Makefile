KDIR ?= /lib/modules/$(shell uname -r)/build
MODULE_DIR ?=

.PHONY: all help module clean compile-db test-solutions doctor validate validate-quick vm tools

.DEFAULT_GOAL := help

all: help

help:
	@printf '%s\n' \
		'Rootkitlings maintainer targets:' \
		'  tools           Build guest-side verification tools' \
		'  doctor          Check host dependencies' \
		'  validate        Run the full repository validation' \
		'  validate-quick  Run static validation only' \
		'  test-solutions  Build and test reference solutions' \
		'  compile-db      Generate compile_commands.json' \
		'  vm              Build and boot L00'

module:
	@test -n "$(MODULE_DIR)" || { echo 'MODULE_DIR is required' >&2; exit 2; }
	$(MAKE) -C "$(KDIR)" M="$(MODULE_DIR)" W=1 modules

tools: tools/cred_trigger_test tools/icmp_send tools/ioctl_test

tools/cred_trigger_test: tools/cred_trigger_test.c
	$(CC) -static -o $@ $<

tools/icmp_send: tools/icmp_send.c
	$(CC) -static -o $@ $<

tools/ioctl_test: tools/ioctl_test.c
	$(CC) -static -o $@ $<

clean:
	@workspace_id=$$("$(CURDIR)/scripts/catalog" query L00 id); \
	workspace="$(CURDIR)/.rootkitlings/work/$$workspace_id"; \
	if [ -d "$$workspace" ]; then "$(CURDIR)/rootkitlings" clean L00; fi
	$(RM) tools/cred_trigger_test tools/icmp_send tools/ioctl_test

doctor:
	@"$(CURDIR)/scripts/doctor"

compile-db:
	@"$(CURDIR)/rootkitlings" start L00
	bear -- "$(CURDIR)/rootkitlings" build L00
	@python3 scripts/expand-compile-db

vm: tools
	@"$(CURDIR)/rootkitlings" start L00
	@"$(CURDIR)/rootkitlings" build L00
	@workspace_id=$$("$(CURDIR)/scripts/catalog" query L00 id); \
	"$(CURDIR)/vm/boot" "$(CURDIR)/.rootkitlings/work/$$workspace_id/hello.ko"

validate:
	@"$(CURDIR)/scripts/validate"

validate-quick:
	@"$(CURDIR)/scripts/validate" --quick

test-solutions:
	@"$(CURDIR)/scripts/test-solutions"
