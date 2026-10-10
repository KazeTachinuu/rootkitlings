# Assemble and verify the complete rootkit

This project joins the course's culminating subsystems into one module.
The work is integration: one startup order, one rollback path, and one clean
unload.

```text
local token ------> root action ------> event record
                         |
selected views <---- resident core ----> reversible load request
                         |
AEAD control ------> root shell -------> TCP peer
                         |
UDP signal --------> packet redirect --> replacement port
```

The module provides:

- a controllable resident worker;
- one-use local privilege elevation and syscall telemetry;
- selected directory and read filtering with an independent evidence map;
- a reversible module-load request;
- an authenticated, encrypted control connection;
- a root reverse shell requested through that connection; and
- a controlled UDP destination rewrite.

## Remote operation

Set `peer_address`, `peer_port`, and a nonzero 32-byte `control_key` when the
module loads.
The agent reconnects to that IPv4 peer and creates a fresh random session for
every connection.
An authenticated `HELLO` establishes the session before commands are accepted.

The fixed commands are:

- `PING`, `INFO`, and `STATUS` for observation;
- `ENABLE`, `DISABLE`, and `RUN` for the resident core;
- `MARK label` for one bounded operator marker;
- `SHELL port` and `SHELL STATUS` for the root shell; and
- `QUIT` to close the control connection.

An accepted shell request returns:

```text
OPEN <8-hex request id> <16-hex cookie>
```

The shell connection begins with the matching `RKSH` prelude.
This binds the connection to the authenticated request.
The following shell stream is a raw, non-PTY `/bin/sh` stream; it is not
encrypted by the control channel.

Run the completed reference safely with:

```sh
./rootkitlings play C00
```

The disposable guest can reach only the two host-side listeners created for that
session.

## Visibility boundary

When actions are enabled, selected `getdents64()` views omit the module's proc
entries, resident PIDs, module file, load request, and sysfs module name.
A bounded `read()` view masks the module token in `/proc/modules`.

Direct paths, alternate directory and read APIs, socket tables, symbols, Kprobe
records, the shell process, and kernel logs remain evidence.
The root-only evidence endpoint states this scope and its runtime counters.

The load-request transaction is also deliberately bounded.
The RAM-only guest does not reboot, so the course proves safe staging and
rollback, not persistence across boots.

## Your task

Edit:

- `Kbuild`
- `core/main.c`

Compile the supplied packet components and connect them to the module lifecycle.
Start each component only after its dependencies exist.
On failure and unload, stop command intake and release every acquired resource.

Run:

```sh
./rootkitlings check C00
```

The checks exercise the integrated shell, control channel, privilege action,
telemetry, visibility boundary, load-request rollback, and UDP behavior.
They also force startup failures 1 through 15 and reject an unknown point.

## References

- Linux v7.2 user-mode helpers
  <https://github.com/torvalds/linux/blob/v7.2/kernel/umh.c>
- Linux v7.2 Kprobe interface
  <https://github.com/torvalds/linux/blob/v7.2/include/linux/kprobes.h>
- Linux v7.2 Netfilter interface
  <https://github.com/torvalds/linux/blob/v7.2/include/linux/netfilter.h>
- NIST SP 800-38D
  <https://csrc.nist.gov/pubs/sp/800/38/d/final>
