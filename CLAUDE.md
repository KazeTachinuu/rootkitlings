# Rootkitlings Course

Educational rootkit engineering course.
C kernel module for stealth, Go agent for comms, Go server for operator control.

## Architecture (non-negotiable)

Three components with separate kernel, target, and operator responsibilities:

1. **Kernel module (C)**: stealth ONLY.
   Kprobes, hiding, persistence, mkdir dispatch.
   No networking.
2. **Agent (Go)**: all network comms.
   Course and local operator runs use rootless :8443; an external listener may
   explicitly select :443.
   Talks to kernel via os.Mkdir.
3. **Server (Go)**: operator tooling. `rklings serve <exercise>` starts the
   focused lab panel or integrated operator independently from the target VM.
   It defaults to :8443 with the dashboard on 127.0.0.1:8080; deployments may
   explicitly select :443.

## Curriculum: 32 exercises

25 lings + 1 agent qualification + 1 deployment + 4 milestones + 1 operational
final.
Every lesson introduces one bounded mechanism; milestones assess composition.
Every transition is grounded: "we just did X, now we need Y because Z."

## Lesson voice and contract

Use a restrained field-instructor voice: direct objectives, exact boundaries,
observable evidence, and explicit limitations.
Avoid lore, motivational filler, and generic language tutorials.

Every learner README uses this operational spine, in order:

1. `Mission`: one outcome and the concrete limitation from the previous stage.
2. `What you will observe`: evidence the learner can reproduce, not claims.
3. `Workspace`: exact editable and supplied boundaries.
4. Mechanism sections: only the model and APIs needed by this implementation.
5. `TODOs`: numbered edits in dependency order, with exact source locations.
6. `Test it`: commands, execution boundary, and expected evidence.
7. `Validate`: the behavior independently proved by the runner.
8. `Next`: the remaining limitation that creates the next exercise.

D00 and P00 are operational and omit `Implement it`.
Do not add generated contract blocks, internal concept slugs, generic failure
clinics, standalone API catalogs, repeated course history, or completed
solutions to learner pages.
Technical detail stays when it directly supports a TODO, invariant, command, or
observation; otherwise remove it.

`exercise.toml` is the executable contract.
For every exercise it must declare:

- prerequisites and previously taught requirements;
- only the new knowledge introduced here;
- editable learner files versus supplied black boxes; and
- behavioral validation cases.

`./rklings info <exercise>` renders that contract.
A mechanism used by learner work must be taught here, taught on the prerequisite
path, or declared as a supplied artifact.
There is no implicit fourth category.

### Phase 1: Kernel foundation (L00-L04, C)

"Before you can hide, you need to exist in the kernel."

- L00: module_init/exit, Kbuild, insmod/rmmod
- L01: goto cleanup chains, IS_ERR/PTR_ERR, reverse-order teardown
- L02: kzalloc/kfree, module_param, persistent opaque state
- L03: proc_create, seq_file, /proc/rklings
- L04: copy_from_user, proc write handler

### Phase 2: Escalation (L05-L07, C)

"Your module runs in the kernel.
Now make it do something an attacker needs: give root."

- L05: rklings_hook.h (black box), RK_ON_MKDIR callback, dmesg on mkdir match
- L06: prepare_kernel_cred(&init_task) + commit_creds. mkdir rklings.e -> root
- L07: immediate PID targeting with task lookup and credential lifetime rules.

### Phase 3: Interception (L08-L09, C)

"You used a black box to intercept mkdir.
Now understand what it does and build your own."

- L08: open rklings_hook.h, understand kprobes, rewrite from scratch
- L09: kretprobe entry+return, dispatcher parsing "rklings.<cmd>.<arg>"

### M00: Persistence + Stealth (C)

"Your module works until reboot, and anyone can see it in lsmod.
Fix both."

- filp_open + kernel_write for /etc/modules-load.d persistence
- list_del/list_add THIS_MODULE for module hiding
- +4 mkdir cmds: persist, unpersist, h.m, u.m

### Phase 4: Concealment (L10-L13, C)

"You survive reboot and lsmod can't see you.
But ls, ps, cat, and procfs TCP tables still can."

- L10: spinlock_t target list with add/remove/match
- L11: getdents64 kretprobe, linux_dirent64 buffer walk, memmove filter
- L12: ksys_read kretprobe, text line filtering (reinforces L11 pattern)
- L13: tcp4_seq_show kretprobe, seq_file count rewind, inet_sock->inet_sport

