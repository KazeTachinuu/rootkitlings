# Management commands: INFO, LIST, UNHIDE ALL

A rootkit that can only hide things is hard to operate. The operator needs
introspection: what host is this, what is currently hidden, and how to reset
state. This lesson adds three management commands to the wire protocol.

## Command overview

```text
operator --> TCP --> "INFO\n"
                      |
                      v
            utsname()->nodename   "host01"
            utsname()->release    "7.2.5-4-omarchy"
            utsname()->machine    "x86_64"
                      |
                      v
            format and send response

operator --> TCP --> "LIST FILE\n"
                      |
                      v
            iterate hidden_files[], format "[0] secret_doc\n[1] .hidden\n"
                      |
                      v
            send response (or "empty\n")

operator --> TCP --> "UNHIDE ALL\n"
                      |
                      v
            set file_count = 0, content_count = 0, port_count = 0
                      |
                      v
            send "cleared\n"
```

## System identification

The kernel exposes the system's identity through `utsname()`, which returns a
pointer to `struct new_utsname`. The fields you need:

```c
#include <linux/utsname.h>

struct new_utsname *uts = utsname();
/* uts->nodename   -- hostname */
/* uts->release    -- kernel release string */
/* uts->machine    -- architecture */
```

These are kernel-internal strings. Do not modify them.

## Target list introspection

The hiding lessons (L10 through L14) maintain arrays of hidden targets with
counts. LIST iterates the appropriate array and formats each entry with its
index, so the operator can selectively unhide by index later.

Supported categories:

- `LIST FILE` iterates hidden file prefixes
- `LIST CONTENT` iterates hidden content strings
- `LIST PORT` iterates hidden port numbers

Format each entry as `[index] value\n`. If the list is empty, send `empty\n`.

## Clearing all targets

`UNHIDE ALL` resets every target count to zero. This is the emergency reset:
all hiding stops immediately. No need to iterate or free; the arrays are static,
so setting the count to zero is sufficient.

## Sending responses

Use `kernel_sendmsg` with a `struct kvec` to push formatted data out the
socket. Format each response line into a stack buffer with `snprintf`, then
send:

```c
struct msghdr msg = { 0 };
struct kvec vec = { .iov_base = buf, .iov_len = len };
kernel_sendmsg(sock, &msg, &vec, 1, len);
```

## Your task

Edit `main.c`:

1. Implement `rk_cmd_info`: format and send nodename, release, machine.
2. Implement `rk_cmd_list`: accept a category string ("FILE", "CONTENT",
   "PORT"), iterate the corresponding array, format and send each entry.
3. Implement `rk_cmd_unhide_all`: set all target counts to zero, send
   confirmation.
4. Wire these into the command dispatcher (match "INFO", "LIST ...",
   "UNHIDE ALL").

Run:

```sh
./rklings check
```

## References

- [`utsname()` and `struct new_utsname`](https://github.com/torvalds/linux/blob/v7.2/include/linux/utsname.h)
- [`kernel_sendmsg`](https://github.com/torvalds/linux/blob/v7.2/include/linux/net.h)
