# Rootkitlings Course

Educational rootkit engineering course.
C kernel module for stealth, Go agent for comms, Go server for operator control.

## Architecture (non-negotiable)

Three components matching real implant architecture (Sliver, PUMAKIT):

1. **Kernel module (C)**: stealth ONLY. Kprobes, hiding, persistence, mkdir dispatch. No networking.
2. **Agent (Go)**: all network comms. mTLS to server on :443. Talks to kernel via os.Mkdir.
3. **Server (Go)**: operator tooling. mTLS listener on :443, webapp on 127.0.0.1:8080. reeflective/console TUI + lipgloss.

## Curriculum: 32 exercises

26 lings + 1 deployment + 4 modules + 1 project.
Every exercise teaches ONE concept.
Every transition is grounded: "we just did X, now we need Y because Z."

### Phase 1: Foundation (L00-L04, C)
"Before you can hide, you need to exist in the kernel."
- L00: module_init/exit, Kbuild, insmod/rmmod
- L01: goto cleanup chains, IS_ERR/PTR_ERR, reverse-order teardown
- L02: kmalloc/kfree, module_param, persistent struct
- L03: proc_create, seq_file, /proc/rklings
- L04: copy_from_user, proc write handler

### Phase 2: Escalation (L05-L07, C)
"Your module runs in the kernel.
Now make it do something an attacker needs: give root."
- L05: rklings_hook.h (black box), RK_ON_MKDIR callback, dmesg on mkdir match
- L06: prepare_kernel_cred(&init_task) + commit_creds. mkdir rklings.e -> root
- L07: atomic_t deferred credential delivery by PID. mkdir rklings.e.<pid>

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
But ls, ps, cat, and ss still can."
- L10: spinlock_t target list with add/remove/match
- L11: getdents64 kretprobe, linux_dirent64 buffer walk, memmove filter (HARDEST exercise)
- L12: ksys_read kretprobe, text line filtering (reinforces L11 pattern)
- L13: tcp4_seq_show kretprobe, seq_file count rewind, inet_sock->inet_sport

### M01: Covert Local Control (C)
"Assemble everything.
16 mkdir commands.
A complete local rootkit."
- Assemble all kretprobes + target lists + escalation. 16 mkdir commands.

### D00: Deploy
"You built a rootkit.
Now deploy it against a real target.
From the outside."
Full kill chain.
Initial access + LPE + rootkit deployment in one exercise.
1. Initial access: VM runs a ~10-line Python HTTP server with command injection. Student exploits it from host, gets reverse shell as student.
2. LPE: CVE-2026-31431 "Copy Fail". Student runs 732-byte PoC, gets root.
3. Deploy: insmod M01 rootkit, verify hiding works.
- VM pinned to pre-patch kernel. Vulnerable service started by cloud-init.
- "You're in. You're root. Your rootkit is loaded. But you left shell history, login records, a binary on disk, and you had to type every command by hand. Phase 5 fixes all of that."

### Phase 5: The Split (L14 C, L15-L16 Go)
"D00 was manual and loud.
The kernel can launch programs.
Let's make it launch an agent that phones home."
- L14: call_usermodehelper to launch agent binary from kernel
- L15: Go agent with mTLS (crypto/tls, x509.CertPool, tls.Dial, go:embed certs). ~60 lines.
- L16: os.Mkdir bridge. Server sends "HIDE FILE x", agent does mkdir("rklings.h.f.x").

### Phase 6: Remote Operations (L17-L19, Go)
"The agent connects.
Now make it useful: commands, shell access, file transfer."
- L17: bufio.Scanner command dispatch over mTLS. PING, STATUS, HIDE, PERSIST, ESCALATE.
- L18: exec.Command("sh", "-c", args).CombinedOutput(). ~5 lines.
- L19: os.Open + io.Copy file download/upload over TLS. Size line then raw bytes (like HTTP Content-Length / SCP).

### M02: Remote Control (Go + C)
"Full C2.
Operator TUI.
tcpdump sees HTTPS to a normal-looking server.
All commands work remotely."
- Full C2. Operator TUI (reeflective/console). tcpdump shows HTTPS. All commands work remotely.

