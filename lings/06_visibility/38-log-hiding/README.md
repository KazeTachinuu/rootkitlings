# 38 - Filter one kernel-log read path

BusyBox `dmesg` reads the kernel log through the syslog syscall.
This module removes complete lines containing `rk:` from that returned buffer while retaining unrelated lines.

## Observe it

```sh
insmod hello.ko
dmesg | grep 'rk:'
cat /proc/rk_logfilter
echo 'marker survives' > /dev/kmsg
dmesg | grep 'marker survives'
```

```text
hidden=<positive count>
marker survives
```

The filtered search prints nothing even though the module emitted matching records.

## How it works

The entry handler saves the syslog action and userspace buffer from the x86-64 syscall registers.
The return handler acts only on read actions 2 and 3 with a positive returned length.

```text
kernel log ---> syslog read ---> split complete lines ---> omit rk: ---> dmesg
     |
     +-------------------- other readers may retain the original records
```

The provided compactor finds line endings with bounded `memchr()` calls and copies retained lines forward.
`strnstr()` must receive the exact current line length so it cannot match text in a later line.
Atomic scratch allocation and no-fault user copies preserve probe-context rules.
Any failure leaves the original syscall result unchanged.

## Implement it

```sh
./rootkitlings start L38
$EDITOR module/hello.c
```

Complete the marked line decision.
Skip the line when `strnstr(src, HIDE_PREFIX, nl - src + 1)` returns non-NULL.

The action filter, line compaction, hidden counter, result update, and lifecycle are provided.

## Verify

```sh
./rootkitlings check L38
```

The check requires matching lines to disappear, a runtime marker to remain, the hidden counter to increase, and clean probe removal.

Another log reader or an external collector is an independent view and may retain the records before this filter runs.
