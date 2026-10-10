# Assemble and verify the complete rootkit

You have built and tested each subsystem separately.
This project joins them into one loadable module with one initialization
sequence, one rollback path, and one authenticated control channel:

```text
local control ----> authorization ----> credential action
                         |
syscall probes ------> telemetry ------> evidence
                         |
process filter ------> resident core <----> guarded boot request
                         |
TCP peer <--------> AEAD framed agent ----> root reverse shell
                         |
UDP datagram ------> packet switchboard --> replacement port
```

The result has local control, a bounded privilege action, syscall telemetry,
process-view filtering, a guarded boot request, authenticated remote control,
UDP redirection, and a root reverse shell.
Every startup step needs rollback, and unload must release everything.

The remote agent retains `PING`, `INFO`, `MARK label`, and `QUIT`.
It also exposes five exact, bounded commands:

- `STATUS` returns `STATUS enabled worker_runs`;
- `ENABLE` allows scheduled and requested work;
- `DISABLE` prevents new work;
- `RUN` queues one worker request or reports why it could not;
- `SHELL port` opens a root shell to `127.0.0.1:port` when actions are enabled.

The port is a decimal integer from 1 through 65535.
Only one shell may be pending or running at a time.
The reply is `SHELL OPENING`, `ERR busy`, `ERR disabled`, or `ERR shell`.
Commands and output then travel over the separate shell connection.

Credential replacement stays on the local authorized path.
That path authenticates the requesting task and replaces that task's
credentials; invoking it from the agent would target the kernel thread instead.

Edit:

- `Kbuild`
- `core/main.c`

Add the supplied packet switchboard to the authenticated reverse-shell module.
Start it before enabling commands and stop it before destroying resident state.
Preserve the inherited lifecycle: agent intake stops before the root shell, and
every failed startup unwinds acquired resources in reverse order.

Run:

```sh
./rootkitlings check C00
```

## Inherited control channel

Each resident-agent TCP frame carries one envelope:

```text
version:1 | kind:1 | session:8 | sequence:8 | ciphertext | tag:16
```

The inherited channel authenticates its visible header and encrypted payload.
Its 64-byte frame carries at most 30 plaintext bytes.
Exact sequence checks reject duplicate and out-of-order requests.

Supply `control_key` as exactly 64 hexadecimal digits when loading the module.
The module generates a fresh nonzero session by default and reports it through
the root-only agent status file.
The checker alone may use the hidden `session_id` override to compare the module
with fixed independent vectors.

## Packet switchboard

While actions are enabled, one outbound loopback UDP destination is rewritten
from `redirect_from_port` to `redirect_to_port`.
The defaults are 5400 and 5401.
Other traffic is accepted unchanged.

## Evidence

The checker:

- retains the independently checked authenticated channel from M08;
- exercises the loopback agent, root shell, reconnect, cancellation, boot
  rollback, packet redirection, remote core control, and inherited local
  features on Linux 7.2.9;
- verifies failed initialization produces no remote command reply;
- forces startup failures 1 through 14 and verifies rollback;
- rejects an unknown failure point at build time.

## References

- Linux v7.2 `include/crypto/aead.h`
  <https://github.com/torvalds/linux/blob/v7.2/include/crypto/aead.h>
- NIST SP 800-38D
  <https://csrc.nist.gov/pubs/sp/800/38/d/final>
- RFC 5116: AEAD interface and nonce requirements
  <https://www.rfc-editor.org/rfc/rfc5116.html>