### M01: Covert Local Control (C)

"Assemble everything. 16 mkdir commands.
A complete local rootkit."

- Integrate supplied, previously validated subsystems through one ordered init,
  rollback, and teardown boundary. 16 mkdir commands.

### D00: Deploy

Start outside the target.
Initial access, privilege escalation, transfer, and rootkit deployment form one
exercise.

1. Initial access: VM runs a ~10-line Python HTTP server with command injection.
   Student exploits it from host, gets reverse shell as student.
2. LPE: CVE-2026-31431 "Copy Fail".
   Student runs 732-byte PoC, gets root.
3. Deploy: insmod M01 rootkit, verify hiding works.

- VM pinned to pre-patch kernel.
  Vulnerable service started by cloud-init.
- "You're in.
  You're root.
  Your rootkit is loaded.
  But you left shell history, login records, a binary on disk, and you had to
  type every command by hand.
  Phase 5 fixes all of that."

### Phase 5: The Split (L14 C, G00 and L15-L16 Go)

"D00 was manual and loud.
The kernel can launch programs.
Let's make it launch an agent that phones home."

- L14: call_usermodehelper to launch agent binary from kernel
- G00: complete length-framed TCP I/O and practical Go error handling
- L15: Go agent with mTLS (crypto/tls, x509.CertPool, tls.Dial, go:embed certs).
  ~60 lines.
- L16: os.Mkdir bridge.
  Server sends "HIDE FILE x", agent does mkdir("rklings.h.f.x").

### Phase 6: Remote Operations (L17-L19, Go)

"The agent connects.
Now make it useful: commands, shell access, file transfer."

- L17: bufio.Scanner command dispatch over mTLS.
  PING, STATUS, HIDE, PERSIST, ESCALATE.
- L18: exec.Command("sh", "-c", args).CombinedOutput(). ~5 lines.
- L19: os.Open + io.Copy file download/upload over TLS.
  Size line then raw bytes (like HTTP Content-Length / SCP).

### M02: Remote Control (Go + C)

"Full remote-control milestone.
Browser operator plus terminal fallback. tcpdump sees HTTPS to a normal-looking
server.
All commands work remotely."

- Supplied terminal and browser operator surfaces.
- Student-owned seams: server-side mTLS, serialized protocol exchange, and PTY
  lifecycle. tcpdump shows TLS application data.

### Phase 7: Tradecraft (L20-L24, Go)

"Everything works remotely.
But the agent binary is on disk, the .ko was loaded from a file, and the process
shows in ps.
An analyst would find you in minutes."

- L20: unix.MemfdCreate(MFD_CLOEXEC) + unix.FinitModule.
  Fileless .ko loading. go:embed .ko bytes. ~10 lines.
  PUMAKIT technique.
- L21: unix.MemfdCreate (NO MFD_CLOEXEC) + unix.Exec("/proc/self/fd/N").
  MagisterQuis technique.
  Delete before exec. ~8 lines.
- L22: tls.Config{ClientAuth: RequestClientCert} + manual x509.Verify.
  No cert = decoy website, right cert = C2.
  Same port.
  Cobalt Strike malleable C2.
- L23: crypto/rand jitter + prctl(PR_SET_NAME, "[kworker/0:1]") masquerade (13
  chars, fits TASK_COMM_LEN=16) + prctl(PR_SET_DUMPABLE, 0) +
  setrlimit(RLIMIT_CORE, 0) + auto mkdir hide PID/port/binary on connect.
  BPFDoor + Sliver.
- L24: os.Chtimes timestomping + Clearenv + history scrub + utmp/wtmp login
  record scrub + journal tampering.
  Anti-forensics.

### M03: Ghost

Compose the supplied L20 through L24 mechanisms in dependency order through the
single student-owned deployment function.
Verify each claimed observation path directly and state the remaining limits.

### P00: Operate and Verify the Complete System

- Complete, read-only product workspace; this is not an implementation capstone.
- Manifest-driven build properties: `go build -ldflags="-s -w" -trimpath`.
- Direct TLS listener with a user-controlled DNS name in the course certificate.
- Full test: deploy against a clean VM and verify the implemented controls,
  operator commands, and persistence lifecycle.

## Integrated mkdir command set