### Phase 7: Tradecraft (L20-L24, Go)
"Everything works remotely.
But the agent binary is on disk, the .ko was loaded from a file, and the process
shows in ps.
An analyst would find you in minutes."
- L20: unix.MemfdCreate(MFD_CLOEXEC) + unix.FinitModule. Fileless .ko loading. go:embed .ko bytes. ~10 lines. PUMAKIT technique.
- L21: unix.MemfdCreate (NO MFD_CLOEXEC) + unix.Exec("/proc/self/fd/N"). MagisterQuis technique. Delete before exec. ~8 lines.
- L22: tls.Config{ClientAuth: RequestClientCert} + manual x509.Verify. No cert = decoy website, right cert = C2. Same port. Cobalt Strike malleable C2.
- L23: crypto/rand jitter + prctl(PR_SET_NAME, "[kworker/0:1]") masquerade (13 chars, fits TASK_COMM_LEN=16) + prctl(PR_SET_DUMPABLE, 0) + setrlimit(RLIMIT_CORE, 0) + auto mkdir hide PID/port/binary on connect. BPFDoor + Sliver.
- L24: os.Chtimes timestomping + Clearenv + history scrub + utmp/wtmp login record scrub + journal tampering. Anti-forensics.

### M03: Ghost
"All tradecraft integrated.
Run every defender tool.
Find nothing."
- All tradecraft integrated. Every defender tool finds nothing.

### P00: Complete Rootkit
"Production.
Real domain.
Real target.
Real obfuscation.
Ship it."
- Build hardening: `go build -ldflags="-s -w" -trimpath` + garble obfuscation.
- Cloudflare Quick Tunnel (default) or DuckDNS + Let's Encrypt (extra credit).
- README covers: JA3/JA4 TLS fingerprinting awareness, domain fronting concept, DNS canary domains.
- Full test: deploy against clean VM, verify all stealth, all tradecraft, all commands work.

## mkdir Command Set (18 total)

```
rklings.e              # escalate self (L06)
rklings.e.<pid>        # escalate PID (L07)
rklings.ping           # ping (L09)
rklings.status         # status (L09)
rklings.persist        # write /etc/modules-load.d (M00)
rklings.unpersist      # remove persistence (M00)
rklings.h.m            # hide module from lsmod (M00)
rklings.u.m            # unhide module (M00)
rklings.h.f.<name>     # hide file from ls (L11)
rklings.u.f.<idx>      # unhide file by index (L11)
rklings.h.i.<pid>      # hide PID from ps (L11)
rklings.u.i.<idx>      # unhide PID (L11)
rklings.h.c.<str>      # hide content line from reads (L12)
rklings.u.c.<idx>      # unhide content (L12)
rklings.h.p.<port>     # hide port from ss/netstat (L13)
rklings.u.p.<idx>      # unhide port (L13)
rklings.list           # list targets (L16)
rklings.quit           # quit (L16)
```

## mTLS: Three-CA System

