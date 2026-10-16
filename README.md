# Rootkitlings

Build a Linux kernel rootkit one mechanism at a time.

26 lessons, 4 milestones, 1 deployment exercise, 1 final project.
C for kernel stealth.
Go for the agent and server.
Every exercise runs in a disposable QEMU guest, never in the host kernel.

## Requirements

x86-64 Linux or macOS host with:

- Go 1.22+
- Docker or Podman
- QEMU 8.0+
- mkcert

```
rklings setup
```

Checks everything, downloads the VM image, pulls the build container, generates
SSH keys and certificates.
Prints install instructions for anything missing.

## Start

```
rklings setup          # one-time: deps, VM image, certs
rklings run l00        # build, boot VM, drop into shell
# edit exercises/l00-module-init/starter/core/main.c
rklings validate l00   # automated: build, boot, test, teardown
rklings status         # show progress
```

## Course

```text
Phase 1: Foundation (C)
  L00-L04  module lifecycle, state, procfs

Phase 2: Escalation (C)
  L05-L07  mkdir interception, credential escalation

Phase 3: Interception (C)
  L08-L09  kprobes from scratch, command dispatcher
  M00      persistence + module hiding

Phase 4: Concealment (C)
  L10-L13  target lists, hide files/PIDs/content/ports
  M01      full local rootkit (16 mkdir commands)

D00: Deploy
  Initial access, LPE, rootkit deployment against a live VM

Phase 5: The Split (C + Go)
  L14      call_usermodehelper (kernel launches agent)
  L15-L16  mTLS agent, mkdir bridge

Phase 6: Remote Operations (Go)
  L17-L19  command dispatch, remote shell, file transfer
  M02      operator TUI with full remote control

Phase 7: Tradecraft (Go)
  L20-L24  fileless loading, decoy routing, process masquerade, anti-forensics
  M03      ghost mode (all tradecraft integrated)

P00: Complete Rootkit
  Production deployment with real domain and obfuscation
```

## Architecture

```text
  kernel module (C)          agent (Go)            server (Go)
  stealth only               all network comms     operator tooling
  kprobes, hiding            mTLS to server :443   mTLS listener :443
  mkdir dispatch             os.Mkdir to kernel    webapp 127.0.0.1:8080
```

The kernel module never touches the network.
The agent handles all communication over mTLS.
The server provides operator control via TUI and web interface.

## CLI

| Command | What it does |
|---------|-------------|
| `rklings setup` | Check deps, download VM, generate keys and certs |
| `rklings build <ex>` | Cross-compile .ko and/or Go agent |
| `rklings run <ex>` | Build + boot VM + deploy + drop into shell |
| `rklings validate <ex>` | Automated test: build, boot, verify, teardown |
| `rklings status` | Show exercise progress |
| `rklings solution <ex>` | Print reference solution (requires prior validate pass) |
| `rklings reset <ex>` | Restore starter files |
| `rklings server` | Start operator server (M02/P00) |

## File layout

```text
rootkitlings/
  exercises/              student workspace
    l00-module-init/
      README.md
      starter/
  .rklings/               internals (hidden)
    solutions/
    validate/
    lab/
  cmd/rklings/            CLI source
  internal/               compiled into rklings
```

## VM

Ubuntu Minimal 26.04 cloud image, kernel 7.0.0-38-generic.
QEMU overlay per run gives clean state in seconds.
SSH on localhost:2222, agent callbacks to 10.0.2.2:443.
