# L04: Control Module State through `/proc`

## Mission

A visible control channel is not what a deployed rootkit uses, but it proves the
module can accept commands.
L05 replaces it with mkdir, which leaves no trace.

L03 lets the shell read module state, but not change it.

Make `/proc/rklings` accept two commands: `0` makes the module inactive and `1`
makes it active.
Invalid commands must fail without changing its state.

## What you will observe

`printf '0\n' > /proc/rklings` sends the two bytes `0` and newline to the proc
entry.
That write invokes the module's `rk_proc_write` callback.

```console
root@victim:~# cat /proc/rklings
active=1
root@victim:~# printf '0\n' > /proc/rklings
root@victim:~# cat /proc/rklings
active=0
root@victim:~# printf '1\n' > /proc/rklings
root@victim:~# cat /proc/rklings
active=1
```

An invalid command fails and leaves the previous value unchanged.

## Workspace

```text
exercises/L04-proc-control
├── core
│   ├── main.c
│   └── state.c
├── include
│   └── rootkit.h
├── interface
│   └── proc.c
└── Kbuild
```

Only `interface/proc.c` is editable.
You implement the function that handles writes to `/proc/rklings`.

The module lifecycle, stored state, and proc read path are supplied.

## Cross the userspace boundary

Shell redirection opens the proc entry and writes the bytes produced by
`printf`:

```text
printf '0\n' > /proc/rklings
        |
        ├── ubuf points to "0\n"
        └── count is 2
        v
rk_proc_write(file, ubuf, count, position)
```

The callback receives `ubuf`, an address owned by the shell's userspace process.
The `__user` annotation marks that boundary; kernel code must not dereference
the address directly.

```text
userspace                           kernel
---------                           ------
ubuf -------- unsafe to read ----------X
     +------- copy_from_user --------> command byte
```

`copy_from_user(destination, source, length)` returns the number of bytes it
could not copy.
Return `-EFAULT` unless that result is zero.

## Read exactly one command byte

Only the first byte selects the command, so copy exactly one byte into one
`char`.
Reject `count == 0` before copying because an empty write has no command.
Even when userspace submits a longer string, no caller-controlled length reaches
kernel memory.

## TODOs

```text
TODO 1  interface/proc.c:28:2  copy one command byte and update active state
```

### TODO 1 -- `interface/proc.c:28:2` -- Handle one control write

Use one `char command`.
Copy one byte with `copy_from_user(&command, ubuf, 1)` only after confirming
that `count` is not zero.

```text
count is zero -> -EINVAL
copy fails   -> -EFAULT
command '0'  -> set active to false, return count
command '1'  -> set active to true, return count
other byte   -> -EINVAL, leave state unchanged
```

## Test it

From the repository root, build the module and enter the VM:

```console
$ ./rklings run L04
```

Inside the VM:

```console
root@victim:~# insmod /rklings/rklings.ko
root@victim:~# printf '0\n' > /proc/rklings
root@victim:~# cat /proc/rklings
active=0
root@victim:~# printf 'x\n' > /proc/rklings
-bash: printf: write error: Invalid argument
root@victim:~# cat /proc/rklings
active=0
root@victim:~# rmmod rklings
```

## Validate

```console
$ ./rklings validate L04
  [+] Bounded procfs control
  [+] State allocation rejection
  [+] Publication rollback
  PASS
```

## Next

The module now accepts commands through a visible proc entry.
L05 recognizes a command carried by an ordinary `mkdir` call and suppresses the
requested directory instead.
