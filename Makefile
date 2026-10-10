KDIR ?=
MODULE_DIR ?=
MODULE_OUT ?=
TOOL_OUT ?= .rootkitlings/build/tools
ROOTKITLINGS_FAIL_POINT ?= 0
GUEST_TOOL_NAMES := \
	agent_fixture \
	cred_trigger_test \
	credential_snapshot_test \
	event_stream_test \
	getdents64_test \
	module_load_params \
	module_load_test \
	netfilter_udp_test \
	packet_classifier_test \
	packet_mutation_test \
	proc_read_test \
	proc_write_test \
	read_result_test \
	run_as \
	rmdir_telemetry_test \
	secure_agent_fixture \
	socket_owner_fixture \
	status_field_test \
	task_attribution_test \
	tcp_session_fixture
GUEST_TOOLS := $(addprefix $(TOOL_OUT)/,$(GUEST_TOOL_NAMES))

.PHONY: all help module clean compile-db doctor tools tool-output-directory
.PHONY: test-checkers test-solutions validate validate-quick

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
		'  test-checkers   Test exercise checkers against faulty implementations' \
		'  test-solutions  Build and test reference solutions' \
		'  compile-db      Generate compile_commands.json'

module:
	@test -n "$(MODULE_DIR)" || { echo 'MODULE_DIR is required' >&2; exit 2; }
	@kdir="$(KDIR)"; \
	if test -z "$$kdir"; then \
		kdir="$$("$(CURDIR)/scripts/kernel" build-dir)" || exit; \
	fi; \
	if test -n "$(MODULE_OUT)"; then \
		set -- "MO=$(MODULE_OUT)"; \
	else \
		set --; \
	fi; \
	env -i PATH="$$PATH" LC_ALL=C LANG=C TZ=UTC \
		make -C "$$kdir" M="$(MODULE_DIR)" W=1 ARCH=x86_64 CC=gcc HOSTCC=gcc \
		ROOTKITLINGS_FAIL_POINT="$(ROOTKITLINGS_FAIL_POINT)" "$$@" modules

tools: $(GUEST_TOOLS)

tool-output-directory:
	@test -n "$(strip $(TOOL_OUT))" && test "$(TOOL_OUT)" != / || \
		{ echo 'TOOL_OUT must name a directory' >&2; exit 2; }
	mkdir -p "$(TOOL_OUT)"

$(TOOL_OUT)/agent_fixture: vm/tools/agent_fixture.c | tool-output-directory
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -static -o $@ $<

$(TOOL_OUT)/cred_trigger_test: vm/tools/cred_trigger_test.c | tool-output-directory
	$(CC) -std=c11 -Wall -Wextra -Werror -pthread -static -o $@ $<

$(TOOL_OUT)/credential_snapshot_test: vm/tools/credential_snapshot_test.c | tool-output-directory
	$(CC) -std=c11 -Wall -Wextra -Werror -static -o $@ $<

$(TOOL_OUT)/event_stream_test: vm/tools/event_stream_test.c | tool-output-directory
	$(CC) -std=c11 -Wall -Wextra -Werror -static -o $@ $<

$(TOOL_OUT)/getdents64_test: vm/tools/getdents64_test.c | tool-output-directory
	$(CC) -std=c11 -Wall -Wextra -Werror -pthread -static -o $@ $<

$(TOOL_OUT)/module_load_params: vm/tools/module_load_params.c | tool-output-directory
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -static -o $@ $<

$(TOOL_OUT)/module_load_test: vm/tools/module_load_test.c | tool-output-directory
	$(CC) -static -o $@ $<

$(TOOL_OUT)/netfilter_udp_test: vm/tools/netfilter_udp_test.c | tool-output-directory
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -pthread -static -o $@ $<

$(TOOL_OUT)/packet_classifier_test: vm/tools/packet_classifier_test.c | tool-output-directory
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -O2 -static -o $@ $<

$(TOOL_OUT)/packet_mutation_test: vm/tools/packet_mutation_test.c | tool-output-directory
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -O2 -static -o $@ $<

$(TOOL_OUT)/proc_read_test: vm/tools/proc_read_test.c | tool-output-directory
	$(CC) -static -o $@ $<

$(TOOL_OUT)/proc_write_test: vm/tools/proc_write_test.c | tool-output-directory
	$(CC) -std=c11 -Wall -Wextra -Werror -static -o $@ $<

$(TOOL_OUT)/read_result_test: vm/tools/read_result_test.c | tool-output-directory
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -pthread -static -o $@ $<

$(TOOL_OUT)/run_as: vm/tools/run_as.c | tool-output-directory
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -static -o $@ $<

$(TOOL_OUT)/rmdir_telemetry_test: vm/tools/rmdir_telemetry_test.c | tool-output-directory
	$(CC) -std=c11 -Wall -Wextra -Werror -pthread -static -o $@ $<

$(TOOL_OUT)/secure_agent_fixture: vm/tools/secure_agent_fixture.c | tool-output-directory
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -static -o $@ $<

$(TOOL_OUT)/socket_owner_fixture: vm/tools/socket_owner_fixture.c | tool-output-directory
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -static -o $@ $<

$(TOOL_OUT)/status_field_test: vm/tools/status_field_test.c | tool-output-directory
	$(CC) -std=c11 -Wall -Wextra -Werror -static -o $@ $<

$(TOOL_OUT)/task_attribution_test: vm/tools/task_attribution_test.c | tool-output-directory
	$(CC) -std=c11 -Wall -Wextra -Werror -pthread -static -o $@ $<

$(TOOL_OUT)/tcp_session_fixture: vm/tools/tcp_session_fixture.c | tool-output-directory
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -static -o $@ $<

clean:
	@test "$(TOOL_OUT)" = .rootkitlings/build/tools || \
		{ echo 'clean does not accept TOOL_OUT' >&2; exit 2; }
	$(RM) $(GUEST_TOOLS)

doctor:
	@"$(CURDIR)/scripts/doctor"

compile-db:
	@"$(CURDIR)/scripts/compile-db"

validate:
	@"$(CURDIR)/scripts/validate"

validate-quick:
	@"$(CURDIR)/scripts/validate" --quick

test-checkers:
	@"$(CURDIR)/scripts/test-checkers"

test-solutions:
	@"$(CURDIR)/scripts/test-solutions"
