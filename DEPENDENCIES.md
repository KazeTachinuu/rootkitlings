# Dependencies

Rootkitlings keeps its generated state in `.rklings/`. `./rklings setup` checks
host requirements, downloads pinned inputs, verifies them, and prepares the
build toolchains.

## Required host tools

| Dependency       | Requirement                            | Role          |
| ---------------- | -------------------------------------- | ------------- |
| Go               | 1.27 or newer                          | Go builds     |
| Podman or Docker | one runtime                            | Kernel builds |
| QEMU             | 8.0+, x86-64                           | Target VMs    |
| `qemu-img`       | required                               | VM overlays   |
| OpenSSH          | `ssh`, `scp`, `ssh-keygen`             | VM access     |
| `curl`           | required                               | Downloads     |
| ISO builder      | `xorriso`, `genisoimage`, or `mkisofs` | Seed ISOs     |

KVM is optional.
QEMU uses it when `/dev/kvm` is available and otherwise falls back to software
emulation.

## Images and lab assets

| Input              | Location                                                |
| ------------------ | ------------------------------------------------------- |
| Ubuntu 26.04 image | `.rklings/cache/`                                       |
| Ubuntu 24.04 image | `.rklings/cache/`                                       |
| Copy Fail PoC      | `exercises/D00-deploy/.rklings/assets/copy_fail_exp.py` |

Copy Fail source: [upstream repository][copy-fail].

[copy-fail]: https://github.com/theori-io/copy-fail-CVE-2026-31431

## Build containers

The complete definitions are in
[`toolchains.toml`](internal/build/container/toolchains.toml) and use immutable
base-image digests.

| Toolchain      | Base                  | Kernel headers      |
| -------------- | --------------------- | ------------------- |
| Course         | Ubuntu 26.04          | `7.0.0-38-generic`  |
| D00 deployment | Ubuntu 24.04 snapshot | `6.8.0-101-generic` |

Container packages are `gcc`, `make`, the selected Linux headers, and
`ca-certificates` when the snapshot archive is used.

## Go modules

The root CLI directly uses:

- `github.com/BurntSushi/toml`
- `github.com/google/shlex`
- `github.com/spf13/cobra`

The complete operator directly uses:

- `charm.land/bubbles/v2`
- `charm.land/bubbletea/v2`
- `charm.land/lipgloss/v2`
- `github.com/gorilla/websocket`
- `golang.org/x/term`

Exercise agents have their own `go.mod` and `go.sum` files.
Every exact direct and transitive version is recorded in the corresponding
`go.sum`.

## Web interface

Building the dashboard requires Node.js and npm.
Runtime dependencies are:

- React and React DOM
- xterm.js with the fit addon
- Lucide React

Development dependencies are Vite, Tailwind CSS, the React Vite plugin, React
type declarations, and Oxlint.
Exact versions are locked in
[`package-lock.json`](lib/webapp/package-lock.json).

## Development-only tools

| Tool              | Used by                            |
| ----------------- | ---------------------------------- |
| Git               | `rklings reset`                    |
| Node.js and npm   | Web and documentation checks       |
| Prettier          | `make format-docs`                 |
| markdownlint-cli2 | `make lint-docs`                   |
| `tree`            | Workspace diagrams                 |
| Python 3          | D00 transfer and guest-side server |

The host-side Python server is only a convenient D00 transfer method.
Any HTTP server reachable from the compromised guest can replace it.
