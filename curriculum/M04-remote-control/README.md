# Full protocol over the encrypted agent connection

This milestone wires together the command parser (L24), shell timeouts (L22),
file download (L23), the M03 secure channel, and M01 local commands into a
single agent loop.
Every operation the rootkit supports, from hiding files to escalating
privileges, now has a wire equivalent.
No command ever touches the network in plaintext.

## Architecture

```text
Operator                          Target (kernel module)
   |                                      |
   |  encrypted frame: "HIDE FILE x"      |
   | -----------------------------------> |
   |                                      |  decrypt
   |                                      |  rk_command_parse
   |                                      |  rk_command_dispatch
   |                                      |    -> rk_do_hide_file("x")
   |                                      |  rk_send_response("OK")
   |  encrypted frame: "OK"               |
   | <----------------------------------- |
```

The agent kthread loop:

1. Receive an encrypted frame from the socket.
2. Decrypt it using the AEAD context from M03.
3. Pass the plaintext to `rk_command_parse`.
4. Pass the parsed command to `rk_command_dispatch`.
5. The handler executes the operation and sends a response.
6. Loop.

## Command coverage

Every M01 local command now works remotely:

| Wire command         | Kernel operation              | Reply format                     |
|----------------------|-------------------------------|----------------------------------|
| PING                 | Liveness check                | OK                               |
| STATUS               | Report rootkit state          | STATUS hidden=yes persisted=no ...  |
| INFO                 | Report target identity        | INFO hostname kernel arch        |
| SHELL port           | Open interactive shell (L22)  | OPEN request_id cookie           |
| SHELL STATUS         | Check shell state             | STATE request_id state result    |
| EXEC_SHELL port      | Open exec shell               | OPEN request_id cookie           |
| DOWNLOAD path        | Exfiltrate file (L23)         | FILE size ... EOF                |
| HIDE FILE name       | Hook getdents64               | OK index                         |
| UNHIDE FILE idx      | Remove getdents64 filter      | OK removed_name                  |
| HIDE PID pid         | Filter /proc entries          | OK index                         |
| UNHIDE PID idx       | Restore /proc entry           | OK removed_name                  |
| HIDE CONTENT pat     | Filter file content           | OK index                         |
| UNHIDE CONTENT idx   | Restore content visibility    | OK removed_name                  |
| HIDE PORT port       | Filter /proc/net/tcp          | OK index                         |
| UNHIDE PORT idx      | Restore port visibility       | OK removed_name                  |
| HIDE MODULE          | Remove module from /proc      | OK                               |
| UNHIDE MODULE        | Restore module visibility     | OK                               |
| UNHIDE ALL           | Clear all hide lists          | OK                               |
| PERSIST              | Write persistence files       | OK                               |
| UNPERSIST            | Remove persistence            | OK                               |
| ESCALATE pid         | Grant root to PID             | OK or ERR 6                      |
| LIST category        | Count hidden entries          | LIST count                       |
| LIST category idx    | Show one entry by index       | ENTRY idx value                  |
| QUIT                 | Disconnect                    | (close)                          |

## Integration pattern

The solution does not reimplement any of the component lessons.
It calls into them:

- `rk_command_parse` from L24 converts bytes to structured commands.
- `rk_command_dispatch` from L24 routes to handlers.
- Shell dispatch uses L22's timeout-aware shell slots.
- File download calls L23's `rk_download_file`.
- Hide/unhide operations call into the M01 local command handlers.
- All I/O goes through the M03 encrypted framing.

## Error handling

Unknown commands get an `ERR unknown command` response.
Handler failures return an error frame with the kernel error code.
The agent loop continues after any error; only a socket failure or module unload
stops it.

## Your task

Edit `execution/commands.c`:

1. Implement every command handler listed in the table above.
   Each receives the session and an argument string.
   Response formats must match the operator's protocol (see protocol.toml).

Edit `execution/dispatch.c`:

2. Build the dispatch table mapping wire prefixes to handlers.
   Longer prefixes first so "UNHIDE FILE" matches before "HIDE FILE".
   Prefixes with a trailing space take an argument; exact-match otherwise.
3. Implement `rk_send_response` (send a sealed text frame) and `rk_dispatch`.

The remaining files (agent, protocol, envelope, init, target, filter, opsec,
shell) are provided complete from prior exercises.

Run:

```sh
./rklings check
```

## References

- [Kernel threads](https://docs.kernel.org/driver-api/basics.html)
- [Workqueue API](https://docs.kernel.org/core-api/workqueue.html)
- [Kernel crypto AEAD](https://docs.kernel.org/7.2/crypto/api-aead.html)