From L10 onward, conceal and restore are symmetric: pass the same exact value to
`h` and `u`.
There is no numeric-index compatibility path.

```
rklings.e              # elevate the shell that invoked mkdir
rklings.e.<pid>        # elevate a selected process immediately
rklings.ping           # ping (L09)
rklings.status         # status (L09)
rklings.persist        # write /etc/modules-load.d (M00)
rklings.unpersist      # remove persistence (M00)
rklings.h.m            # hide module from lsmod (M00)
rklings.u.m            # unhide module (M00)
rklings.h.f.<name>     # hide file from ls (L11)
rklings.u.f.<name>     # unhide file by value
rklings.h.i.<pid>      # hide PID from ps (L11)
rklings.u.i.<pid>      # unhide PID by value
rklings.h.c.<str>      # hide content line from reads (L12)
rklings.u.c.<str>      # unhide content by value
rklings.h.p.<port>     # hide local port from /proc/net/tcp{,6} (L13)
rklings.u.p.<port>     # unhide port by value
rklings.info           # return the in-memory target ledger (L16)
rklings.quit           # quit (L16)
```

File targets match directory-entry prefixes.
PID and port targets match exact values.
Content targets remove the complete matching line from ordinary non-executable
file reads; terminals, pipes, sockets, and executable files are outside that
filter.

## mTLS trust domains

```
Course CA (private, generated by rklings setup)
  Signs: lab server and agent client certificates
  Trusted by: agent (RootCAs) and server (ClientCAs)
  Stored: .rklings/certs/ca.key + ca.crt

P00 DNS listener
  Uses: the private course CA with the selected DNS name in the server cert
  Generated by: rklings setup --server-name <name>
```

Agent certs embedded at build time via go:embed (Sliver model).
Each build = unique cert + unique hash.
The binary IS the credential.
Target never generates anything.

L22 decoy routing: RequestClientCert (not Require).
Server sends CertificateRequest to ALL clients.
Browsers silently respond empty.
TLS 1.3 encrypts this exchange.
Three outcomes: no cert = decoy, wrong cert = decoy, right cert = C2.

## Infrastructure

### VM

- Ubuntu Minimal 26.04 cloud image, UNMODIFIED
- Kernel 7.0.0-38-generic with the course image configuration
- Already has: openssh-server, coreutils, procps, kmod, iproute2, curl,
  util-linux
- Install NOTHING extra.
  VM is a target, not a dev environment.
- QEMU overlay per run = clean state in ~3s
- Cloud-init seed ISO: creates root + student (uid 1000, no sudo), injects SSH
  pubkey into both
- SSH key pair generated by `rklings setup`, stored at .rklings/ssh/id_ed25519
- Same key for both users. `rklings run` SSHs as root or student depending on
  exercise.

### Build

- .ko cross-compilation: Docker/Podman container (ubuntu:26.04 + gcc + make +
  linux-headers-7.0.0-38-generic)
- Go agent: `GOOS=linux GOARCH=amd64 go build` (native on host)
- `rklings build` detects podman or docker (OCI compatible, same commands)
- Fallback: compile in VM via SSH if no container runtime

### QEMU Networking

- User-mode networking (no root required)
- Host accessible from VM at 10.0.2.2 (QEMU default gateway)
- Lab agents connect to 10.0.2.2:8443; an external deployment may select :443
- SSH forwarding uses a free ephemeral host port

### Ports (host side)

- :443 on 0.0.0.0 - optional privileged mTLS callback
- :8443 on 0.0.0.0 - default rootless callback for course and local runs
- :8080 on 127.0.0.1 - current exercise lab, or M02 operator dashboard
- :4444 on 127.0.0.1 - validation relay into the real L15-L19 connection
- :8088 on 127.0.0.1 - D00 guest status service (QEMU forward to guest :8080)
- :2222 on localhost - SSH forwarded to VM

### Protocol

Plaintext text commands over mTLS.
Newline-delimited.
No custom framing.
TLS handles everything.
File transfers: size on one line (decimal bytes), then raw bytes.
Receiver reads size, then io.LimitReader for exactly that many bytes.
Same pattern as HTTP Content-Length / SCP.
No base64, no encoding.

### Host dependencies

Docker/Podman, Go (1.27+), QEMU, qemu-img, OpenSSH, curl, and an ISO builder.
P00 adds no required host dependency.

