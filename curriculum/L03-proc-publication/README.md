# L03: Proc Publication

Your module owns state, but nothing outside the kernel can see it.

`/proc` is a virtual filesystem the kernel generates on the fly.
Each file is backed by a callback, not data on disk.
Reading `/proc/rklings` calls your function and returns whatever it prints.
This is how you make kernel state visible to userspace.

## procfs and seq_file

The simplest way to create a read-only proc entry is `proc_create_single`:

```c
proc_create_single("rklings", 0444, NULL, rk_status_show);
```

This creates `/proc/rklings` with mode `0444` (world-readable).
When someone reads it, the kernel calls `rk_status_show`:

```c
static int rk_status_show(struct seq_file *output, void *unused)
{
	seq_puts(output, "active\n");
	return 0;
}
```

`seq_file` is the kernel's buffered output interface.
It handles partial reads, large outputs, and seeking.
`seq_puts` writes a string, `seq_printf` works like printf.
Returning 0 means success.

## Lifecycle integration

The proc entry depends on module state: it must be created after state is ready
and removed before state is destroyed.
This extends the goto cleanup chain from L01:

```text
  rk_init()
    |
    +---> rk_state_create()
    |         |
    |        ok
    |         v
    +---> rk_proc_publish()
    |         |
    |     proc_create_single("rklings", 0444, ...)
    |         |
    |         v
    |     /proc/rklings  <--cat-- "active\n"
    |
    |   (failure at any step)
    |         |
    |         v
    |     goto: destroy state, return error

  rk_exit()
      rk_proc_unpublish()    // remove /proc entry first
      rk_state_destroy()     // then free state
```

Exit order is the reverse of init order: remove the proc entry first (so no
reader can access state), then free the state.

## File organization

A new file `interface/proc.c` owns the proc entry.
The main module calls `rk_proc_publish` after state is ready and
`rk_proc_unpublish` before state is destroyed.
The Kbuild file adds `interface/proc.o` to the `rklings-y` list.

## You write

`interface/proc.c` (~37 lines):

- `rk_status_show`
- `rk_proc_publish`
- `rk_proc_unpublish`

`core/main.c` (~53 lines):

- goto cleanup for proc after state

`Kbuild`:

- add `interface/proc.o` to `rklings-y`

## API

```c
// proc.c
static int rk_status_show(struct seq_file *output, void *unused);
__must_check int rk_proc_publish(void);
void rk_proc_unpublish(void);

// Kernel calls
struct proc_dir_entry *proc_create_single(
    const char *name, umode_t mode,
    struct proc_dir_entry *parent,
    int (*show)(struct seq_file *, void *));
void proc_remove(struct proc_dir_entry *de);
void seq_puts(struct seq_file *m, const char *s);
```

## Test it

```
$ rklings run l03
root@vm:~# insmod rklings.ko
root@vm:~# cat /proc/rklings
active
root@vm:~# ls -l /proc/rklings
-r--r--r-- 1 root root 0 ... /proc/rklings
root@vm:~# rmmod rklings
root@vm:~# cat /proc/rklings
cat: /proc/rklings: No such file or directory
```

## Validate

```
$ rklings validate l03
  [+] builds at all fail points
  [+] /proc/rklings exists after insmod
  [+] cat /proc/rklings returns "active"
  [+] permissions are 0444 (read-only)
  [+] FAIL_POINT=3: proc publish fails, module rejects load, state freed
  [+] rmmod removes /proc/rklings
  PASS
```

## Next

The proc file is read-only.
L04 adds a write handler with copy_from_user so userspace can send commands to
the module.
