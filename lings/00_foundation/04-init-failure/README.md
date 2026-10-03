# 04 - Init failure and debugging

## Why this step

So far, init always succeeded. Real initialization can fail after it starts.
What happens to the module? Does exit run? Who cleans up?

More importantly: when something goes wrong, how do you find out what
happened? This ling teaches both controlled failure and the debugging
tools you will use for every ling after this.

## The decision tree

```
  insmod hello.ko
        |
   hello_init()
        |
   return value?
       / \
      /   \
   0       negative
   |          |
 LOADED     REJECTED
   |          |
 rmmod      no exit
   |       no cleanup
 hello_exit()
```

## Make one controlled change

In [hello.c](../../../module/hello.c), add:

```c
#include <linux/errno.h>
```

Change only the final line of `hello_init`:

```c
return -EINVAL;
```

`EINVAL` means "invalid argument."
Kernel functions report failures with negative errno values.

## Your debugging toolkit

These tools work in every ling. Learn them now.

### dmesg: the kernel log

```
  your module             kernel log (dmesg)
  +-----------+           +----------------------------------+
  | pr_info() | --------> | [  1.234] hello: loaded          |
  | pr_err()  | --------> | [  1.235] hello: init failed     |
  | BUG/oops  | --------> | [  1.236] BUG: unable to ...    |
  +-----------+           |          Call Trace:              |
                          |           my_func+0x1a/0x40      |
                          +----------------------------------+
```

```sh
dmesg              # full kernel log
dmesg | tail       # last lines only
dmesg -w           # watch live (like tail -f)
dmesg -c           # read and clear (clean slate for next test)
```

### printk: tracing execution

The kernel has log levels, each with a shorthand macro:

```
  Level     Macro        When to use
  -------   ----------   -----------------------------------
  KERN_ERR  pr_err()     something failed, module can't work
  KERN_WARN pr_warn()    unexpected but recoverable
  KERN_INFO pr_info()    normal operation milestones
  KERN_DEBUG pr_debug()  temporary tracing (off by default)
```

```c
pr_info("hello: loaded\n");                    // normal event
pr_err("hello: allocation failed\n");          // real error
pr_warn("hello: fallback to default\n");       // degraded
pr_debug("hello: counter=%d ptr=%px\n", n, p); // dev tracing
```

`pr_debug` is compiled out unless you build with `#define DEBUG`
before the includes, or enable dynamic debug at runtime:

```sh
echo 'module hello +p' > /sys/kernel/debug/dynamic_debug/control
```

Format specifiers for kernel types:

```
  %d     int               pr_info("pid=%d\n", current->pid);
  %u     unsigned int      pr_info("count=%u\n", n);
  %lu    unsigned long     pr_info("addr=%lu\n", addr);
  %px    pointer (real)    pr_info("ptr=%px\n", ptr);
  %p     pointer (hashed)  hides address, useless for debugging
  %s     string            pr_info("name=%s\n", current->comm);
  %zu    size_t            pr_info("size=%zu\n", count);
```

Use `%px` for kernel pointers (not `%p`, which hashes the address).
Remove debug prints before submitting.

### Reading a crash (kernel oops)

When your module crashes, dmesg shows a call trace. Read it bottom-up:

```
  BUG: kernel NULL pointer dereference
  RIP: 0010:my_hook+0x1a/0x40 [hello]      <-- crash site
  Call Trace:
   do_sys_openat2+0x5b/0x230                <-- who called it
   __x64_sys_openat+0x54/0x90               <-- syscall entry
```

- `[hello]` means the crash is in your module
- `+0x1a` is how far into the function
- The trace shows the full call chain

### Common mistakes

| Symptom | Likely cause |
|---------|-------------|
| `invalid module format` | Wrong kernel. `make clean && make`. |
| `Unknown symbol` | Missing `#include`. Check the README. |
| `Segmentation fault` in dmesg | NULL pointer. Check before use. |
| Check fails but module loads | Output format mismatch. Match the contract. |
| `can't unload, in use` | Something holds a reference (proc, timer, thread). |

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
echo "status=$?"
lsmod
dmesg | tail
```

`$?` is the status of the immediately preceding shell command.
Zero means success; non-zero means failure.

Now try debugging: add a `pr_info` before the return:

```c
pr_info("hello: about to return -EINVAL\n");
return -EINVAL;
```

Rebuild (`make`), run again, check `dmesg`. You should see both
messages. This is how you trace execution in kernel code.

## Contract

The check script verifies:

- `insmod hello.ko` fails (non-zero exit status).
- `dmesg` must contain `hello: loaded` (init ran before returning error).
- `hello` must NOT appear in `lsmod` after failed init.
- `dmesg` must NOT contain `hello: unloaded` (exit was not called).

After verifying, restore `return 0`, remove the now-unused `linux/errno.h`
include, then `./rootkitlings run` and load again. Loading should succeed.

## Verify

```bash
./rootkitlings check 4
```
> **What a defender sees**: the failed load still wrote to `dmesg`. The kernel log
> records every `insmod` attempt, successful or not. A defender sees both.

See also: [05 - Parameters](../05-parameter/README.md).