### `rklings setup` dependency checking

Runs on first use or explicitly.
Checks everything, reports all failures at once (not one-by-one).
Prints clear install instructions per platform when something is missing.

```
$ rklings setup
  [+] Go 1.27.0          (go version)
  [+] Docker 27.1.1      (docker --version / podman --version)
  [+] QEMU 9.0.2         (qemu-system-x86_64 --version)
  [ ] VM image            downloading ubuntu-26.04 image...
  [+] Build image         pulling rklings-build:7.0.0-38...
  [+] SSH key             .rklings/ssh/id_ed25519
  [+] rklings CA          .rklings/certs/ca.crt
  [+] Server cert         .rklings/certs/server.crt
  Ready.
```

Missing dep example:

```
$ rklings setup
  [+] Go 1.27.0
  [ ] Docker              not found
  [ ] Podman              not found
  [+] QEMU 9.0.2

  Missing dependencies:

  Docker or Podman (need one):
    macOS:   brew install --cask docker
    Ubuntu:  sudo apt install docker.io
    Arch:    sudo pacman -S docker

```

Minimum versions: Go >= 1.22, QEMU >= 8.0. Docker/Podman any recent version.
Detection: which/command -v, parse --version output.
Platform detection: runtime.GOOS for install instructions.

## File Layout

```
rootkitlings/
  exercises/                    # STUDENT WORKSPACE (visible)
    L00-kernel-module/
      README.md
      Kbuild
      core/
      .rklings/                 # manifest, checks, reference
    L15-mtls-connect/
      README.md
      main.go
      go.mod
      .rklings/
  lib/webapp/                   # operator UI source and built assets
  cmd/rklings/                  # CLI binary source
    main.go
  internal/                     # compiled into rklings
    exercise/
    vm/
    lab/
    validate/
```

## rklings CLI Commands

- `rklings setup` - check deps, download VM image, pull build image, generate
  SSH key, generate CA + server cert
- `rklings build <exercise>` - compile .ko (Docker) and/or Go agent (native
  cross-compile), generate client cert if needed
- `rklings run <exercise>` - build and stage exactly the artifacts declared by
  `[run]`, perform only its explicit load/start actions, boot QEMU, and open the
  exercise shell.
  It never starts a host-side server.
  P00 stages its kernel and agent but loads and starts neither.
  Exit tears down the VM.
- `rklings validate <exercise>` - fully automated: boots VM, loads module if
  needed, runs agent if needed, checks real behavior, tears down.
  Editable-file TODOs fail during preflight before any build or boot.
  Failed guest checks print concise diagnostics and save the full transcript
  under `.rklings/logs/`.
- `rklings list` - show the ordered route, progress, and next exercise
- `rklings solution <exercise>` - print reference solution (requires prior
  validation pass)
- `rklings reset <exercise>` - restore the declared learner files from Git
- `rklings serve <exercise>` - start the exercise's independent host-side lab
  panel or full operator

## Kernel Module

All kernel exercises produce `rklings.ko`.
One module, grows over exercises.
Each kernel exercise workspace includes a Kbuild file.

- L00-L09: single file.
  Kbuild: `obj-m := rklings.o`
- M00+: multi-file.
  Kbuild with `ccflags-y := -I$(src)/include`:
  - M00: `init.o dispatch.o cred.o module.o`
  - M01: `init.o dispatch.o escalation.o module.o target.o filter.o`

`rklings build` runs make inside the Docker container targeting the learner
directory.

### Command Dispatch

Table-driven function pointer registry.
Every subsystem owns a static command table:

```c
typedef int (*rk_handler_t)(const char *arg);

struct rk_cmd_entry {
    const char *key;
    rk_handler_t handler;
};

static const struct rk_cmd_entry filter_cmds[] = {
    { "h.f", cmd_hide_file },
    { "u.f", cmd_unhide_file },
};

int rk_filter_init(void) {
    return rk_cmds_register(filter_cmds, ARRAY_SIZE(filter_cmds));
}
```

Dispatch progression:

- L09: static `cmd_table[]`, exact-match (first dot splits cmd from arg)
- L11-L13: static `cmd_table[]`, longest-prefix match
- M00+: dynamic `rk_cmd_register()`, longest-prefix match

### pr_info Convention

- L00-L09: pr_info on load, unload, and key actions (mkdir intercept,
  escalation). dmesg is the only feedback channel.
  Students need it.
