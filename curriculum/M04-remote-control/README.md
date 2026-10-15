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

| Wire command         | Kernel operation              |
|----------------------|-------------------------------|
| PING                 | Respond with PONG             |
| STATUS               | Report rootkit state          |
| SHELL                | Open interactive shell (L22)  |
| EXEC <cmd>           | Run one-shot command (L22)    |
| DOWNLOAD <path>      | Exfiltrate file (L23)         |
| HIDE FILE <name>     | Hook getdents64               |
| UNHIDE FILE <idx>    | Remove getdents64 filter      |
| HIDE PID <pid>       | Filter /proc entries          |
| UNHIDE PID <pid>     | Restore /proc entry           |
| HIDE CONTENT <pat>   | Filter file content           |
| UNHIDE CONTENT <idx> | Restore content visibility    |
| HIDE PORT <port>     | Filter /proc/net/tcp          |
| UNHIDE PORT <port>   | Restore port visibility       |
| PERSIST              | Write persistence files       |
| UNPERSIST            | Remove persistence            |
| ESCALATE             | commit_creds(root)            |
| INFO                 | Report module info            |
| LIST FILE/PID/...    | List active hides             |

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

Edit `main.c`:

1. Set up the agent kthread with the secure channel context.
2. In the agent loop, receive and decrypt frames.
3. Parse each decrypted frame with `rk_command_parse`.
4. Dispatch with `rk_command_dispatch`, which calls:
   - Shell/exec handlers (L22 shell slots with timeouts)
   - Download handler (L23 chunked file read)
   - All hide/unhide/persist/escalate handlers (M01)
5. Send responses as encrypted frames.
6. Handle errors gracefully: send error response, continue loop.
7. Clean shutdown: cancel delayed works, stop kthread, release socket.

Run:

```sh
./rklings check
```

## References

- [Kernel threads](https://docs.kernel.org/driver-api/basics.html)
- [Workqueue API](https://docs.kernel.org/core-api/workqueue.html)
- [Kernel crypto AEAD](https://docs.kernel.org/7.2/crypto/api-aead.html)