```
CA #1: rklings CA (private, generated by rklings setup)
  Signs: agent client certs (unique per build, CN = agent ID)
  Trusted by: server (tls.Config.ClientCAs)
  Stored: ~/.rklings/certs/ca.key + ca.crt

CA #2: mkcert CA (local development)
  Signs: server TLS cert for lab
  Trusted by: student's browser (green lock)
  Used in: L15-L22 lab exercises

CA #3: Let's Encrypt (production)
  Signs: server TLS cert for real domain
  Used in: P00 only
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
- Kernel 7.0.0-38-generic, production-hardened (KSPP, all protections on)
- Already has: openssh-server, coreutils, procps, kmod, iproute2, curl, util-linux
- Install NOTHING extra. VM is a target, not a dev environment.
- QEMU overlay per run = clean state in ~3s
- Cloud-init seed ISO: creates root + student (uid 1000, no sudo), injects SSH pubkey into both
- SSH key pair generated by `rklings setup`, stored at ~/.rklings/ssh/id_ed25519
- Same key for both users. `rklings run` SSHs as root or student depending on exercise.

### Build
- .ko cross-compilation: Docker/Podman container (ubuntu:26.04 + gcc + make + linux-headers-7.0.0-38-generic)
- Go agent: `GOOS=linux GOARCH=amd64 go build` (native on host)
- `rklings build` detects podman or docker (OCI compatible, same commands)
- Fallback: compile in VM via SSH if no container runtime

### QEMU Networking
- User-mode networking (no root required)
- Host accessible from VM at 10.0.2.2 (QEMU default gateway)
- Agent in VM connects to 10.0.2.2:443 to reach host server
- Port forwarding: hostfwd=tcp::2222-:22 (SSH)

### Ports (host side)
- :443 on 0.0.0.0 - mTLS agent callback (server/lab)
- :8080 on 127.0.0.1 - lab webapp / operator dashboard
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
Docker/Podman, Go (1.22+), QEMU, mkcert.
Four things.
P00-only: garble (`go install mvdan.cc/garble@latest`), cloudflared.

### `rklings setup` dependency checking

Runs on first use or explicitly.
Checks everything, reports all failures at once (not one-by-one).
Prints clear install instructions per platform when something is missing.

```
$ rklings setup
  [+] Go 1.22.4          (go version)
  [+] Docker 27.1.1      (docker --version / podman --version)
  [+] QEMU 9.0.2         (qemu-system-x86_64 --version)
  [+] mkcert 1.4.4       (mkcert --version)
  [ ] VM image            downloading ubuntu-26.04-minimal-cloudimg-amd64.img (407 MB)...
  [+] Build image         pulling rklings-build:7.0.0-38...
  [+] SSH key             ~/.rklings/ssh/id_ed25519
  [+] rklings CA          ~/.rklings/certs/ca.crt
  [+] Server cert (mkcert) ~/.rklings/certs/server.crt
  Ready.
```

Missing dep example:
```
$ rklings setup
  [+] Go 1.22.4
  [ ] Docker              not found
  [ ] Podman              not found
  [+] QEMU 9.0.2
  [ ] mkcert              not found

  Missing dependencies:

  Docker or Podman (need one):
    macOS:   brew install --cask docker
    Ubuntu:  sudo apt install docker.io
    Arch:    sudo pacman -S docker

  mkcert:
    macOS:   brew install mkcert
    Ubuntu:  go install filippo.io/mkcert@latest
    Arch:    sudo pacman -S mkcert
```

Minimum versions: Go >= 1.22, QEMU >= 8.0.
Docker/Podman any recent version.
Detection: which/command -v, parse --version output.
Platform detection: runtime.GOOS for install instructions.

## File Layout

```
rootkitlings/
  exercises/                    # STUDENT WORKSPACE (visible)
    l00-module-init/
      README.md
      starter/
    l15-mtls-connect/
      README.md
      starter/
        main.go
        go.mod
  .rklings/                     # INTERNALS (hidden dotdir)
    solutions/
      l00/main.c
      l15/main.go
    validate/
      l00.go
      l15.go
    lab/
      l15/server.go
      l15/assets/
    image.lock                  # pinned VM image URL + SHA256
  cmd/rklings/                  # CLI binary source
    main.go
  internal/                     # compiled into rklings
    exercise/
    vm/
    lab/
    validate/