- L10+: pr_info on load/unload only.
  Commands are verified by real system behavior (ls, ps, cat, lsmod, and
  procfs).
  No command-level pr_info.
- P00: ZERO pr_info.
  OPSEC rule.

### Workspace TODO Style

One numbered TODO per coherent student decision.
Keep the source comment to one line and say what must be completed, not how to
implement it:

```c
// TODO 3: dispatch the longest matching command
```

Never leak the implementation recipe:

```c
// BAD: TODO 3: use strncmp to compare suffix with each registry[i].key,
//      track longest match, then call best->handler(arg)
```

TODO numbers are local to one exercise and start at 1. Their order follows the
implementation dependency, not alphabetical filenames.
The README TODO map, source comment, explanation heading, reference solution,
and validator must all describe the same work.

### Exercise audit protocol

`REVIEW.md` is the canonical, mandatory procedure for every exercise review.
Read it completely before changing an exercise.
Do not perform a prose-only pass: audit intent, knowledge, starter code, README,
reference solution, validation, and curriculum transitions as one learner
experience.

## Exercise Chain

General pattern: L(n) solution informs the L(n+1) workspace.
But this is NOT a hardcoded rule.
Each workspace contains only what the student needs for its next concept.
Clean, minimal.
If the previous solution has code irrelevant to the next lesson, drop it.
Workspaces merge relevant lings but only the parts that serve the module's goal.
Quality of learning > mechanical chain consistency.
Validation uses ~15 shared check primitives.
Lab servers compose (L18 imports L17 handler + adds exec panel).

## Exercise Metadata

Each `.rklings/exercise.toml` declares its workspace, VM user, prerequisites,
tools, runtime mode, real lab harness requirements, and verification cases.
The catalog loader rejects missing route entries, files, checks, and unknown
tools.

## Validation Philosophy

`rklings validate` tests REAL behavior, not tautologies.
Every check verifies the actual mechanism:

- "File hidden from ls" = run ls, confirm file absent from output, then cat the
  file to prove it still exists
- "Module loaded" = check /proc/modules AND verify the module's proc entry works
- "Port hidden" = inspect `/proc/net/tcp`, confirm the local port row is absent,
  then connect to prove the socket still works
- "Escalation works" = run id as student, mkdir rklings.e, run id again, confirm
  uid=0
- "mTLS connected" = verify server received valid client cert AND TLS 1.3
  negotiated Never: "does the file exist", "did insmod return 0", "is the string
  in dmesg" as the sole check.

## State Tracking

Exercise progress stored in .rklings/progress.json.

- `rklings validate` writes {exercise: timestamp} on pass
- `rklings solution` is always available; viewing it does not change progress
- `rklings list` reads this file to show progress and the next exercise
- `rklings reset` removes the exercise entry

## Naming Conventions

- Kernel files by subsystem (Reptile): main.c, dispatch.c, cred.c, targets.c,
  dir.c, file.c, network.c, module.c, persist.c
- Kernel functions verb_noun (Diamorphine): escalate_cred(), module_hide(),
  filter_dir(), target_add(), is_hidden()
- Agent packages by capability (Sliver): transport.go, mkdir.go, dispatch.go,
  handlers/, evasion/
- Exercise dirs: L00-kernel-module/, uppercase prefix, hyphenated, describes
  technique
- No invented terminology.
  Names from kernel source, Go docs, or MITRE ATT&CK only.

## README Format

```
# L15: Concrete Outcome
## Mission
## What you will observe
## Workspace
[only mechanism sections required by this exercise]
## TODOs
[numbered coordinate map, then one explanation per TODO]
## Test it
## Validate
## Next
```

D00 and P00 omit `TODOs` because they are operational verification exercises.
Use diagrams only when they make a lifecycle, boundary, or data flow materially
clearer than prose.
No hints system.

## Lab Servers

L15-L19 have focused, independent live panels.
M02 runs the full operator dashboard.
Later networked exercises reuse the real mTLS harness for automated
verification; checks talk through the actual guest connection, never a mock.
L22 runs its decoy/C2 server directly and verifies all three certificate routing
outcomes against the real listener.

## Verified Kernel Hook Points (kernel 7.x, Ubuntu 26.04)

All verified online against kernel source.
No hallucinated symbols.

