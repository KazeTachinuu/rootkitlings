# Open rklings_hook.h and understand it

You have used `rklings_hook.h` for two lessons without knowing how it works.
Now open it. Read every line. Then delete the `#include` and rewrite the hook
from scratch in your own code.

## What the header does

Open `curriculum/include/rklings_hook.h` and follow along.

### Kretprobe vs Kprobe

A **kprobe** fires a `pre_handler` just before a function runs. It observes
but cannot change the return value.

A **kretprobe** fires two handlers:
- `entry_handler`: runs before the function, can save state
- `handler` (return handler): runs after the function returns, can change the
  return value

Each firing gets its own `kretprobe_instance` with a `data` field for passing
state from entry to return. This is how the header bridges the two halves.

### Per-instance data

```c
struct rk_mkdir_data {
    int result;
    bool intercepted;
};
```

The entry handler decides whether to intercept and stores the result. The
return handler checks `intercepted` and overwrites the syscall return value.

### The nested pt_regs trick

`__x64_sys_mkdir` is a syscall wrapper. Its signature is:

```c
long __x64_sys_mkdir(const struct pt_regs *regs);
```

The kretprobe's entry handler receives its own `pt_regs` (the kprobe trap
frame). To reach the actual syscall arguments:

```c
uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
pathname = (const char __user *)uregs->di;
```

`regs_get_kernel_argument(regs, 0)` extracts the first function argument (the
inner pt_regs pointer). Then `uregs->di` is the RDI register, which holds the
first syscall argument: the userspace pathname.

### Copying and checking the path

```c
copied = strncpy_from_user(buf, pathname, sizeof(buf));
```

`strncpy_from_user` safely copies a string from userspace. Returns the number
of bytes copied, or negative on fault.

After copying, `strrchr(buf, '/')` finds the last slash to extract the
basename. The "rklings." prefix check uses `strncmp`.

### Suppressing the real mkdir

```c
put_user('\0', (char __user *)pathname);
```

Writing a null byte to the start of the userspace path makes the real mkdir
see an empty string. It fails harmlessly. The return handler then overwrites
the error code with our result using `regs_set_return_value`.

## Your task

1. Read `curriculum/include/rklings_hook.h` completely
2. Delete `#include "rklings_hook.h"` from your code
3. Add the required kernel headers (`kprobes.h`, `ptrace.h`, `uaccess.h`)
4. Write the `struct rk_mkdir_data` definition
5. Write `rk_mkdir_entry` (the entry handler)
6. Write `rk_mkdir_return` (the return handler)
7. Define the `struct kretprobe` for `__x64_sys_mkdir`
8. Use `register_kretprobe` / `unregister_kretprobe` in init/exit

Everything should work exactly as before: `mkdir rklings.e` grants root,
`mkdir rklings.e.PID` escalates the target.

## Verify

```sh
$ mkdir rklings.e && id
uid=0(root)
```

Run:

```sh
./rklings check
```

Confirm your code does NOT include rklings_hook.h.
