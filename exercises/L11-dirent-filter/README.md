# L11: Hide Files and Processes from Listings

## Mission

This exercise makes a file and a running process disappear from ordinary
listings.

Neither target is deleted or stopped.
You will change only the directory entries returned to programs such as `ls` and
`ps`.

## What you will observe

After implementing the filter, create one file and one long-running process:

```console
$ printf 'still here\n' > /tmp/secret
$ sleep 3600 &
[1] 1234
$ ls /tmp | grep secret
secret
$ ps -p 1234
    PID TTY          TIME CMD
   1234 pts/0    00:00:00 sleep
```

Your finished module will support these commands:

```console
$ mkdir rklings.h.f.secret
$ mkdir rklings.h.i.1234
```

Afterward, `ls` and `ps` must omit the targets while direct access still works:

```console
$ ls /tmp | grep secret
$ cat /tmp/secret
still here
$ kill -0 1234
```

An empty `grep` result means the name is hidden.
Successful `cat` and `kill -0` commands prove the targets still exist.

## Workspace

The workspace already intercepts `mkdir` and recognizes four commands:

| Command              | Effect                     |
| -------------------- | -------------------------- |
| `rklings.h.f.<name>` | Hide a file name           |
| `rklings.u.f.<name>` | Show that file name again  |
| `rklings.h.i.<pid>`  | Hide a process ID          |
| `rklings.u.i.<pid>`  | Show that process ID again |

It also declares the file and PID target lists and the `__x64_sys_getdents64`
kretprobe.
The TODOs connect those pieces.

## One filter handles both targets

`getdents64` is the system call that returns the contents of a directory.
It gives the caller one buffer containing records placed back to back:

```text
getdents64 buffer

offset 0                         offset 24
   |                                |
   v                                v
+----------------------------+--------------------------------+
| record 1: 24 bytes         | record 2: 32 bytes             |
| inode | type | d_reclen=24 | inode | type | d_reclen=32     |
| name="notes"               | name="secret"                  |
+----------------------------+--------------------------------+
   |                                |
   +-- next offset = 0 + 24         +-- next offset = 24 + 32

Advance by d_reclen, not by sizeof(struct linux_dirent64).
```

The name makes each record a different size. `d_reclen` stores the complete size
of that record, including its name and alignment padding.
Adding `d_reclen` to the current offset lands on the next record.

When `secret` is hidden, move the remaining bytes over its record and return the
smaller byte count:

```text
before filtering                     after filtering
+----------+----------+----------+   +----------+----------+
| notes    | secret   | report   |   | notes    | report   |
+----------+----------+----------+   +----------+----------+
                  +-- remove ----------------^
```

Linux exposes each running process as a numbered directory under `/proc`.
The same name filter therefore handles files and processes:

```text
filesystem listing                   process listing

ls /tmp                              ps
   |                                  | reads /proc
   v                                  v
getdents64("/tmp")                   getdents64("/proc")
   |                                  |
   +-- notes                          +-- 1
   +-- secret  -- remove              +-- 1234  -- remove
   +-- report                         +-- 1402
   |                                  |
   v                                  v
notes, report                        PID 1234 is omitted

cat /tmp/secret prints "still here"  kill -0 1234 still succeeds
```

Only discovery through the directory listing changes.
The file and process continue to exist.

## Implement it

### 1. Complete the target lists

Implement `rk_target_add`, `rk_target_remove`, and `rk_target_match`.

- Protect every read and write with `spin_lock_irqsave` and
  `spin_unlock_irqrestore`.
- Store names with `strscpy`.
- Reject a full list with `-ENOSPC`.
- Match exact names with `strcmp`.
- On removal, use `memmove` to close the gap and decrement `count`.

Both add and remove must leave the list unchanged when they fail.

### 2. Decide whether to hide a record

Implement `should_hide_dirent` by checking the record name against both target
lists.

File names and PID strings use exact matching.
Hiding `12` must not hide `123`.

### 3. Save the caller's output buffer

The filter runs after the kernel has produced the directory records.
At that point you need two values:

