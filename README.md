# Rootkitlings

Build a Linux kernel rootkit one mechanism at a time.

Rootkitlings is a practical course in kernel modules, local control, agent
design, authenticated callbacks, operator tooling, and tradecraft.
It assumes no previous kernel-development or rootkit knowledge.
You learn each mechanism by building and proving it in a disposable QEMU
guest, never in the host kernel.

```text
25 lessons > 1 agent qualification > 4 milestones
           > 1 deployment lab > 1 operational final
```

## Requirements

See [DEPENDENCIES.md](DEPENDENCIES.md) for the complete host, build-container,
Go, web, image, and pinned third-party asset inventory.

- x86-64 Linux or macOS
- Go 1.27 or newer
- Docker or Podman
- QEMU 8.0 or newer
- `qemu-img`, OpenSSH, `curl`, and an ISO builder

`setup` checks the host, downloads the VM images, prepares the build container,
and generates the course keys and certificates:

```sh
./rklings setup
```

Local TLS uses a private ECDSA P-256 course CA.
Browsers show a trust warning unless `.rklings/certs/ca.crt` is imported
explicitly.
A DNS deployment uses a course server certificate generated for that hostname.

## Quick start

```sh
./rklings setup
./rklings list
./rklings info L00
```

## First exercise

L00 is the first contact with the course and with kernel development.
There is no terminology-only warm-up: you edit a real module, build it, load it
in a disposable guest, inspect its evidence, and unload it.

First prepare the toolchain and VM:

```sh
./rklings setup
./rklings list
```

Open
[L00: Build, Load, and Unload a Kernel Module](exercises/L00-kernel-module/README.md),
then inspect its exact contract:

```sh
./rklings info L00
```

Complete the declared TODOs, then use both execution paths:

```sh
./rklings run L00       # enter the disposable guest and operate the module
./rklings validate L00  # repeat the lifecycle proof in a clean guest
```

Every exercise follows the same operational loop:

```text
read the mission -> inspect the executable contract -> perform declared work
                 -> run and observe -> prove the advertised behavior
```

The exercise page must identify anything supplied by the course.
If a command, file, service, or mechanism was neither taught earlier nor
identified as supplied, that is a course defect rather than something you are
expected to guess.

Follow the ordered route shown by `./rklings list`.

```sh
./rklings info L00
./rklings validate L00
./rklings list
```

`setup` also generates the root `compile_commands.json` from the pinned kernel
toolchain, covering every learner and reference C source.
Regenerate it after adding source files with either command:

```sh
./rklings compile-db
make compile-db
```

`run` opens an interactive guest. `validate` performs an isolated build, boot,
verification, and teardown.

## Learning path

```text
L00–L04  Kernel foundation
   |     host/guest boundary · module lifecycle · owned state · procfs
   v
L05–L07  Escalation
   |     mkdir interception · credentials · PID targeting
   v
L08–L09  Interception
   |     kprobes · command dispatch
   +--> M00  Persistence and module hiding
              |
              v
L10–L13  Concealment
   |     target lists · files · PIDs · content · ports
   +--> M01  Complete local rootkit
              |
              v
D00      Deployment lab
   |     initial access · privilege escalation · deployment
   v
L14–L16  Userspace split
   |     usermode helper · G00 framed Go I/O · mTLS agent · kernel bridge
   v
L17–L19  Remote operations
   |     dispatch · shell · file transfer
   +--> M02  Complete remote operator
              |
              v
L20–L24  Tradecraft
   |     fileless loading · decoy routing · process and disk hygiene
   +--> M03  Integrated ghost agent
              |
              v
           P00  Operate and verify the complete system
```

Progression:

1. Implement the smallest working mechanism.
2. Verify its kernel or network behavior.
3. Integrate it at the next milestone.
4. Integrate the focused mechanism into the complete system.

Examples include one-shot execution before a persistent PTY, focused callback
panels before the complete operator, and disk-backed loading before fileless
loading.

Artifact handling changes at L20:

```text
L00–M02  /rklings/rklings.ko is staged for direct inspection and loading
L20      the module moves to memfd + finit_module
M03/P00  only the self-contained agent is staged; no standalone .ko exists
D00      the module stays on the attacker host until you transfer it
```

## System model

