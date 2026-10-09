# Assemble and verify the complete rootkit

The main course branches now work alone.
This project joins their tested components into one loadable module with one
startup, one rollback path, and one authenticated control channel:

```text
local control ----> authorization ----> credential action
                         |
syscall probes ------> telemetry ------> evidence
                         |
process filter ------> resident core <----> guarded boot request
                         |
TCP peer <--------> AEAD framed agent ----> authenticated ready signal
                         |
UDP signal --------> packet switchboard --> replacement port
```

The result has local control, a bounded privilege action, syscall telemetry,
process-view filtering, a guarded boot request, authenticated remote control,
UDP redirection, and an authenticated status signal.
Every startup step needs rollback, and unload must release everything.

The remote agent retains `PING`, `INFO`, `MARK label`, and `QUIT`.
It also exposes four exact, bounded resident-core commands:

- `STATUS` returns `STATUS enabled worker_runs`;
- `ENABLE` allows scheduled and requested work;
- `DISABLE` prevents new work;
- `RUN` queues one worker request or reports why it could not.

It does not run shell text or arbitrary commands.
The supplied keys are test fixtures, not a key-provisioning design.
Credential replacement stays on the local authorized path.
That path authenticates the requesting task and replaces that task's
credentials; invoking it from the agent would target the kernel thread instead.

Edit:

- `Kbuild`
- `core/main.c`
- `include/agent.h`
- `include/framing.h`
- `include/protocol.h`
- `network/agent.c`
- `network/protocol.c`
- `security/control.c`

Implement the four `security/control.c` channel operations and store their state
in the agent.
Authenticate requests before parsing and replies before framing.
Send ready only after all resources exist, and reverse every acquisition on
failure or unload.
Keep remote command processing blocked until initialization is complete.
Import, but do not reimplement, the tested packet, AES-GCM, and signal
components.
Start the supplied packet switchboard before announcing ready and stop it before
destroying resident state.

Run:

```sh
./rootkitlings check C00
```

## Control envelope

Each resident-agent TCP frame carries one envelope:

```text
version:1 | kind:1 | session:8 | sequence:8 | ciphertext | tag:16
```

The visible first 18 bytes are AES-GCM associated data.
`kind` is 1 for requests and 2 for replies; integers are big-endian.
At most 30 plaintext bytes keep the envelope within the 64-byte frame limit.

Associated data binds the visible header.
Nonce reuse breaks GCM, so session separates module loads, direction separates
requests and replies, and sequence separates messages.
Exact sequence checks reject replay.

Each direction starts at sequence 1 and accepts only exact order.
Duplicate or gap changes no state.
The largest permitted sequence is `0x7fffffff`.

Derive the 12-byte nonce as:

```text
session:8 | ((sequence << 1) | direction):4
```

`direction` is 0 for requests and 1 for replies.
`session_id` must be nonzero and new for every module load under one key; the
checker supplies it to the independent peer.

Open and seal in private storage.
Publish plaintext and advance request state only after tag, header, session, and
sequence pass.
Publish an envelope and advance reply state only after sealing succeeds.
Clear private plaintext, envelope, nonce, and channel-key storage after use.

## Separate status signal

After all agent resources and endpoints are ready, send one authenticated
DNS-shaped datagram to `127.0.0.1:5300`.
Use version 1, control session as epoch, sequence 1, and ready status 2.

It is not ordinary DNS: name, timing, length, session, sequence, and status are
visible.
The checker independently verifies the capture and HMAC.

## Packet switchboard

While actions are enabled, one outbound loopback UDP destination is rewritten
from `redirect_from_port` to `redirect_to_port`.
The defaults are 5400 and 5401.
Other traffic is accepted unchanged.

## Evidence

The checker:

- uses OpenSSL as an independent AES-GCM oracle;
- verifies tamper, key, length, replay, exhaustion, failure, and cleanup rules;
- exercises the loopback agent, reconnect, cancellation, boot rollback, ready
  signal, packet redirection, remote core control, and inherited local features
  on Linux 7.2.9;
- verifies failed initialization produces no remote command reply;
- forces startup failures 1 through 15 and verifies rollback;
- rejects an unknown failure point at build time.

## References

- Linux v7.2 `include/crypto/aead.h`
  <https://github.com/torvalds/linux/blob/v7.2/include/crypto/aead.h>
- NIST SP 800-38D
  <https://csrc.nist.gov/pubs/sp/800/38/d/final>
- RFC 5116: AEAD interface and nonce requirements
  <https://www.rfc-editor.org/rfc/rfc5116.html>
- RFC 1035: DNS message format
  <https://www.rfc-editor.org/rfc/rfc1035.html>
