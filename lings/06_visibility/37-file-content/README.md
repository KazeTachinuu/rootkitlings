# 37 - Replace content in one read result

A read-path filter can change bytes returned to userspace without modifying the file on disk.
This module replaces each `SECRET` in reads from `/tmp/rk_target` with the equal-length string `XXXXXX`.

## Observe it

```sh
echo 'data SECRET data' > /tmp/rk_target
insmod hello.ko
dd if=/tmp/rk_target bs=4096 count=1 2>/dev/null
cat /proc/rk_content
rmmod hello
dd if=/tmp/rk_target bs=4096 count=1 2>/dev/null
```

```text
data XXXXXX data
redactions=1
data SECRET data
```

BusyBox `cat` may use `sendfile()`, so the example deliberately uses `dd` to cross the hooked `read()` path.

## How it works

The kretprobe entry handler saves the file descriptor and userspace buffer from `__x64_sys_read`.
At load time, the module opens `/tmp/rk_target` and retains its inode identity.
After the real read returns, the return handler resolves the descriptor with `fdget()`, matches that exact inode, and copies the returned bytes into atomic scratch memory.

```text
disk bytes ---> real read ---> private copy ---> SECRET to XXXXXX ---> userspace
     |
     +---------------------------------------------------- unchanged
```

Equal-length replacement preserves the syscall byte count and avoids shifting partial read data.
No-fault user-copy helpers keep the probe from sleeping on an invalid userspace page.
Every successful `fdget()` has a matching `fdput()`.
A different directory may contain the same basename without being filtered.

## Implement it

```sh
./rootkitlings start L37
$EDITOR .rootkitlings/work/read-view-filtering/hello.c
```

Inside the provided bounded match loop, replace the current occurrence with `REDACT_TO` using `memcpy(p, REDACT_TO, REDACT_LEN)`.

The target-file check, bounds, counter, copy-back, and failure behavior are provided.

## Verify

```sh
./rootkitlings check L37
```

The check creates the target at runtime, observes replacement and its counter, unloads the module, and proves the original disk content is visible again.
