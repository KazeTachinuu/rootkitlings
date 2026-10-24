# P00: The Complete Rootkit

## Mission

This is it.

Thirty exercises built a Linux rootkit from an empty kernel module to a remotely
operated implant.
P00 is the final assessment: operate the finished system end to end, through the
real interfaces, and prove every mechanism holds.

There is no code to write.

## What you will observe

The complete product, running:

```text
OPERATOR HOST                              TARGET VM

+----------------------+                  +----------------------+
| terminal + web UI    |    WebSocket     | rklings_agent        |
| http://127.0.0.1:8080| <--------------> | AEAD + persist       |
+----------------------+                  +----------+-----------+
                                                     |
                                                     | mkdir controls
                                                     v
                                          +----------------------+
                                          | rklings.ko           |
                                          | filtering + control  |
                                          +----------------------+
```

## Transport

The agent embeds two 32-byte X25519 keys: `keys/agent.key` (private) and
`keys/operator.pub` (public).
Both sides derive a shared AES-256-GCM key via ECDH.
The encryption key and agent ID are derived independently via HKDF with separate
labels (`rklings-aead` and `rklings-id`), so the ID leaks no key material.

The agent connects to the operator over WebSocket, authenticating with its agent
ID and an encrypted hello frame:

- Commands and responses flow bidirectionally over the persistent connection
  with per-frame AEAD encryption
- Unrecognized HTTP requests receive a decoy HTML page
- No runtime handshake beyond the initial hello, no external crypto dependencies

## What you built

```text
KERNEL MODULE (C)                    AGENT (Go)
  module lifecycle             L00    AEAD transport            L15
  error rollback               L01    mkdir bridge             L16
  private state                L02    command dispatch         L17
  procfs status                L03    remote shell + PTY       L18
  procfs control               L04    file transfer            L19
  mkdir command interface      L05    fileless module load     L20
  privilege escalation      L06-L07   fileless execution       L21
  kretprobe interception    L08-L09   decoy routing            L22
  target lists                 L10    process masquerade       L23
  dirent filter                L11    anti-forensics           L24
  content filter               L12
  port filter                  L13    OPERATOR (Go)
  usermode helper              L14    terminal console
  persistence + hiding         M00    neobrutalist web dashboard
  integrated local control     M01    multi-target management
```

The web dashboard at `http://127.0.0.1:8080` provides:

- Target overview
- Browser-embedded PTY shell
- File browsing with upload and download
- Live process and port snapshots
- Command history and connection events

Untrusted visitors to the agent listener receive a decoy page; only agents with
a valid shared key reach the operator protocol.

## Workspace

```text
exercises/P00-complete-rootkit
├── agent
│   ├── crypto.go
│   ├── dispatch.go
│   ├── evasion.go
│   ├── go.mod
│   ├── go.sum
│   ├── handlers.go
│   ├── hidden.go
│   ├── main.go
│   ├── operator.go
│   ├── persistence.go
│   ├── singleton.go
│   └── transport.go
└── kernel
    ├── core
    │   ├── dispatch.c
    │   ├── escalation.c
    │   ├── filter.c
    │   ├── init.c
    │   ├── module.c
    │   └── target.c
    ├── include
    │   └── rootkit.h
    └── Kbuild
```

Every file is a supplied, read-only product artifact.
The operator lives in `product/operator`.

## Test it

Two terminals.

### 1. Boot the target and start the agent

```console
$ ./rklings run P00
root@victim:~# /rklings/rklings_agent &
```

### 2. Start the operator

```console
$ ./rklings serve P00
```

The agent connects over WebSocket with AEAD encryption.
The terminal prompt appears; the dashboard is live at `http://127.0.0.1:8080`.

## Operate it

```text
victim-17def67e > info
system   victim / 7.0.0-38-generic / amd64
agent    pid=915 uid=0
stealth  module=on persistence=on

victim-17def67e > exec id
uid=0(root) gid=0(root) groups=0(root)

victim-17def67e > hide file secret.txt
ok

victim-17def67e > download /etc/hostname
saved 7 bytes -> .rklings/downloads/victim-17def67e/etc/hostname
```

`shell` opens a persistent PTY; `Ctrl+]` returns to the operator.
With multiple victims, `agents` lists them and `use <id>` selects one.

## Prove persistence

Reboot the guest while the operator stays running:

```console
root@victim:~# reboot
```

The target reconnects automatically.

Remove persistence and reboot again:

```text
victim-17def67e > unpersist
ok
```

The target must remain absent.

## Validate

```console
$ ./rklings validate P00
  [+] agent connects and responds to commands
  [+] kernel controls work through the agent
  [+] concealment hides files, PIDs, content, and ports
  [+] persistence survives reboot and agent reconnects
  [+] removal clears persistence and module is absent after reboot
  PASS
```

## Next

P00 is the end of the required route.
