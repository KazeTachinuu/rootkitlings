# Open a root reverse shell

Protect the resident control channel, then let one authenticated command open
the root shell built in L21.

```text
operator -- authenticated control --> resident agent
operator <------ plain TCP shell ---- root /bin/sh
```

`peer_address=` selects the IPv4 peer for both connections and defaults to
`127.0.0.1`.
The shell stream is a separate lab transport and is not encrypted.

## Provision the key

Load the module with `control_key=` followed by exactly 64 hexadecimal digits.
The supplied provisioning component rejects missing, malformed, or all-zero
keys, decodes the value into 32 private bytes, and clears the text buffer.
The parameter has mode `0000`, so sysfs never exposes it.

The resident agent retains one key copy and clears it after the worker stops.

## Start every connection cleanly

Generate a fresh nonzero session after each TCP connection.
Start a new secure channel, then send `HELLO` as the first authenticated reply.
Its authenticated header tells the peer which session to use.
A reconnect gets a different session and fresh request and reply sequences.

Authenticate the complete envelope before dispatch.
Reject replay, gaps, the wrong direction or session, and a bad tag by closing
that connection.

## Open and observe the shell

Add these exact commands:

```text
SHELL port
SHELL STATUS
```

The port is 1 through 65535 with no sign, whitespace, or leading zero.
Require the resident action gate for `SHELL port`.

An accepted request returns an encrypted reply:

```text
OPEN <8-hex request id> <16-hex cookie>
```

The shell connection starts with the matching plaintext prelude:

```text
RKSH <8-hex request id> <16-hex cookie>\n
```

The operator must verify it before using the raw stream.
The one-use cookie prevents another guest process from winning the listener
race.
It authenticates ownership of that connection; it does not encrypt later shell
bytes.

`SHELL STATUS` returns the latest request ID, state, and result.
Refusal, timeout, normal exit, and a live shell are therefore visible instead of
being collapsed into the initial queue result.

The imported shell owns its socket, helper, timeout, and stop path.
Stop the control agent before the shell so no new request can race teardown.

## Your task

Edit:

- `Kbuild`
- `core/main.c`
- `include/agent.h`
- `include/protocol.h`
- `interface/agent_status.c`
- `network/agent.c`
- `network/protocol.c`
- `security/control.c`

Wire the L21 shell and L25 AEAD into M07.
Keep the inherited resident actions, retry policy, framing, load-request
transaction, and status counters intact.

Run:

```sh
./rootkitlings check
```

The checker authenticates each fresh `HELLO`, rejects replay and tampering,
verifies the shell cookie, opens sequential UID-0 shells, observes refusal and
timeout, checks busy behavior, and unloads blocked control and shell sockets.

## References

- Kernel random interface
  <https://github.com/torvalds/linux/blob/v7.2/include/linux/random.h>
- Linux v7.2 AEAD interface
  <https://docs.kernel.org/7.2/crypto/api-aead.html>
- Explicit memory clearing
  <https://github.com/torvalds/linux/blob/v7.2/include/linux/string.h>
