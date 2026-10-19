# L04: Control Live State through Procfs

L03 published a read-only status file.
This exercise lets a shell write `0` or `1` to the same path and change the
state reported by subsequent reads.

```console
root@victim:~# cat /proc/rklings
active=1
root@victim:~# printf '0\n' > /proc/rklings
root@victim:~# cat /proc/rklings
active=0
```

The implementation is short, but it crosses an important safety boundary:
`proc_write` receives an address owned by a userspace process, not ordinary
kernel memory.

## Workspace

The workspace already provides:

- the state object and its `active` getter and setter;
- the read callback that prints `active=0` or `active=1`;
- the `proc_ops` table;
- publication of `/proc/rklings` with mode `0644`; and
- the complete state and procfs lifecycle from L03.

Only `rk_proc_write` in `interface/proc.c` contains a TODO.
Read the write path and buffer rules below before implementing it.

## Follow one write

The shell redirection opens `/proc/rklings` and asks the kernel to write bytes
to it:

```text
printf '0\n' > /proc/rklings
        |
        | userspace buffer contains:  '0' '\n'
        v
rk_proc_write(file, ubuf, count=2, position)
        |
        +-- copy at most 3 bytes into a 4-byte kernel buffer
        +-- append '\0'
        +-- first byte is '0'
        +-- rk_state_set_active(proc_state, false)
        |
        v
return count=2
        |
        v
printf sees a successful complete write
```

The following `cat` takes the separate read path:

```text
cat /proc/rklings
        |
        v
rk_status_show()
        |
        +-- rk_state_is_active(proc_state) == false
        v
active=0
```

The write callback changes state; it does not construct the read response.

## Respect the userspace boundary

The callback parameter is declared `const char __user *ubuf`. `__user` documents
that the address belongs to the calling process:

```text
userspace                              kernel space
---------                              ------------
ubuf ----- cannot dereference safely ------X
     +---- copy_from_user(...) -----------> local buf
```

Userspace may supply an invalid or unmapped address.
Reading `ubuf[0]` directly can fault inside the kernel.
Use `copy_from_user`, check its result, and return `-EFAULT` when the copy is
incomplete.

```c
if (copy_from_user(buf, ubuf, len))
	return -EFAULT;
```

Unlike most C success conventions, `copy_from_user` returns the number of bytes
it could not copy.
Zero therefore means the complete copy succeeded.

## Bound and terminate the input

The local buffer has four bytes:

```c
char buf[4];
```

Never copy `count` bytes without checking its size.
The caller controls `count`, so it may be much larger than the local array.

```c
len = min(count, sizeof(buf) - 1);
```

Reserving one byte lets you add a C string terminator safely:

```text
input:  '1' '\n'                     local buf[4]
                                        +-----+-----+------+-----+
copy two bytes -----------------------> | '1' | '\n'| '\0' |  ?  |
                                        +-----+-----+------+-----+
                                                       ^
                                            appended by the kernel code
```

This exercise needs only the first byte, but explicit termination keeps the
buffer safe for normal string operations and makes its contract clear.

## Return a precise result

The callback accepts two commands:

| First byte    | Action                | Return value     |
| ------------- | --------------------- | ---------------- |
| `0`           | Set inactive          | Original `count` |
| `1`           | Set active            | Original `count` |
| Anything else | Leave state unchanged | `-EINVAL`        |
| Copy fault    | Leave state unchanged | `-EFAULT`        |

Returning the original `count` tells userspace that the complete submitted write
was handled.
Returning only the bounded `len` would incorrectly report a partial write.

Reject invalid input before changing state.
After `printf 'x\n' > /proc/rklings` fails, the previous active value must
remain intact.

## Implement it

In `rk_proc_write`:

1. Declare a four-byte kernel buffer and a bounded length.
2. Copy at most `sizeof(buf) - 1` bytes from `ubuf`.
3. Return `-EFAULT` if any requested byte in that bounded copy fails.
4. Append a null terminator.
5. Set state to false for `0` and true for `1`.
6. Return `-EINVAL` for any other first byte without changing state.
7. Return the original `count` after a valid command.

The surrounding lifecycle code is inherited and must remain intact.

## Test it

After completing the implementation, build it and enter the VM:

```console
$ ./rklings run L04
```

Inside the VM:

```console
root@victim:~# insmod /rklings/rklings.ko
root@victim:~# cat /proc/rklings
active=1
root@victim:~# printf '0\n' > /proc/rklings
root@victim:~# cat /proc/rklings
active=0
root@victim:~# printf '1\n' > /proc/rklings
root@victim:~# cat /proc/rklings
active=1
root@victim:~# printf 'x\n' > /proc/rklings
-bash: printf: write error: Invalid argument
root@victim:~# cat /proc/rklings
active=1
```

Unload the module and confirm the interface disappears:

```console
root@victim:~# rmmod rklings
root@victim:~# test ! -e /proc/rklings && echo absent
absent
```

## Validate

From the repository root:

```console
$ ./rklings validate L04
```

The validator checks the `0644` interface, both state transitions, rejection of
invalid input without a state change, bounded reads while the entry is open, all
inherited failure paths, and clean unload.

## Next

The module now accepts commands, but `/proc/rklings` permanently advertises the
control interface.
L05 carries a command in an ordinary `mkdir` call and prevents the requested
directory from appearing.
