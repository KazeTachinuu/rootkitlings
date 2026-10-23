# L11: Hide Files and Processes from Listings

## Mission

L10 gave the module a target list, but adding a name changes nothing visible.
This exercise makes a file and a running process disappear from ordinary
listings.

Neither target is deleted or stopped. You will change only the directory entries
returned to programs such as `ls` and
`ps`.

## What you will observe

After implementing the filter, create one file and one long-running process:

```console
root@victim:~# echo 'still here' > /tmp/secret
root@victim:~# sleep 3600 &
[1] 1234
root@victim:~# ls /tmp | grep secret
secret
root@victim:~# ps -p 1234
    PID TTY          TIME CMD
   1234 pts/0    00:00:00 sleep
```

Your finished module will support these commands:

```console
root@victim:~# mkdir rklings.h.f.secret
root@victim:~# mkdir rklings.h.i.1234
```

Afterward, `ls` and `ps` must omit the targets while direct access still works:

```console
root@victim:~# ls /tmp | grep secret
root@victim:~# cat /tmp/secret
still here
root@victim:~# kill -0 1234
```

An empty `grep` result means the name is hidden.
Successful `cat` and `kill -0` commands prove the targets still exist.

## Workspace

```text
exercises/L11-dirent-filter
├── Kbuild
└── main.c
```

`main.c` is the only editable file.
It already intercepts `mkdir` and recognizes four commands:

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
It gives the caller one buffer containing `struct linux_dirent64` records placed
back to back.

Each record contains these fields:

```c
struct linux_dirent64 {
    __u64        d_ino;     /* inode number             */
    __s64        d_off;     /* offset to next record    */
    unsigned short d_reclen; /* size of this record      */
    unsigned char  d_type;  /* file type                */
    char         d_name[];  /* null-terminated filename */
};
```

The `d_name` field is variable length, so each record is a different size.
`d_reclen` stores the complete size of that record, including its name and
alignment padding.

```text
getdents64 buffer

offset 0                         offset 24
   │                                |
   v                                v
+----------------------------+--------------------------------+
| record 1: 24 bytes         | record 2: 32 bytes             |
| d_ino | d_type | d_reclen  | d_ino | d_type | d_reclen      |
| d_name="notes"             | d_name="secret"                |
+----------------------------+--------------------------------+
   │                                |
   └── next offset = 0 + 24         └── next offset = 24 + 32

Advance by d_reclen, not by sizeof(struct linux_dirent64).
```

Adding `d_reclen` to the current offset lands on the next record.

When `secret` is hidden, move the remaining bytes over its record and return the
smaller byte count:

```text
before filtering                     after filtering
+----------+----------+----------+   +----------+----------+
| notes    | secret   | report   │   | notes    | report   |
+----------+----------+----------+   +----------+----------+
                  └── remove ----------------^
```

Linux exposes each running process as a numbered directory under `/proc`.
The same name filter therefore handles files and processes:

```text
filesystem listing                   process listing

ls /tmp                              ps
   │                                  | reads /proc
   v                                  v
getdents64("/tmp")                   getdents64("/proc")
   │                                  |
   ├── notes                          ├── 1
   ├── secret  -- remove              ├── 1234  -- remove
   └── report                         └── 1402
                                      |
   v                                  v
notes, report                        PID 1234 is omitted

cat /tmp/secret prints "still here"  kill -0 1234 still succeeds
```

Only discovery through the directory listing changes.
The file and process continue to exist.

## TODOs

```text
TODO 1  main.c:28:1   complete the target list
TODO 2  main.c:48:2   save the caller's output buffer
TODO 3  main.c:54:2   remove matching records
TODO 4  main.c:201:2  register probes with rollback
TODO 5  main.c:208:2  unregister probes in reverse
```

### TODO 1 -- `main.c:28:1` -- Complete the target list

Implement `rk_target_add`, `rk_target_remove`, `rk_target_match`, and
`should_hide_dirent`.

- Protect every read and write with `spin_lock_irqsave` /
  `spin_unlock_irqrestore`
- Store names with `strscpy`
- Reject a full list with `-ENOSPC`
- Match exact names with `strcmp`
- On removal, use `memmove` to close the gap and decrement `count`

Both add and remove must leave the list unchanged when they fail.

`should_hide_dirent` checks the record name against both target lists.
File names and PID strings use exact matching.
Hiding `12` must not hide `123`.

### TODO 2 -- `main.c:48:2` -- Save the caller's output buffer

The buffer address and byte count become available at different moments:

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

Same register pattern as L08: recover the inner `pt_regs` with
`regs_get_kernel_argument(regs, 0)`, read `si` (the userspace buffer address),
and store it in `ri->data`.
The return handler reads the byte count with `regs_return_value` and accesses
the saved buffer through `copy_from_user` and `copy_to_user`.

### TODO 3 -- `main.c:54:2` -- Remove matching records

In `getdents_return`:

1. Read the returned byte count with `regs_return_value`.
2. Leave immediately when the count is zero or negative.
3. Allocate a kernel buffer with `kmalloc(total, GFP_KERNEL)` and copy the
   returned bytes from userspace.
4. Walk the buffer by each record's `d_reclen`.
5. Remove a matching record with `memmove` and reduce the byte count.
6. Copy the compacted buffer back and replace the syscall return value.
7. Free the kernel buffer on every path.

Validate `d_reclen` before using it:

- Use `offsetof(struct linux_dirent64, d_name)` to compute the fixed header
  size; `d_reclen` must be at least that plus one byte
- It must also fit inside the remaining bytes
- Use `memchr` to confirm a null terminator exists within the name portion

After removing a record, inspect the same offset again because the next record
has moved into that position.

### TODO 4 -- `main.c:201:2` -- Register probes with rollback

Initialize both spinlocks before registering probes.
Register the directory probe and the two supplied `mkdir` probes with rollback
for every partial failure.

### TODO 5 -- `main.c:208:2` -- Unregister probes in reverse

Unregister all three probes in reverse order during module exit.

## Test it

Build the implementation and enter the VM:

```console
$ ./rklings run L11
```

Create the test targets and note the process ID printed by the shell:

```console
root@victim:~# echo 'still here' > /tmp/secret
root@victim:~# sleep 3600 &
[1] 1234
```

Then hide and restore the file with the same value:

```console
root@victim:~# mkdir rklings.h.f.secret
root@victim:~# ls /tmp | grep secret
root@victim:~# cat /tmp/secret
still here
root@victim:~# mkdir rklings.u.f.secret
root@victim:~# ls /tmp | grep secret
secret
```

Then do the same for the live process:

```console
root@victim:~# mkdir rklings.h.i.1234
root@victim:~# ps -p 1234
root@victim:~# kill -0 1234
root@victim:~# mkdir rklings.u.i.1234
root@victim:~# ps -p 1234
    PID TTY          TIME CMD
   1234 pts/0    00:00:00 sleep
root@victim:~# kill 1234
```

The filter affects directory listings only.
It does not block direct access to a known path or PID.

## Validate


```console
$ ./rklings validate L11
  [+] Hidden file vanishes from ls
  [+] Unhidden file reappears
  [+] Hidden PID vanishes from ps
  [+] Unhidden PID reappears
  PASS
```


## Next

Files and processes can disappear from listings, but sensitive records inside a
visible file still give you away.
L12 hides selected lines from tools such as `cat` and `grep` while leaving the
file in place.
