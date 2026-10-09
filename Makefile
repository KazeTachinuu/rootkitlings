KDIR ?= /lib/modules/$(shell uname -r)/build
MODULE_DIR ?=
GUEST_TOOLS := \
	tools/cred_trigger_test \
	tools/credential_snapshot_test \
	tools/icmp_send \
	tools/ioctl_test \
	tools/module_load_test \
	tools/proc_read_test \
	tools/proc_write_test \
	tools/status_field_test \
	tools/task_attribution_test

.PHONY: all help module clean compile-db test-solutions doctor validate validate-quick tools

.DEFAULT_GOAL := help

all: help

help:
	@printf '%s\n' \
		'Rootkitlings maintainer targets:' \
		'  tools           Build guest-side verification tools' \
		'  clean           Remove generated verification tools' \
		'  doctor          Check host dependencies' \
		'  validate        Run the full repository validation' \
		'  validate-quick  Run static validation only' \
		'  test-solutions  Build and test reference solutions' \
		'  compile-db      Generate compile_commands.json'

module:
	@test -n "$(MODULE_DIR)" || { echo 'MODULE_DIR is required' >&2; exit 2; }
	$(MAKE) -C "$(KDIR)" M="$(MODULE_DIR)" W=1 modules

tools: $(GUEST_TOOLS)

tools/cred_trigger_test: tools/cred_trigger_test.c
	$(CC) -std=c11 -Wall -Wextra -Werror -pthread -static -o $@ $<

tools/credential_snapshot_test: tools/credential_snapshot_test.c
	$(CC) -std=c11 -Wall -Wextra -Werror -static -o $@ $<

tools/icmp_send: tools/icmp_send.c
	$(CC) -static -o $@ $<

tools/ioctl_test: tools/ioctl_test.c
	$(CC) -static -o $@ $<

tools/module_load_test: tools/module_load_test.c
	$(CC) -static -o $@ $<

tools/proc_read_test: tools/proc_read_test.c
	$(CC) -static -o $@ $<

tools/proc_write_test: tools/proc_write_test.c
	$(CC) -std=c11 -Wall -Wextra -Werror -static -o $@ $<

tools/status_field_test: tools/status_field_test.c
	$(CC) -std=c11 -Wall -Wextra -Werror -static -o $@ $<

tools/task_attribution_test: tools/task_attribution_test.c
	$(CC) -std=c11 -Wall -Wextra -Werror -pthread -static -o $@ $<

clean:
	$(RM) $(GUEST_TOOLS)

doctor:
	@"$(CURDIR)/scripts/doctor"
	@printf '%s\n' 'Next: ./rootkitlings setup'

compile-db:
	@"$(CURDIR)/rootkitlings" start L00
	bear -- "$(CURDIR)/rootkitlings" build L00
	@python3 scripts/expand-compile-db

validate:
	@"$(CURDIR)/scripts/validate"

validate-quick:
	@"$(CURDIR)/scripts/validate" --quick

test-solutions:
	@"$(CURDIR)/scripts/test-solutions"
