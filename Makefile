KDIR ?=
MODULE_DIR ?=
MODULE_OUT ?=
TOOL_OUT ?= $(CURDIR)/.rklings/build/tools
RKLINGS_FAIL_POINT ?= 0

GUEST_TOOL_NAMES := \
	mkdir_escalation_test \
	module_load_test \
	proc_read_test \
	secure_agent_fixture
GUEST_TOOLS := $(addprefix $(TOOL_OUT)/,$(GUEST_TOOL_NAMES))
READMES := README.md CLAUDE.md $(shell find exercises lib product vm \
	-path '*/node_modules' -prune \
	-o -name README.md -type f -print | sort)
WEB_STAMP := lib/webapp/node_modules/.package-lock.json
DOCS_STAMP := node_modules/.package-lock.json

.DEFAULT_GOAL := build

.PHONY: build test acceptance check web tools module clean help \
	compile-db tool-output-directory format-docs lint-docs lint-shell lint-format

build:
	go build -o rklings ./cmd/rklings

compile-db: build
	./rklings compile-db

test:
	go test ./...
	go vet ./...
	cd product/operator && go test ./... && go vet ./...

acceptance: build
	./rklings validate --all

$(WEB_STAMP): lib/webapp/package.json lib/webapp/package-lock.json
	npm --prefix lib/webapp ci

web: $(WEB_STAMP)
	npm --prefix lib/webapp run lint
	npm --prefix lib/webapp run build

check: test lint-format lint-shell lint-docs web
	git diff --check

lint-format:
	@test -z "$$(gofmt -l cmd internal product vm/fixtures)" || \
		{ gofmt -l cmd internal product vm/fixtures; exit 1; }

lint-shell:
	@find exercises vm -type f \( -path '*/checks/*' -o -name '*.sh' \) \
		! -name '*.py' -exec sh -n {} \;

$(DOCS_STAMP): package.json package-lock.json
	npm ci

format-docs: $(DOCS_STAMP)
	node scripts/workspace-trees.mjs $(READMES)
	npm run format:docs -- $(READMES)

lint-docs: $(DOCS_STAMP)
	npm run check:docs -- $(READMES)
	npx --yes markdownlint-cli2@0.23.3 $(READMES)
	node scripts/check-exercise-readmes.mjs $(READMES)
	node scripts/workspace-trees.mjs --check $(READMES)
	node --test scripts/check-diagrams.test.mjs
	node scripts/check-diagrams.mjs $(READMES)

tools: $(GUEST_TOOLS)

tool-output-directory:
	@test -n "$(strip $(TOOL_OUT))" && test "$(TOOL_OUT)" != / || \
		{ echo 'TOOL_OUT must name a directory' >&2; exit 2; }
	mkdir -p "$(TOOL_OUT)"

$(TOOL_OUT)/mkdir_escalation_test: vm/tools/mkdir_escalation_test.c \
	| tool-output-directory
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -static -o $@ $<

$(TOOL_OUT)/module_load_test: vm/tools/module_load_test.c \
	| tool-output-directory
	$(CC) -static -o $@ $<

$(TOOL_OUT)/proc_read_test: vm/tools/proc_read_test.c \
	| tool-output-directory
	$(CC) -static -o $@ $<

$(TOOL_OUT)/secure_agent_fixture: vm/tools/secure_agent_fixture.c \
	| tool-output-directory
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -static -o $@ $<

module:
	@test -n "$(MODULE_DIR)" || { echo 'MODULE_DIR is required' >&2; exit 2; }
	@test -n "$(KDIR)" || { echo 'KDIR is required' >&2; exit 2; }
	@if test -n "$(MODULE_OUT)"; then \
		set -- "MO=$(MODULE_OUT)"; \
	else \
		set --; \
	fi; \
	env -i PATH="$$PATH" LC_ALL=C LANG=C TZ=UTC \
		make -C "$(KDIR)" M="$(MODULE_DIR)" W=1 ARCH=x86_64 \
		CC=gcc HOSTCC=gcc RKLINGS_FAIL_POINT="$(RKLINGS_FAIL_POINT)" \
		"$$@" modules

clean:
	@test "$(TOOL_OUT)" = "$(CURDIR)/.rklings/build/tools" || \
		{ echo 'clean does not accept TOOL_OUT' >&2; exit 2; }
	$(RM) rklings $(GUEST_TOOLS) product/operator/server
	$(RM) -r lib/webapp/node_modules lib/webapp/dist node_modules

help:
	@printf '%s\n' \
		'Rootkitlings targets:' \
		'  build        Build ./rklings' \
		'  test         Run Go tests and static analysis' \
		'  acceptance   Validate all references with automatic concurrency' \
		'  web          Lint and build the operator dashboard' \
		'  check        Run all repository checks' \
		'  tools        Build the declared guest verification tools' \
		'  compile-db   Generate editor compile commands' \
		'  module       Build MODULE_DIR against KDIR' \
		'  format-docs  Format README files' \
		'  lint-docs    Lint README files' \
		'  clean        Remove binaries, node_modules, and build artifacts'