```

## rklings CLI Commands

- `rklings setup` - check deps, download VM image, pull build image, generate SSH key, generate CA + server cert
- `rklings build <exercise>` - compile .ko (Docker) and/or Go agent (native cross-compile), generate client cert if needed
- `rklings run <exercise>` - build + boot QEMU (overlay) + SCP binaries + start lab server if needed. SSHs into VM as exercise-appropriate user (root or student). Exit tears down everything.
- `rklings validate <exercise>` - fully automated: boots VM, loads module if needed, runs agent if needed, checks real behavior, tears down. No manual steps.
- `rklings status` - show exercise progress (reads ~/.rklings/progress.json)
- `rklings solution <exercise>` - print reference solution (requires prior validation pass)
- `rklings reset <exercise>` - git checkout exercises/<dir>/starter/ (restores clean state)
- `rklings server` - start full operator server (M02/P00)

## Kernel Module

All kernel exercises produce `rklings.ko`.
One module, grows over exercises.
Each kernel exercise starter includes a Kbuild file.
- L00-L09: single file. Kbuild: `obj-m := rklings.o`
- M00+: multi-file. Kbuild: `obj-m := rklings.o` + `rklings-objs := main.o dispatch.o cred.o ...` `rklings build` runs make inside the Docker container targeting the starter directory.

## Exercise Chain

General pattern: L(n) solution informs L(n+1) starter.
But this is NOT a hardcoded rule.
L(n+1) starter contains only what the student needs to learn the next concept.
Clean, minimal.
If the previous solution has code irrelevant to the next lesson, drop it.
Module starters merge relevant lings but only the parts that serve the module's
goal.
Quality of learning > mechanical chain consistency.
Validation uses ~15 shared check primitives.
Lab servers compose (L18 imports L17 handler + adds exec panel).

## Exercise Metadata

Each exercise has a Go struct in internal/exercise/ defining:
- ID, name, directory, type (kernel/go/both)
- VMUser: "root" or "student"
- PreloadModule: bool (insmod before giving shell)
- LabServer: which lab server to start (empty = none)
- IncludeFiles: extra files for build (rklings_hook.h for L05-L07)
- ValidateChecks: list of check primitives to run

## Validation Philosophy

`rklings validate` tests REAL behavior, not tautologies.
Every check verifies the actual mechanism:
- "File hidden from ls" = run ls, confirm file absent from output, then cat the file to prove it still exists
- "Module loaded" = check /proc/modules AND verify the module's proc entry works
- "Port hidden" = run ss, confirm port absent, then curl localhost:port to prove it's listening
- "Escalation works" = run id as student, mkdir rklings.e, run id again, confirm uid=0
- "mTLS connected" = verify server received valid client cert AND TLS 1.3 negotiated Never: "does the file exist", "did insmod return 0", "is the string in dmesg" as the sole check.

## State Tracking

Exercise progress stored in ~/.rklings/progress.json.
- `rklings validate` writes {exercise: timestamp} on pass
- `rklings solution` checks this file before revealing solution
- `rklings status` reads this file to show progress
- `rklings reset` removes the exercise entry

## Naming Conventions

- Kernel files by subsystem (Reptile): main.c, dispatch.c, cred.c, targets.c, dir.c, file.c, network.c, module.c, persist.c
- Kernel functions verb_noun (Diamorphine): escalate_cred(), module_hide(), filter_dir(), target_add(), is_hidden()
- Agent packages by capability (Sliver): transport.go, mkdir.go, dispatch.go, handlers/, evasion/
- Exercise dirs: l00-module-init/, lowercase, hyphenated, describes technique
- No invented terminology. Names from kernel source, Go docs, or MITRE ATT&CK only.

## README Format

```
# L15: mTLS Callback
[ASCII diagram]
## You write
[file, function, ~line count]
## API
[exact signatures + key calls]
## Test it
[exact commands + expected output]
## Validate
[rklings validate output]
## Next
[one sentence]
```

Optional "The trap" for hard exercises.
No hints system.

## Lab Servers

Only these exercises need a webapp: L15, L16, L17, L18, L19, L22.
All others (L00-L14, L20-L21, L23-L24) validated via SSH commands in the VM.
L22 is standalone (split-view decoy/C2).
L15-L19 are tabbed.

## Verified Kernel Hook Points (kernel 7.x, Ubuntu 26.04)

All verified online against kernel source.
No hallucinated symbols.

| Exercise | Symbol | Type | Register access |
|----------|--------|------|-----------------|
| L05-L09 | `__x64_sys_mkdir` | syscall wrapper | inner pt_regs via regs_get_kernel_argument(regs,0), pathname in uregs->di |
| L05-L09 | `__x64_sys_mkdirat` | syscall wrapper | inner pt_regs, pathname in uregs->si |
| L11 | `__x64_sys_getdents64` | syscall wrapper | inner pt_regs, dirent buf in uregs->si |
| L12 | `ksys_read` | regular function | direct: regs->di=fd, regs->si=buf, regs->dx=count |
| L13 | `tcp4_seq_show` | regular function | direct: regs->di=seq_file*, regs->si=sock* |
| L13 | `tcp6_seq_show` | regular function | direct: regs->di=seq_file*, regs->si=sock* |
| L14 | `call_usermodehelper` | exported API | direct call from module code, not a hook |
| M00 | `filp_open` + `kernel_write` | exported APIs | direct call from module code |

Syscall wrappers (`__x64_sys_*`): args inside inner pt_regs (double
indirection).
Regular functions (`ksys_read`, `tcp*_seq_show`): args directly in registers
(standard calling convention).
This is the #1 gotcha.
Existing legacy code handles both patterns correctly.

Total kretprobes in final rootkit: 7 (mkdir, mkdirat, getdents64, ksys_read,
tcp4_seq_show, tcp6_seq_show, plus mkdir pair reused).

## rklings_hook.h (L05-L07 black box)

Placed directly in each L05-L07 starter directory.
Student sees the file, README says "black box, just use the API."
API surface for students:
- `RK_ON_MKDIR(name) { ... }` - define handler, receives basename as const char*
- `rk_hook_start()` - call from module_init, returns 0 or -errno
- `rk_hook_stop()` - call from module_exit Internals: kretprobe on __x64_sys_mkdir + __x64_sys_mkdirat, strncpy_from_user, put_user('\0') trick. L08 opens this file. L09 discards it and writes kretprobe from scratch.

## P00 Production Deployment

Default: `cloudflared tunnel --url https://localhost:443` (one command, no
account, real TLS, real public URL).
Extra credit: DuckDNS + Let's Encrypt for stable domain + full ACME flow.
Host dep for P00: cloudflared binary (in addition to the 4 base deps).

