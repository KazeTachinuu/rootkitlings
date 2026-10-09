KDIR ?= $(shell scripts/kernel build-dir)
MODULE_DIR ?=
TOOL_OUT ?= vm/tools
GUEST_TOOL_NAMES := \
	agent_fixture \
	cred_trigger_test \
	credential_snapshot_test \
	event_stream_test \
	getdents64_test \
	icmp_send \
	ioctl_test \
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

.PHONY: all help module clean compile-db test-solutions doctor validate validate-quick tools tool-output-directory

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

tool-output-directory:
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

$(TOOL_OUT)/icmp_send: vm/tools/icmp_send.c | tool-output-directory
	$(CC) -static -o $@ $<

$(TOOL_OUT)/ioctl_test: vm/tools/ioctl_test.c | tool-output-directory
	$(CC) -static -o $@ $<

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