```text
Target VM                                      Operator host

+------------------+   mkdir control   +------------------+
| kernel module    | <---------------- | Go agent         |
| probes + hiding  |                   | mTLS callback    |
+------------------+                   +--------+---------+
                                               | TLS 1.3
                                               v
                                      +------------------+
                                      | operator server  |
                                      | terminal + web   |
                                      +------------------+
```

The kernel module never performs network communication.
The agent owns transport and translates remote requests into local operations.
The operator is an independent host process.

## Run and serve are separate

Start the target first:

```sh
./rklings run M02
```

Then start the operator in another host terminal:

```sh
./rklings serve M02
```

Open `http://127.0.0.1:8080`, or use the terminal console.
The mTLS listener defaults to the rootless port `8443`.

For an explicitly selected listener on port 443:

```sh
sudo ./rklings serve P00 --listen :443
```

Alternatively, grant only `CAP_NET_BIND_SERVICE` to the installed binary.
Running `rklings run P00` embeds the module into the agent and stages only that
self-contained agent.
Deployment remains explicit inside the guest.

## Commands

| Command             | Purpose                                              |
| ------------------- | ---------------------------------------------------- |
| `setup`             | Prepare dependencies, images, keys, and certificates |
| `list`              | Show the ordered course catalog                      |
| `info EXERCISE`     | Show work, supplied artifacts, knowledge, and proofs |
| `build EXERCISE`    | Build the artifacts declared by the exercise         |
| `run EXERCISE`      | Boot a guest and apply its explicit run contract     |
| `serve EXERCISE`    | Start its independent host server or operator        |
| `validate EXERCISE` | Verify in a clean guest, then tear it down           |
| `complete D00`      | Record active-guest deployment evidence              |
| `solution EXERCISE` | Show the reference solution                          |
| `reset EXERCISE`    | Restore its declared learner files                   |
| `compile-db`        | Regenerate `compile_commands.json`                   |
| `ssh NAME [USER]`   | Open a shell in a named running guest                |

Validation checks editable files before building.
Remaining `TODO` markers are reported with their file and line, so an untouched
exercise returns immediately.
Runtime failures show only actionable assertions; the complete guest transcript
is saved under `.rklings/logs/`.

Maintainers can validate published solutions through the same path:

```sh
./rklings validate L19
```

The complete reference suite chooses a conservative parallel worker count from
the machine's available CPU and memory.
Exercises run concurrently; cases that share one guest remain sequential and
deterministic.

```bash
make acceptance
```

During development, rerun one declarative case without executing the remaining
checks:

```bash
./rklings validate L19 --case download
```

## Repository

```text
rootkitlings/
+-- exercises/             lessons, milestones, D00, and P00
|   +-- L00-.../
|       +-- README.md      lesson guide
|       +-- Kbuild         learner workspace
|       +-- core/          learner source
|       +-- .rklings/      manifest, reference, and validation internals
+-- product/operator/      canonical P00 operator
+-- lib/webapp/            operator dashboard
+-- cmd/rklings/           CLI
+-- internal/              build, VM, validation, and server framework
+-- vm/                    guest runtime and verification tools
+-- course.toml            ordered course catalog
```

Build outputs live under `.rklings/build/`.
Downloaded images, certificates, SSH material, progress, and active-run metadata
live under the repository-local `.rklings/` directory.
Interactive runs use ephemeral overlays and SSH host ports.

## Development checks

```sh
make check
make acceptance
```

`make check` installs the locked web dependencies when necessary, then runs Go
tests and vet, formatting checks, shell syntax checks, documentation lint, and
the web lint/build. `make acceptance` runs the QEMU reference matrix.

## Test the final product

After a clean clone:

```sh
make build
./rklings setup
./rklings validate P00
```

That last command builds the complete kernel module, agent, and operator; boots
a clean guest; establishes an mTLS callback; verifies escalation, concealment,
persistence, shell and file transfer; checks the kernel log; and tears
everything down.

For an interactive product run, use two host terminals.
Start the operator:

```sh
./rklings serve P00
```

Then boot and stage the target:

```sh
./rklings run P00
```

Name VMs when running several victims, then open additional shells without
looking up QEMU ports:

```sh
./rklings run P00 --name victim-1
./rklings run P00 --name victim-2

./rklings ssh victim-1 student
./rklings ssh victim-2 root
```

Inside the guest, start the complete fileless deployment:

```sh
/rklings/rklings_agent
```

Open `http://127.0.0.1:8080` or use the operator terminal.