## Attack Chain Context (updated 2026-10-16)

This course teaches steps 2-6 of the real rootkit kill chain:
1. Initial access -- PRACTICAL LAB: command injection in vulnerable service (D00 step 1)
2. Privilege escalation -- PRACTICAL LAB: CVE-2026-31431 "Copy Fail" (D00 step 2)
3. Kernel module loading (insmod, memfd+finit_module) -- L00, L20
4. Stealth (hiding, filtering, process masquerade) -- L10-L13, L21-L24
5. Persistence (modules-load.d, boot survival) -- M00
6. C2 callback (mTLS agent, command dispatch) -- L15-L19

Real-world references for P00 README:
- PUMAKIT: dropper -> memfd executables -> finit_module .ko (our L20-L21 chain)
- BPFDoor: Jenkins CVE initial access -> lateral movement -> deploy
- CVE-2024-1086 (nftables UAF): 93-99% success LPE, used by ransomware for rootkit install
- CVE-2026-31431 (AF_ALG + splice): page cache corruption, 732-byte PoC, our deployment lab
- AUR supply chain (June 2026): compromised packages -> eBPF rootkit on 1500+ systems

## VM User Model

- L00-L05, L08-L09: root (building the module, need insmod)
- L06-L07, L10-L13: student with module pre-loaded (test capabilities, escalate via rootkit)
- L14-L19: root (building agent, deploying)
- L20-L24, M03, P00: student, nothing pre-loaded (full attack flow)
- M00-M02: depends on module focus (root for build, student for test)

Cloud-init creates both users.
Serial console auto-login as exercise-appropriate user.

## OPSEC Rules

- NEVER print to dmesg in the production rootkit (exercises use dmesg for learning, P00 does not)
- NEVER reference real rootkit projects by name in the codebase
- Module signing (CONFIG_MODULE_SIG_FORCE) is NOT set (students load unsigned modules, but all other hardening is on)

## Style Rules

- No emojis. Use SVG icons (monochrome, currentColor).
- No em dashes. Use commas, colons, periods, parentheses.
- No Co-Authored-By: Claude in commits.
- Kernel code: clear, elegant, step-by-step. No vtable bloat.
- Less is more: simplest correct solution, delete over add.
- No fake APIs: use real kernel/mTLS APIs directly.
- Templates only for debug exercises, never generic shells.
