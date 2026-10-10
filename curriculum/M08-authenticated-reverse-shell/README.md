# Open a root reverse shell

The resident agent already connects to a loopback control point.
Protect that control channel, then let one authenticated command open the root
shell built in L21.

```text
operator -- authenticated SHELL port --> resident agent
operator <------- plain shell stream ---- root /bin/sh
```

Only the trigger and control replies use authenticated encryption.
The shell stream is a separate loopback lab connection and is not encrypted.

## Provision control state

Load the module with `control_key=` followed by exactly 64 hexadecimal digits.
Reject a missing, malformed, or all-zero key.
The parameter is permission 0, so sysfs never exposes it.
Decode it into a private 32-byte buffer, clear the text immediately, and clear
every remaining copy after channel setup.

A fresh nonzero session comes from `get_random_u64()` and appears as `session=`
in `/proc/rootkitlings_agent`.
The peer needs that public value to construct its nonce and authenticated
header.
`session_id=` is a permission-0 test hook; zero keeps the random default.

## Authenticate before dispatch

Each framed request contains a version, direction, session, sequence,
ciphertext, and tag.
Authenticate the complete header before publishing any plaintext.
Accept only the next request sequence.
Keep independent request and reply counters and reject replay, gaps, wrong
direction, wrong session, or a bad tag by closing that control connection.

Add one exact command:

```text
SHELL port
```

The decimal port is 1 through 65535 with no sign, whitespace, or leading zero.
Require the resident action gate to be enabled.
Then call `rk_root_shell_request()` and return `SHELL OPENING`, `ERR busy`, or
`ERR shell` through the authenticated channel.

The imported shell owns its socket, helper, timeout, and stop path.
The agent only requests a session.
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

The checker uses independent ciphertext vectors, rejects replay and tampering,
opens a real UID-0 shell, verifies retry and busy behavior, and unloads while
both control and shell sockets are blocked.

## References

- Kernel random interface
  <https://github.com/torvalds/linux/blob/v7.2/include/linux/random.h>
- Linux v7.2 AEAD interface
  <https://docs.kernel.org/7.2/crypto/api-aead.html>
- Explicit memory clearing
  <https://github.com/torvalds/linux/blob/v7.2/include/linux/string.h>
