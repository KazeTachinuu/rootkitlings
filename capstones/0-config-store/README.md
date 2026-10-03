# Capstone 0: Config store

Build an in-kernel key/value store controlled through `/proc/rk_cfg`.

## Use what you learned

- proc read and write callbacks
- `copy_from_user()` at the userspace boundary
- heap allocation and linked-list cleanup
- `seq_file` for formatted output
- `call_usermodehelper()` for the `exec` command

## Commands

| Write to `/proc/rk_cfg` | Result |
|-------------------------|--------|
| `set:key=value` | Add or replace one entry |
| `del:key` | Delete one entry |
| `exec:command` | Run a command and store stdout in `_result` |

Reading `/proc/rk_cfg` returns one `key=value` pair per line.

The `exec` path reuses ling 16 exactly:

```text
call_usermodehelper("/bin/sh", "-c", command > temporary file)
        | wait for the child
        v
kernel_read(temporary file) ---> set_entry("_result", output)
        |
        +----------------------> rm -f temporary file
```

Use a fixed-size command buffer, check `call_usermodehelper()`, `filp_open()`,
and `kernel_read()`, and close the file on every path where it was opened. The
cleanup helper uses a fixed path; user-controlled text must never be inserted
into its `rm` command.

```sh
insmod hello.ko
echo 'set:target=192.168.1.1' > /proc/rk_cfg
echo 'set:interval=60' > /proc/rk_cfg
cat /proc/rk_cfg
# target=192.168.1.1
# interval=60

echo 'exec:uname -r' > /proc/rk_cfg
grep _result /proc/rk_cfg

echo 'del:target' > /proc/rk_cfg
rmmod hello
```

## Ownership

Each list node is allocated when its key is first set and freed when the key is
deleted or the module unloads. The proc entry is removed on unload. The command
output file is temporary and must not survive cleanup.

## Contract

The check verifies an empty initial store, insert, replacement, deletion,
multiple entries, command output, and cleanup after unload.

```bash
./rootkitlings check capstone-0
```

> **What a defender sees**: `/proc/rk_cfg`, the loaded module, the temporary
> output file while a command runs, and the userspace process spawned by the
> helper.