- L05-L09, `__x64_sys_mkdir`: syscall wrapper.
  The inner `pt_regs` comes from `regs_get_kernel_argument(regs, 0)`; the
  pathname is in `uregs->di`.
- L05-L09, `__x64_sys_mkdirat`: syscall wrapper.
  The pathname is in the inner `pt_regs` at `uregs->si`.
- L11, `__x64_sys_getdents64`: syscall wrapper.
  The dirent buffer is in the inner `pt_regs` at `uregs->si`.
- L12, `ksys_read`: regular function.
  Arguments are directly in `regs->di`, `regs->si`, and `regs->dx`.
- L13, `tcp4_seq_show` and `tcp6_seq_show`: regular functions.
  The `seq_file` and socket are directly in `regs->di` and `regs->si`.
- L14+, `inet_sk_diag_fill`: regular function.
  The socket and `sk_buff` are directly in `regs->di` and `regs->dx`.
- L14, `call_usermodehelper`: exported API called directly by module code.
- M00, `filp_open` and `kernel_write`: exported APIs called directly by module
  code.

Syscall wrappers (`__x64_sys_*`): args inside inner pt_regs (double
indirection).
Regular functions (`ksys_read`, `tcp*_seq_show`): args directly in registers
(standard calling convention).
This is the #1 gotcha.
Existing legacy code handles both patterns correctly.

Total kretprobes in final rootkit: 7 (mkdir, mkdirat, getdents64, ksys_read,
tcp4_seq_show, tcp6_seq_show, inet_sk_diag_fill).

## rklings_hook.h (L05-L07 black box)

Placed directly in each L05-L07 workspace.
Student sees the file, README says "black box, just use the API." API surface
for students:

- `RK_ON_MKDIR(name) { ... }` - define handler, receives basename as const
  char\*
- `rk_hook_start()` - call from module_init, returns 0 or -errno
- `rk_hook_stop()` - call from module_exit.
  Internally, the helper probes `__x64_sys_mkdir` and `__x64_sys_mkdirat`, uses
  `strncpy_from_user`, and suppresses the requested directory with
  `put_user('\0')`.
  L08 opens this file.
  L09 discards it and writes the kretprobe directly.

## P00 External Listener

An external deployment may bind the direct TLS listener to `:443`.
Run `rklings setup --server-name c2.example.com` before building so the embedded
course CA verifies the operator hostname.
Use an unprivileged high port or grant `CAP_NET_BIND_SERVICE` when root is
undesirable.

## Attack Chain Context (updated 2026-10-16)

The deployment sequence covers:

1. Initial access -- PRACTICAL LAB: command injection in vulnerable service (D00
   step 1)
2. Privilege escalation -- PRACTICAL LAB: CVE-2026-31431 "Copy Fail" (D00
   step 2)
3. Kernel module loading (insmod, memfd+finit_module) -- L00, L20
4. Stealth (hiding, filtering, process masquerade) -- L10-L13, L21-L24
5. Persistence (modules-load.d, boot survival) -- M00
6. C2 callback (mTLS agent, command dispatch) -- L15-L19

## VM User Model

- L00-L05, L08-L09: root (building the module, need insmod)
- L06-L07, L10-L13: student with module pre-loaded (test capabilities, escalate
  via rootkit)
- L14-L19: root (building agent, deploying)
- L20 and the anti-forensics/integration exercises run as root because
  `finit_module`, wtmp/journal changes, and full deployment require privilege.
- L21 and L23 isolate user-space techniques and can run as student.
- M00-M02: depends on module focus (root for build, student for test)

Cloud-init creates both users.
Serial console auto-login as exercise-appropriate user.

## OPSEC Rules

- NEVER print to dmesg in P00 (earlier exercises use dmesg for learning, P00
  does not)
- NEVER reference real rootkit projects by name in the codebase
- Module signing (CONFIG_MODULE_SIG_FORCE) is NOT set (students load unsigned
  modules, but all other hardening is on)

## Style Rules

- No emojis.
  Use SVG icons (monochrome, currentColor).
- No em dashes.
  Use commas, colons, periods, parentheses.
- No Co-Authored-By: Claude in commits.
- Kernel code: clear, elegant, step-by-step.
  No vtable bloat.
- Less is more: simplest correct solution, delete over add.
- No fake APIs: use real kernel/mTLS APIs directly.
- Templates only for debug exercises, never generic shells.
