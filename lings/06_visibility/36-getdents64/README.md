# 36 - Filter directory enumeration

`getdents64()` returns a packed sequence of variable-length directory records.
Removing selected records from that result hides names from `ls` while direct path access continues to work.

## Observe it

```sh
insmod hello.ko
ls /proc | grep '^rk_'
cat /proc/rk_dirfilter

echo test > /tmp/rk_secret
ls /tmp | grep '^rk_'
cat /tmp/rk_secret
```

```text
hidden
test
```

Enumeration omits both `rk_` names, but direct lookup reaches them.

## Return-path filtering

```text
entry handler                         return handler
     |                                      |
     +-- save userspace dirent buffer       +-- read byte count
                                            +-- no-fault copy
real syscall fills variable records         +-- validate each d_reclen
                                            +-- remove rk_* records
                                            +-- publish shorter result
```

The x86-64 syscall wrapper receives a pointer to saved userspace registers in `regs->di`.
The inner register `si` contains the directory-buffer pointer.
Kretprobe per-instance data carries that pointer from entry to return without using global state.

The provided return handler allocates scratch memory with `GFP_ATOMIC` and uses no-fault user-copy helpers because probe callbacks cannot sleep or take a userspace page fault.
It validates every record before reading `d_name` or advancing by `d_reclen`.
If allocation, validation, or copying fails, it preserves the original syscall result.

## Implement it

```sh
./rootkitlings start L36
$EDITOR module/hello.c
```

Complete the marked decision in the existing record loop.
Use `strncmp()` to test whether `cur->d_name` starts with `HIDDEN_PREFIX`, whose length is `sizeof(HIDDEN_PREFIX) - 1`.

The entry capture, record validation, compaction, copy-back, return-value update, proc status, and probe lifecycle are provided.

## Verify

```sh
./rootkitlings check L36
```

The check requires hidden enumeration, successful direct access, and clean proc and probe removal.