- the address of the caller's output buffer; and
- the number of bytes the kernel placed in it.

They become available at different moments:

```text
userspace calls getdents64(fd, buffer, capacity)
                           |
                           | buffer address is available now
                           v
+-------------------------------------------------------------+
| kretprobe entry handler                                     |
| save buffer in this call's ri->data                         |
+----------------------------+--------------------------------+
                             |
                             v
                 kernel fills that buffer
                             |
                             v
+-------------------------------------------------------------+
| kretprobe return handler                                    |
| read returned byte count, then filter the saved buffer      |
+----------------------------+--------------------------------+
                             |
                             v
                  userspace receives the result
```

The return handler receives the result of the call, but not a convenient copy of
its original arguments.
Save the buffer address on entry so it remains available on return.

Do not use a global variable for it.
Two processes can call `getdents64` at the same time, and one global address
would let the calls overwrite each other.
Every active kretprobe call instead owns a separate `ri->data` area:

```c
struct getdents_data {
	void __user *ubuf;
};
```

#### Finding the buffer argument

In a normal kernel function, the function arguments are directly available from
the probe's register frame. `__x64_sys_getdents64` is different: it is an x86-64
syscall wrapper whose first kernel argument is a pointer to the userspace
syscall registers.

```text
kretprobe register frame
+--------------------------------------+
| argument 0                           |
|   +-- pointer to syscall pt_regs -------------+
+--------------------------------------+        |
                                                v
                                  syscall register frame
                           +------------------------------+
                           | di = fd                      |
                           | si = userspace buffer        |
                           | dx = buffer capacity         |
                           +------------------------------+
```

First recover that inner register frame, then read `si`, the second syscall
argument:

```c
struct pt_regs *uregs;
struct getdents_data *data;

data = (struct getdents_data *)ri->data;
uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
data->ubuf = (void __user *)uregs->si;
```

The `__user` annotation records that `ubuf` points into userspace.
Do not dereference it as kernel memory.
The return handler will use `copy_from_user` and `copy_to_user` to cross that
boundary safely.

### 4. Remove matching records

In `getdents_return`:

1. Read the returned byte count with `regs_return_value`.
2. Leave immediately when the count is zero or negative.
3. Allocate a kernel buffer and copy the returned bytes from userspace.
4. Walk the buffer by each record's `d_reclen`.
5. Remove a matching record with `memmove` and reduce the byte count.
6. Copy the compacted buffer back and replace the syscall return value.
7. Free the kernel buffer on every path.

Validate `d_reclen` before using it.
It must contain the fixed record fields, fit inside the remaining bytes, and
contain a terminated name.

After removing a record, inspect the same offset again because the next record
has moved into that position.

### 5. Own the probe lifecycle

Initialize both spinlocks before registering probes.
Register the directory probe and the two supplied `mkdir` probes with rollback
for every partial failure.
Unregister them in reverse order during module exit.

## Test it

Build the implementation and enter the VM:

```console
$ ./rklings run L11
```

Create the test targets and note the process ID printed by the shell:

```console
$ printf 'still here\n' > /tmp/secret
$ sleep 3600 &
[1] 1234
```

Then hide and restore the file with the same value:

```console
$ mkdir rklings.h.f.secret
$ ls /tmp | grep secret
$ cat /tmp/secret
still here
$ mkdir rklings.u.f.secret
$ ls /tmp | grep secret
secret
```

Then do the same for the live process:

```console
$ mkdir rklings.h.i.1234
$ ps -p 1234
$ kill -0 1234
$ mkdir rklings.u.i.1234
$ ps -p 1234
    PID TTY          TIME CMD
   1234 pts/0    00:00:00 sleep
$ kill 1234
```

The filter affects directory listings only.
It does not block direct access to a known path or PID.

## Validate

From the repository root:

```console
$ ./rklings validate L11
```

The validator checks file hiding, direct file access, process hiding, process
liveness, restoration, and repeated hide/show cycles.

## Next

Files and processes can disappear from listings, but sensitive records inside a
visible file still give you away.
L12 hides selected lines from tools such as `cat` and `grep` while leaving the
file in place.
