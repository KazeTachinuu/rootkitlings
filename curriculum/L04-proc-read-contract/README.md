# L04: Proc Write Handler

Your proc file is read-only.
A rootkit needs to receive commands, not just report status.
Writing to `/proc/rklings` will let userspace control the module with nothing
more than `echo`.

## copy_from_user

Userspace and kernel memory are separate address spaces.
The kernel must copy data across the boundary explicitly:

```c
if (copy_from_user(buf, ubuf, len))
	return -EFAULT;
```

`__user` marks a pointer as userspace.
`copy_from_user` returns 0 on success, nonzero on fault.
Always check: a malicious process can pass an unmapped address.

## proc_ops

A writable proc entry needs the full `proc_ops` struct instead of
`proc_create_single`:

```c
static const struct proc_ops rk_proc_ops = {
	.proc_open    = rk_proc_open,
	.proc_read    = seq_read,
	.proc_write   = rk_proc_write,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

proc_create("rklings", 0644, NULL, &rk_proc_ops);
```

Permission `0644`: owner writes, everyone reads.
`proc_open` uses `single_open` to wire seq_file for reads.
`proc_write` handles input.

```text
  echo "1" > /proc/rklings
       |
       v
  rk_proc_write(ubuf, count)
       |
       v
  copy_from_user --> buf[0]=='1' --> set_active(true)
       |
       v
  return count

  cat /proc/rklings --> rk_status_show --> "active=1\n"
```

## The trap

If `copy_from_user` fails and you use `buf` anyway, you read uninitialized
kernel stack.
On a hardened kernel this is a fatal fault.

## You write

`interface/proc.c` (~76 lines):

- `rk_proc_write` with `copy_from_user`
- `proc_ops` struct
- switch from `proc_create_single` to `proc_create`

`include/rootkit.h`:

- add `rk_state_set_active`

`core/state.c`:

- implement `rk_state_set_active`

## API

```c
void rk_state_set_active(struct rk_state *state, bool active);

unsigned long copy_from_user(void *to, const void __user *from,
    unsigned long n);
struct proc_dir_entry *proc_create(const char *name, umode_t mode,
    struct proc_dir_entry *parent, const struct proc_ops *ops);
int single_open(struct file *file,
    int (*show)(struct seq_file *, void *), void *data);
```

## Test it

```
$ rklings run l04
root@vm:~# insmod rklings.ko
root@vm:~# cat /proc/rklings
active=1
root@vm:~# echo 0 > /proc/rklings
root@vm:~# cat /proc/rklings
active=0
root@vm:~# echo 1 > /proc/rklings
root@vm:~# cat /proc/rklings
active=1
root@vm:~# echo x > /proc/rklings
bash: echo: write error: Invalid argument
```

## Validate

```
$ rklings validate l04
  [+] builds at all fail points
  [+] /proc/rklings permissions are 0644
  [+] cat shows active=1 after load
  [+] echo 0 sets active=0
  [+] echo 1 restores active=1
  [+] echo x returns -EINVAL
  [+] rmmod removes proc entry cleanly
  PASS
```

## Next

Your module takes commands through proc, but proc is visible to any defender who
looks.
L05 introduces a covert channel: intercepting mkdir so the module reacts to
`mkdir rklings.something` with no visible interface.
