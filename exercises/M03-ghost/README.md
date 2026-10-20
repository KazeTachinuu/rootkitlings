# M03: Integrate the Deployment Path

## Mission

L20 through L24 implemented individual deployment controls.
M03 combines them into one agent and makes their dependencies explicit.

The challenge is ordering.
The module must be active before it can hide anything.
Re-execution replaces the current program and never returns.
The callback must exist before its real local port is known.
Cleanup must not break the command channel used to verify the result.

## What you will observe

The completed agent starts from a staged executable, loads the embedded module,
re-executes from memory, connects over mTLS, and remains controllable while the
selected discovery paths omit it:

```text
operator                     victim observations

PING -> PONG                 module absent from lsmod
SHELL id -> uid=0            PID absent from ps
STATUS -> real local port    port absent from procfs and ss
                             launcher absent from the filesystem
                             /proc/PID/exe reports a deleted memfd
```

These are narrow controls over named observation paths.
They do not prevent kernel memory inspection, remote logging, traffic analysis,
or independent integrity monitoring.

## Workspace

```text
exercises/M03-ghost
├── go.mod
├── go.sum
└── main.go
```

The build embeds the completed M01 module and the course certificates into the
agent.
Implementations from L20–L24 are supplied in `main.go`; read their contracts,
but do not rewrite them.
Your work is the `deploy` orchestration function.
No standalone module is staged in the guest.

## Follow the two process images

`unix.Exec` does not start a child process.
It replaces the program running in the current PID.
Successful execution therefore divides deployment into two passes through
`main`:

```text
FIRST PROCESS IMAGE                    MEMFD PROCESS IMAGE

+-------------------------+           +-------------------------+
| staged agent            |           | same PID, new program   |
| /rklings/agent          |           | /proc/PID/exe -> memfd  |
+------------+------------+           +------------+------------+
             |                                     |
             | load embedded module                | skip module load
             | hide module                         | set process controls
             | copy self to memfd                  | hide current PID
             +--------------- exec --------------> | scrub selected records
                                                   +------------+------------+
                                                                |
                                                                | mTLS dial
                                                                v
                                                   hide actual local port
```

The second image starts at `main` again.
Check `/proc/self/exe` for `memfd` before loading or re-executing; without that
guard, the agent loops forever.

## Implement it

Compose the deployment in dependency order.

Each helper below was already implemented and validated in its focused lesson.
Call them in the required order, guard the two process images, and wrap errors
at the phase boundary that produced them.
The milestone assesses composition, repeat safety, and preservation of control.

### 1. Require the deployment privilege

`finit_module` requires privilege, and the module that provides `rklings.e` is
not loaded yet.
M03 therefore runs the agent as root.
Check the effective UID and stop with a clear error if that precondition is not
met; escalation cannot come from a control plane that does not exist yet.

### 2. Load the embedded module from memory

Create a memfd with `MFD_CLOEXEC`, write all embedded module bytes, call
`FinitModule`, and close the descriptor on every path:

```text
embedded rklings.ko -> memfd -> finit_module -> running module
```

`unix.Write` may write fewer bytes than requested.
Continue until every byte is written; otherwise `finit_module` receives a
truncated ELF object.

The descriptor is only a loading source, so closing it during a later `exec` is
correct.

### 3. Hide the module, then re-execute the agent

The module can accept `rklings.h.m` only after it has loaded.
Send that command, copy `/proc/self/exe` into a second memfd, and execute
`/proc/self/fd/<fd>`.

Do not use `MFD_CLOEXEC` for the executable memfd.
The kernel still needs that descriptor while replacing the process image.

### 4. Apply process controls in the new image

After the memfd guard skips steps 2 and 3:

1. set `comm` to `[kworker/0:1]`;
2. set `PR_SET_DUMPABLE` to zero;
3. set both `RLIMIT_CORE` values to zero; and
4. hide the current PID through `rklings.h.i.<pid>`.

Changing `comm` affects one displayed name.
The process remains a userspace process with mappings, descriptors, credentials,
and network activity.

### 5. Connect before hiding the port

The kernel chooses the callback's local ephemeral port during `Dial`.
Read it from the established `net.TCPConn` and only then send
`rklings.h.p.<local-port>`:

```text
dial operator
     |
     +-- connection.LocalAddr() -> 10.0.2.15:52734
     |
     +-- mkdir rklings.h.p.52734
```

Do not hide port 443 or 8443. Those are remote listener ports, not the local
socket value shown in the victim's network tables.

### 6. Clean the selected local records

Compose the functions from L24:

- clear matching `utmp` and `wtmp` records;
- truncate the selected shell histories;
- copy reference atime and mtime values;
- rotate and vacuum the local journal; and
- clear inherited environment variables.

Keep each operation bounded to its declared record.
For example, changing atime and mtime cannot restore ctime, and local journal
cleanup cannot alter a remote log collector.

### 7. Preserve the command channel

After deployment, continue serving `PING`, `STATUS`, `SHELL`, and `DEPLOY`.
`DEPLOY` must be repeat-safe: the memfd guard prevents another module load or
re-exec, while already-applied process and cleanup operations remain safe to
repeat.

`STATUS` reports the actual PID, UID, dumpable state, core limit, and callback
port.
This supports debugging; validation still checks the victim directly rather than
trusting that response.

## Test it

Run the complete transition against a disposable target:

```console
$ ./rklings validate M03
```

The validator uses a real mTLS harness and proves that deployment does not break
control:

```text
> STATUS
PID=1234 UID=0 DUMPABLE=0 CORE=0 PORT=52734
> PING
PONG
> SHELL id
uid=0(root) gid=0(root) groups=0(root)
.
> DEPLOY
OK
> PING
PONG
```

The second `PING` proves that repeating deployment preserved the session.

## Validate

The validator observes the victim rather than trusting agent status fields:

```text
[+] module absent from lsmod
[+] selected PID absent from ps
[+] callback port absent from procfs and ss
[+] launcher absent from the filesystem
[+] selected login record absent
[+] agent is not dumpable
[+] selected timestamps match their reference
[+] agent still responds to commands
PASS
```

## Next

M03 proves one integrated deployment against a real command channel.
P00 keeps these mechanisms and adds the complete multi-target operator,
reconnect and singleton handling, durable boot lifecycle, browser workflow, and
final end-to-end verification.
