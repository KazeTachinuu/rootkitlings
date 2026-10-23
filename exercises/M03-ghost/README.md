# M03: Integrate the Deployment Path

## Mission

L20 through L24 implemented individual deployment controls. M03 combines them
into one agent and makes their dependencies explicit.

The challenge is ordering:

- The module must be active before it can hide anything
- Re-execution replaces the current program and never returns
- Cleanup must not break the command channel used to verify the result

## What you will observe

The completed agent starts from a staged executable, loads the embedded module,
re-executes from memory, and remains controllable while the selected discovery
paths omit it:

```text
operator                     victim observations

PING -> PONG                 module absent from lsmod
SHELL id -> uid=0            PID absent from ps
STATUS -> agent info         launcher absent from the filesystem
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

The build embeds the completed M01 module and the agent's X25519 keys into
the agent binary.

- Implementations from L20-L24 are supplied in `main.go`; read their
  contracts, but do not rewrite them
- Your work is the `deploy` orchestration function
- No standalone module is staged in the guest

## Follow the two process images

`unix.Exec` does not start a child process.
It replaces the program running in the current PID.
Successful execution therefore divides deployment into two passes through
`main`:

```text
FIRST PROCESS IMAGE                    MEMFD PROCESS IMAGE

+-------------------------+           +-------------------------+
| staged agent            │           | same PID, new program   |
| /rklings/agent          │           | /proc/PID/exe -> memfd  |
+------------+------------+           +------------+------------+
             │                                     |
             | load embedded module                | skip module load
             | hide module                         | set process controls
             | copy self to memfd                  | hide current PID
             +--------------- exec --------------> | scrub selected records
                                                   +------------+------------+
                                                                |
                                                                | begin WS
                                                                | session loop
                                                                v
```

The second image starts at `main` again.
Check `/proc/self/exe` for `memfd` before loading or re-executing; without that
guard, the agent loops forever.

## TODOs

```text
TODO 1  deploy  reject deployment without root
TODO 2  deploy  first image: load module, hide it, re-exec from memory
TODO 3  deploy  second image: masquerade as a kernel thread
TODO 4  deploy  hide the current PID and scrub local traces
```

Compose the deployment in dependency order.

Each helper below was already implemented and validated in its focused lesson:

- Call them in the required order
- Guard the two process images
- Wrap errors at the phase boundary that produced them

The milestone assesses composition, repeat safety, and preservation of
control.

### TODO 1 -- Reject deployment without root

`finit_module` requires privilege, and the module that provides `rklings.e`
is not loaded yet. M03 therefore runs the agent as root.

Check the effective UID and return an error if that precondition is not met.

### TODO 2 -- First image: load, hide, re-exec

Guard this block with a `/proc/self/exe` check: if the link contains `memfd`,
skip everything below and fall through to the second-image steps.

On the disk-backed first image:

1. Call `loadModuleFromMemory(koBytes)` to create a memfd with `MFD_CLOEXEC`,
   write all embedded module bytes, and call `FinitModule`.
2. Send `trigger("h.m")` to hide the module from `lsmod`.
3. Call `reexecFromMemory()` to copy the binary into a second memfd (without
   `MFD_CLOEXEC`) and `unix.Exec` into it.

`unix.Exec` replaces the process image and does not return on success.
Wrap each call so failures identify the broken boundary.

### TODO 3 -- Masquerade as a kernel thread

After re-exec, the memfd guard skips TODO 2.

Call `masquerade()`, which:

- Sets `comm` to `[kworker/0:1]`
- Clears `PR_SET_DUMPABLE`
- Zeros both `RLIMIT_CORE` values

Apply masquerade before registering process concealment so that the PID is
already disguised when its entry disappears from `ps`.

### TODO 4 -- Hide PID and scrub traces

1. Call `hideAll()` to send `rklings.h.i.<pid>`
2. Call `scrubAll()` to compose the L24 record-cleaning operations into one
   pass
3. Wrap errors so each phase is identifiable

### Supplied steps

**Command serving** is already supplied after deployment. `DEPLOY` must be
repeat-safe: the memfd guard prevents another module load or re-exec, while
already-applied process and cleanup operations remain safe to repeat.

## Test it

Two terminals.

### 1. Boot the target

```console
$ ./rklings run M03
```

The VM boots and stages the agent binary.
The agent starts in the background, retrying the WebSocket connection until an operator
exists.

### 2. Start the operator

In a second host terminal:

```console
$ ./rklings serve M03
```

After the agent connects, send the deployment command:

```text
rklings> ping
PONG
rklings> deploy
OK
rklings> ping
PONG
```

The second `PING` proves that deployment preserved the session.

### 3. Observe from the guest

SSH into the running guest and verify each concealment layer:

```console
$ ./rklings ssh M03
```

```console
root@victim:~# lsmod | grep rklings
root@victim:~# ps aux | grep agent
root@victim:~# ls /rklings/
ls: cannot access '/rklings/': No such file or directory
root@victim:~# cat /proc/$(pgrep -f kworker/0:1 | tail -1)/comm
[kworker/0:1]
```

Every query returns empty.
The module runs, the agent responds, but no standard observation tool reveals
them.

## Validate


```text
[+] module absent from lsmod
[+] selected PID absent from ps
[+] connection port absent from procfs and ss
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
